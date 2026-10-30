#include "platform/esp32/network.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>

#include "esp_tls_errors.h"
#include "mqtt_client.h"
#include "platform/common/api_json.h"
#include "platform/esp32/aibuddy.h"

// The IDF's CA bundle (built into the SDK's mbedtls). Declared here because the
// header name is shadowed by WiFiClientSecure's copy, an Arduino variant that
// needs a bundle file of its own (and including it pulls in that library).
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace platform::net {

namespace {

constexpr const char* kNamespace = "cyd";
constexpr const char* kKey = "net";
constexpr uint32_t kTickMs = 500;
constexpr uint32_t kRetryMinMs = 10000;  // a join takes a few seconds; retries back off to the max
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kApFallbackMs = 60000;  // the home Wi-Fi unreachable this long brings the AP up
constexpr int kKeepaliveS = 30;      // the broker sends the last will about 1.5x this after a drop
constexpr int kMaxMessage = 2048;     // a card with every text field full and \u-escaped fits
constexpr const char* kOffline = "{\"online\":false}";
constexpr const char* kAibOnline = "{\"isConnected\":true}";  // AI Buddy's d/<number>/st (no retain: clean session)
constexpr const char* kAibOffline = "{\"isConnected\":false}";
constexpr const char* kDefaultNtp = "time.google.com";  // then pool.ntp.org and time.cloudflare.com
constexpr uint32_t kNtpPushMs = 10 * 60 * 1000;         // the clock is set from the system time this often
constexpr time_t kNtpValidAfter = 1700000000;           // earlier than Nov 2023: SNTP has not answered yet

struct Settings {
  char ssid[33] = "";
  char wifiPass[65] = "";
  char host[64] = "";
  uint16_t port = 0;  // 0: 1883, or 8883 over TLS
  bool tls = false;
  char user[65] = "";
  char mqttPass[65] = "";
  char topic[65] = "";  // "" = gDefaultTopic
  char ntp[48] = "";    // "" = kDefaultNtp
  int16_t tzMin = 540;  // minutes east of UTC, fixed (Korea has no daylight saving)
  // AI Buddy (aibuddy.h): the device's number and secret key. A number set switches the MQTT
  // client to that protocol: login, client id and topics come from it, user/pass/topic are unused
  char device[16] = "";
  char secret[65] = "";
};

enum class Link : uint8_t { Off, Connecting, Connected };
const char* const kLinkNames[] = {"off", "connecting", "connected"};

Hooks gHooks;
char gName[24] = "", gHostName[24] = "", gBoard[24] = "", gDefaultTopic[16] = "", gApPassword[24] = "";
std::atomic<bool> gApOn{false};

SemaphoreHandle_t gMutex = nullptr;  // gSettings: web server, serial, network task
Settings gSettings;
std::atomic<uint32_t> gGeneration{0};  // bumped by every change; the network task applies it

std::atomic<Link> gWifi{Link::Off}, gMqtt{Link::Off};
std::atomic<uint8_t> gWifiReason{0};  // why the last join failed, 0 = no failure yet
std::atomic<const char*> gMqttError{""};

// The client and its topics are replaced by the network task only while no
// client runs; publishOutcome() checks the handle under this mutex
SemaphoreHandle_t gClientMutex = nullptr;
esp_mqtt_client_handle_t gClient = nullptr;
char gTopicCard[80], gTopicTime[80], gTopicOutcome[80], gTopicStatus[80], gTopicBattery[80],
    gTopicBatteryGet[88], gTopicDebug[80], gTopicAsk[80];
char gTopicDown[32], gTopicUp[32];  // AI Buddy: d/<number>/dn and /up; gTopicStatus is d/<number>/st
std::atomic<bool> gAibuddy{false};  // the running client speaks AI Buddy's protocol

// Messages longer than the client's buffer arrive in pieces, gathered in
// gPieces; most fit in one and are read in place (MQTT task only)
enum class Inbox : uint8_t { None, Card, Time, Frame };
Inbox gMessageKind = Inbox::None;
char* gPieces = nullptr;

// Mutual TLS, test stage: the broker's CA, the device's certificate and its private key as PEM in
// NVS (the key readable from flash until the digital signature peripheral takes over). gPem holds
// what the running client was given; setPem() bumps the generation so poll() restarts the client.
constexpr int kPems = 3;
constexpr const char* kPemKeys[kPems] = {"tlsca", "tlscert", "tlskey"};
constexpr size_t kMaxPem = 3000;  // an RSA 2048 key is 1.7 KB; one NVS string holds up to 4000 bytes
char* gPem[kPems] = {};
uint32_t gAppliedPems = 0;
std::atomic<uint32_t> gPemChanges{0};

// The PEM kept in NVS as a malloc'd string, or nullptr
char* loadPem(int which) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return nullptr;
  String pem = prefs.isKey(kPemKeys[which]) ? prefs.getString(kPemKeys[which]) : String();
  prefs.end();
  if (!pem.length()) return nullptr;
  char* out = static_cast<char*>(malloc(pem.length() + 1));
  if (out) memcpy(out, pem.c_str(), pem.length() + 1);
  return out;
}

// SNTP, driven from poll(). lwIP keeps the pointer of the server name it is given, so the name
// lives here. gNtpSynced is read by toJson() on any thread.
char gNtpServer[48] = "";
bool gNtpStarted = false;
uint32_t gNtpPushedMs = 0;
std::atomic<bool> gNtpSynced{false};

// While an update runs MQTT (and its TLS buffers) is off; poll() does it on the loop task
std::atomic<bool> gMqttPaused{false};
bool gMqttPauseApplied = false;

// poll() state (loop task)
Settings gApplied;
uint32_t gAppliedGeneration = 0, gPollMs = 0, gNextTryMs = 0, gBackoffMs = kRetryMinMs, gDownSinceMs = 0;

void bringUpAp(bool withStation) {
  WiFi.mode(withStation ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(gName, gApPassword);
  gApOn = true;
}

// ---- Settings ----

// Copies o[key] into dst when present; false if it is not a string or does not fit
bool takeString(JsonObjectConst o, const char* key, char* dst, size_t size) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  const char* s = v.as<const char*>();
  if (!s || strlen(s) >= size) return false;
  memcpy(dst, s, strlen(s) + 1);
  return true;
}

bool parse(JsonObjectConst doc, Settings& s) {
  JsonObjectConst w = doc["wifi"].as<JsonObjectConst>(), m = doc["mqtt"].as<JsonObjectConst>(),
                  t = doc["time"].as<JsonObjectConst>();
  if (!takeString(w, "ssid", s.ssid, sizeof(s.ssid)) || !takeString(w, "pass", s.wifiPass, sizeof(s.wifiPass)) ||
      !takeString(m, "host", s.host, sizeof(s.host)) || !takeString(m, "user", s.user, sizeof(s.user)) ||
      !takeString(m, "pass", s.mqttPass, sizeof(s.mqttPass)) || !takeString(m, "topic", s.topic, sizeof(s.topic)) ||
      !takeString(t, "server", s.ntp, sizeof(s.ntp))) {
    return false;
  }
  JsonObjectConst a = doc["aibuddy"].as<JsonObjectConst>();
  if (!takeString(a, "device", s.device, sizeof(s.device)) || !takeString(a, "secret", s.secret, sizeof(s.secret))) {
    return false;
  }
  for (const char* p = s.device; *p; p++) {  // a number is a topic level: HB-0001
    if (!isalnum(uint8_t(*p)) && *p != '-' && *p != '_') return false;
  }
  if (!t["tz"].isNull()) {
    long tz = t["tz"] | 100000L;
    if (tz < -720 || tz > 840) return false;  // UTC-12:00 .. UTC+14:00
    s.tzMin = int16_t(tz);
  }
  if (!m["port"].isNull()) {
    long port = m["port"] | -1L;
    if (port < 0 || port > 65535) return false;
    s.port = uint16_t(port);
  }
  if (!m["tls"].isNull()) s.tls = m["tls"].as<bool>();
  // A topic here is a prefix to publish under: no wildcards, no empty levels at the end
  size_t n = strlen(s.topic);
  return !strpbrk(s.topic, "+#") && (n == 0 || s.topic[n - 1] != '/');
}

bool save(const Settings& s) {
  JsonDocument doc;
  doc["wifi"]["ssid"] = s.ssid;
  doc["wifi"]["pass"] = s.wifiPass;
  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["host"] = s.host;
  m["port"] = s.port;
  m["tls"] = s.tls;
  m["user"] = s.user;
  m["pass"] = s.mqttPass;
  m["topic"] = s.topic;
  doc["time"]["server"] = s.ntp;
  doc["time"]["tz"] = s.tzMin;
  doc["aibuddy"]["device"] = s.device;
  doc["aibuddy"]["secret"] = s.secret;
  char json[1024];
  size_t n = serializeJson(doc, json, sizeof(json));
  Preferences prefs;
  if (n >= sizeof(json) || !prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.putString(kKey, json) == n;
  prefs.end();
  return ok;
}

void load(Settings& s) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  String json = prefs.isKey(kKey) ? prefs.getString(kKey) : String();
  prefs.end();
  JsonDocument doc;
  Settings loaded;
  if (json.length() && !deserializeJson(doc, json.c_str(), json.length()) && parse(doc.as<JsonObjectConst>(), loaded)) {
    s = loaded;
  }
}

bool sameWifi(const Settings& a, const Settings& b) {
  return strcmp(a.ssid, b.ssid) == 0 && strcmp(a.wifiPass, b.wifiPass) == 0;
}

bool sameMqtt(const Settings& a, const Settings& b) {
  return strcmp(a.host, b.host) == 0 && a.port == b.port && a.tls == b.tls && strcmp(a.user, b.user) == 0 &&
         strcmp(a.mqttPass, b.mqttPass) == 0 && strcmp(a.topic, b.topic) == 0 && strcmp(a.device, b.device) == 0 &&
         strcmp(a.secret, b.secret) == 0;
}

const char* wifiError(uint8_t reason) {
  switch (reason) {
    case 0:
      return "";
    case WIFI_REASON_NO_AP_FOUND:
      return "no-ap";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "auth";
    default:
      return "other";
  }
}

// ---- MQTT ----

bool topicIs(const esp_mqtt_event_t& ev, const char* topic) {
  return size_t(ev.topic_len) == strlen(topic) && memcmp(ev.topic, topic, size_t(ev.topic_len)) == 0;
}

void publishStatus(esp_mqtt_client_handle_t c) {
  IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
  char json[160];
  int n = snprintf(json, sizeof(json), "{\"online\":true,\"name\":\"%s\",\"board\":\"%s\",\"ip\":\"%s\"}", gName,
                   gBoard, ip.toString().c_str());
  esp_mqtt_client_publish(c, gTopicStatus, json, n, 1, 1);
}

// AI Buddy frames to d/<number>/up, queued for the MQTT task like the outcome (any thread)
bool publishUp(const char* json, size_t len) {
  bool sent = false;
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  if (gClient && gMqtt == Link::Connected) sent = esp_mqtt_client_enqueue(gClient, gTopicUp, json, int(len), 1, 0, true) >= 0;
  xSemaphoreGive(gClientMutex);
  return sent;
}

void onMessage(const esp_mqtt_event_t& ev) {
  // Only the first piece names the topic. Retained messages come again with
  // every subscription: a stale card or time.
  if (ev.current_data_offset == 0) {
    free(gPieces);
    gPieces = nullptr;
    gMessageKind = Inbox::None;
    if (ev.retain || ev.total_data_len > kMaxMessage) {
      Serial.printf("mqtt: ignored %s message on %.*s\n", ev.retain ? "retained" : "oversized", ev.topic_len,
                    ev.topic);
    } else if (gAibuddy && topicIs(ev, gTopicDown)) {
      gMessageKind = Inbox::Frame;
    } else if (topicIs(ev, gTopicCard)) {
      gMessageKind = Inbox::Card;
    } else if (topicIs(ev, gTopicTime)) {
      gMessageKind = Inbox::Time;
    } else if (topicIs(ev, gTopicBatteryGet)) {  // the payload does not matter
      if (gHooks.batteryRequest) gHooks.batteryRequest();
    }
    if (gMessageKind != Inbox::None && ev.data_len < ev.total_data_len) {
      gPieces = static_cast<char*>(malloc(size_t(ev.total_data_len)));
      if (!gPieces) gMessageKind = Inbox::None;
    }
  }
  if (gMessageKind == Inbox::None) return;
  const char* json = ev.data;
  size_t len = size_t(ev.data_len);
  if (gPieces) {
    memcpy(gPieces + ev.current_data_offset, ev.data, len);
    len += size_t(ev.current_data_offset);
    if (len < size_t(ev.total_data_len)) return;
    json = gPieces;
  }

  if (gMessageKind == Inbox::Frame) {
    aibuddy::onFrame(json, len);
  } else if (gMessageKind == Inbox::Card) {
    engine::Event e;
    if (!api::cardFromJson(json, len, e)) {
      Serial.println("mqtt: card rejected (bad JSON, or a prompt without options)");
    } else {
      bool queued = gHooks.postCard && gHooks.postCard(e);
      Serial.printf("mqtt: card %s: %s\n", queued ? "queued" : "dropped (inbox full)", e.card.title);
    }
  } else {
    int64_t local;
    bool ok = api::localTimeFromJson(json, len, local) && gHooks.setTime;
    if (ok) gHooks.setTime(local);
    Serial.printf("mqtt: time %s\n", ok ? "set" : "rejected");
  }
  free(gPieces);
  gPieces = nullptr;
  gMessageKind = Inbox::None;
}

const char* mqttError(const esp_mqtt_error_codes_t& err) {
  if (err.error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
    switch (err.connect_return_code) {
      case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
      case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
        return "auth";
      default:
        return "refused";
    }
  }
  switch (err.esp_tls_last_esp_err) {
    case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME:
      return "dns";
    case ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST:
      return "tcp";
    case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT:
      return "timeout";
    case ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED:
    case ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED:
      return "tls";
    default:
      return "network";
  }
}

void onMqttEvent(void*, esp_event_base_t, int32_t id, void* data) {
  const esp_mqtt_event_t& ev = *static_cast<esp_mqtt_event_handle_t>(data);
  switch (esp_mqtt_event_id_t(id)) {
    case MQTT_EVENT_CONNECTED:
      gMqtt = Link::Connected;
      gMqttError = "";
      if (gAibuddy) {  // own channel only: the broker refuses any other topic to a device account
        esp_mqtt_client_subscribe(ev.client, gTopicDown, 1);
        esp_mqtt_client_publish(ev.client, gTopicStatus, kAibOnline, 0, 1, 0);
        Serial.printf("mqtt: connected as %s, listening on %s\n", aibuddy::device(), gTopicDown);
        aibuddy::onConnected();
        break;
      }
      esp_mqtt_client_subscribe(ev.client, gTopicCard, 1);
      esp_mqtt_client_subscribe(ev.client, gTopicTime, 1);
      esp_mqtt_client_subscribe(ev.client, gTopicBatteryGet, 0);
      publishStatus(ev.client);
      Serial.printf("mqtt: connected, listening on %s and %s\n", gTopicCard, gTopicTime);
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (gMqtt == Link::Connected) Serial.println("mqtt: disconnected, reconnecting");
      gMqtt = Link::Connecting;
      aibuddy::onDisconnected();
      break;
    case MQTT_EVENT_ERROR:
      if (ev.error_handle) {
        gMqttError = mqttError(*ev.error_handle);
        Serial.printf("mqtt: %s (esp-tls 0x%x, socket errno %d, refused %d)\n", gMqttError.load(),
                      unsigned(ev.error_handle->esp_tls_last_esp_err), ev.error_handle->esp_transport_sock_errno,
                      int(ev.error_handle->connect_return_code));
      }
      break;
    case MQTT_EVENT_DATA:
      onMessage(ev);
      break;
    default:
      break;
  }
}

void startMqtt(const Settings& s) {
  aibuddy::setDevice(s.host[0] ? s.device : "", s.tzMin);
  if (!s.host[0]) return;
  gAibuddy = s.device[0] != '\0';
  const char* base = s.topic[0] ? s.topic : gDefaultTopic;
  snprintf(gTopicCard, sizeof(gTopicCard), "%s/card", base);
  snprintf(gTopicTime, sizeof(gTopicTime), "%s/time", base);
  snprintf(gTopicOutcome, sizeof(gTopicOutcome), "%s/outcome", base);
  snprintf(gTopicStatus, sizeof(gTopicStatus), "%s/status", base);
  snprintf(gTopicBattery, sizeof(gTopicBattery), "%s/battery", base);
  snprintf(gTopicBatteryGet, sizeof(gTopicBatteryGet), "%s/battery/get", base);
  snprintf(gTopicDebug, sizeof(gTopicDebug), "%s/debug", base);
  snprintf(gTopicAsk, sizeof(gTopicAsk), "%s/ask", base);
  if (gAibuddy) {
    snprintf(gTopicDown, sizeof(gTopicDown), "d/%s/dn", s.device);
    snprintf(gTopicUp, sizeof(gTopicUp), "d/%s/up", s.device);
    snprintf(gTopicStatus, sizeof(gTopicStatus), "d/%s/st", s.device);
  }

  for (int i = 0; i < kPems; i++) {  // the client keeps pointers into these until it is destroyed
    free(gPem[i]);
    gPem[i] = loadPem(i);
  }
  esp_mqtt_client_config_t cfg = {};
  cfg.host = s.host;
  cfg.port = s.port ? s.port : (s.tls ? 8883 : 1883);
  cfg.transport = s.tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
  const bool ownCa = s.tls && gPem[int(Pem::Ca)], mutual = s.tls && gPem[int(Pem::ClientCert)] && gPem[int(Pem::ClientKey)];
  if (ownCa) cfg.cert_pem = gPem[int(Pem::Ca)];  // the broker's certificate must come from this CA alone
  else if (s.tls) cfg.crt_bundle_attach = esp_crt_bundle_attach;  // public CAs, as a browser would
  if (mutual) {
    cfg.client_cert_pem = gPem[int(Pem::ClientCert)];
    cfg.client_key_pem = gPem[int(Pem::ClientKey)];
  }
  cfg.client_id = gAibuddy ? s.device : gHostName;  // RabbitMQ names the device's queue after it
  cfg.username = gAibuddy ? s.device : (s.user[0] ? s.user : nullptr);
  cfg.password = gAibuddy ? s.secret : (s.mqttPass[0] ? s.mqttPass : nullptr);
  cfg.lwt_topic = gTopicStatus;
  cfg.lwt_msg = gAibuddy ? kAibOffline : kOffline;
  cfg.lwt_qos = 1;
  cfg.lwt_retain = gAibuddy ? 0 : 1;
  cfg.keepalive = kKeepaliveS;
  // Not pinned (the precompiled IDF has no core option): below the UI task's
  // priority it only takes idle time when it lands on core 1, TLS handshakes included
  cfg.task_prio = 1;

  esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);  // copies the strings
  if (!c) {
    gMqttError = "init";
    return;
  }
  esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, onMqttEvent, nullptr);
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  gClient = c;
  xSemaphoreGive(gClientMutex);
  gMqttError = "";
  gMqtt = Link::Connecting;
  esp_mqtt_client_start(c);
  Serial.printf("mqtt: broker %s:%u%s, %s %s\n", s.host, unsigned(cfg.port),
                mutual ? (ownCa ? " (mutual TLS, own CA)" : " (mutual TLS, public CAs)")
                       : (s.tls ? (ownCa ? " (TLS, own CA)" : " (TLS)") : ""),
                gAibuddy ? "AI Buddy device" : "topic", gAibuddy ? s.device : base);
}

void stopMqtt() {
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  esp_mqtt_client_handle_t c = gClient;
  gClient = nullptr;
  xSemaphoreGive(gClientMutex);
  if (!c) return;
  // A clean disconnect skips the last will: say it ourselves
  if (gMqtt == Link::Connected) {
    esp_mqtt_client_publish(c, gTopicStatus, gAibuddy ? kAibOffline : kOffline, 0, 1, gAibuddy ? 0 : 1);
  }
  aibuddy::onDisconnected();
  esp_mqtt_client_destroy(c);
  for (char*& p : gPem) {
    free(p);
    p = nullptr;
  }
  gMqtt = Link::Off;
  gMqttError = "";
}

// Once the station has joined: ask the servers (SNTP keeps the system time right by itself), then
// lend the wall clock the local time from it at once and every 10 minutes.
void pollTime(uint32_t now, const Settings& s) {
  if (!gNtpStarted) {
    snprintf(gNtpServer, sizeof(gNtpServer), "%s", s.ntp[0] ? s.ntp : kDefaultNtp);
    configTime(0, 0, gNtpServer, "pool.ntp.org", "time.cloudflare.com");  // UTC: the offset is added below
    gNtpStarted = true;
    Serial.printf("ntp: asking %s\n", gNtpServer);
    return;
  }
  const time_t utc = time(nullptr);
  if (utc < kNtpValidAfter) return;
  if (gNtpSynced && now - gNtpPushedMs < kNtpPushMs) return;
  if (gHooks.setTime) gHooks.setTime(int64_t(utc) + int64_t(s.tzMin) * 60);
  if (!gNtpSynced) Serial.printf("ntp: time set (UTC%+d:%02d)\n", s.tzMin / 60, abs(s.tzMin % 60));
  gNtpSynced = true;
  gNtpPushedMs = now;
}

}  // namespace

void poll(uint32_t now) {
  aibuddy::poll(now);
  if (now - gPollMs < kTickMs) return;
  gPollMs = now;
  const Settings& s = gApplied;
  if (gMqttPaused != gMqttPauseApplied) {  // an update asked for the RAM of the MQTT client, or gives it back
    gMqttPauseApplied = gMqttPaused;
    if (gMqttPaused) {
      stopMqtt();
    } else if (gAppliedGeneration) {
      startMqtt(s);
    }
  }
  uint32_t generation = gGeneration;
  if (generation != gAppliedGeneration) {
    Settings next;
    xSemaphoreTake(gMutex, portMAX_DELAY);
    next = gSettings;
    xSemaphoreGive(gMutex);
    bool first = gAppliedGeneration == 0;
    if (first || !sameWifi(next, s)) {
      if (s.ssid[0]) WiFi.disconnect();
      if (s.ssid[0] && !next.ssid[0]) WiFi.enableSTA(false);  // back to the AP alone
      if (!next.ssid[0] && !gApOn) bringUpAp(false);          // the AP is the only way in then
      gWifiReason = 0;
      gNextTryMs = now;
      gBackoffMs = kRetryMinMs;
    }
    const uint32_t pems = gPemChanges;
    if (first || !sameMqtt(next, s) || pems != gAppliedPems) {
      stopMqtt();
      if (!gMqttPaused) startMqtt(next);
      gAppliedPems = pems;
    }
    if (strcmp(next.ntp, s.ntp) != 0) gNtpStarted = false;  // ask the new server
    if (next.tzMin != s.tzMin) gNtpSynced = false;          // and set the clock again at once
    gApplied = next;
    gAppliedGeneration = generation;
  }

  if (!s.ssid[0]) {
    gWifi = Link::Off;
  } else if (WiFi.status() == WL_CONNECTED) {
    if (gWifi != Link::Connected) {
      Serial.printf("wifi: joined \"%s\", settings page at http://%s/\n", s.ssid, WiFi.localIP().toString().c_str());
      gWifiReason = 0;
      gBackoffMs = kRetryMinMs;
    }
    gWifi = Link::Connected;
    pollTime(now, s);
  } else {
    if (gWifi == Link::Connected) {
      Serial.println("wifi: lost the connection");
      gNextTryMs = now;
    }
    if (gWifi != Link::Connecting) gDownSinceMs = now;
    gWifi = Link::Connecting;
    if (!gApOn && now - gDownSinceMs >= kApFallbackMs) {
      Serial.printf("wifi: \"%s\" not joined for %u s, AP on\n", s.ssid, unsigned(kApFallbackMs / 1000));
      bringUpAp(true);
    }
    if (int32_t(now - gNextTryMs) >= 0) {
      if (gWifiReason) {
        Serial.printf("wifi: \"%s\" failed (reason %u), next try in %u s\n", s.ssid, unsigned(gWifiReason.load()),
                      unsigned(gBackoffMs / 1000));
      }
      WiFi.disconnect();
      WiFi.begin(s.ssid, s.wifiPass);  // also turns the station on next to the AP
      gNextTryMs = now + gBackoffMs;
      gBackoffMs = std::min(gBackoffMs * 2, kRetryMaxMs);
    }
  }
}

void begin(const char* name, const char* apPassword, const char* boardName, const Hooks& hooks) {
  gHooks = hooks;
  snprintf(gName, sizeof(gName), "%s", name);
  snprintf(gBoard, sizeof(gBoard), "%s", boardName);
  for (size_t i = 0; i <= strlen(gName); i++) gHostName[i] = char(tolower(gName[i]));
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(gDefaultTopic, sizeof(gDefaultTopic), "blob/%02x%02x", mac[4], mac[5]);

  gMutex = xSemaphoreCreateMutex();
  gClientMutex = xSemaphoreCreateMutex();
  aibuddy::Hooks aib;
  aib.postCard = hooks.postCard;
  aib.needOtp = hooks.needOtp;
  aib.publishUp = publishUp;
  aibuddy::begin(aib);
  load(gSettings);

  WiFi.persistent(false);        // these settings are the one copy; Arduino would keep its own in NVS
  WiFi.setAutoReconnect(false);  // poll() retries, with backoff
  WiFi.setHostname(gHostName);   // before the station starts
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        // Leaving on purpose (every retry starts with a disconnect) is no failure
        uint8_t reason = info.wifi_sta_disconnected.reason;
        if (reason != WIFI_REASON_ASSOC_LEAVE) gWifiReason = reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  snprintf(gApPassword, sizeof(gApPassword), "%s", apPassword);
  if (gSettings.ssid[0]) {
    WiFi.mode(WIFI_STA);  // the AP comes up on request (setAp) or when the home Wi-Fi cannot be joined
  } else {
    bringUpAp(false);  // first setup: the AP is the way in, the station joins once an SSID is saved
  }
  gGeneration = 1;
}

bool apActive() { return gApOn; }

bool apRequired() { return !gApplied.ssid[0]; }

void setAp(bool on) {
  if (on == gApOn) return;
  if (!on && apRequired()) return;
  if (on) {
    bringUpAp(true);
  } else {
    WiFi.softAPdisconnect(true);  // the station keeps running
    gApOn = false;
  }
}

size_t toJson(char* out, size_t size) {
  xSemaphoreTake(gMutex, portMAX_DELAY);
  const Settings s = gSettings;
  xSemaphoreGive(gMutex);

  JsonDocument doc;
  JsonObject w = doc["wifi"].to<JsonObject>();
  Link wifi = gWifi;
  w["ssid"] = s.ssid;
  w["passSet"] = s.wifiPass[0] != '\0';
  w["status"] = kLinkNames[int(wifi)];
  w["error"] = wifi == Link::Connecting ? wifiError(gWifiReason) : "";
  if (wifi == Link::Connected) {
    w["ip"] = WiFi.localIP().toString();
    w["rssi"] = WiFi.RSSI();
  }

  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["host"] = s.host;
  m["port"] = s.port;
  m["tls"] = s.tls;
  m["user"] = s.user;
  m["passSet"] = s.mqttPass[0] != '\0';
  {
    Preferences prefs;  // which TLS material is stored; never the material itself
    const bool open = prefs.begin(kNamespace, true);
    m["ownCa"] = open && prefs.isKey(kPemKeys[int(Pem::Ca)]);
    m["clientCert"] = open && prefs.isKey(kPemKeys[int(Pem::ClientCert)]);
    m["clientKey"] = open && prefs.isKey(kPemKeys[int(Pem::ClientKey)]);
    if (open) prefs.end();
  }
  m["topic"] = s.topic[0] ? s.topic : gDefaultTopic;
  m["status"] = kLinkNames[int(gMqtt.load())];
  m["error"] = gMqttError.load();

  JsonObject t = doc["time"].to<JsonObject>();
  t["server"] = s.ntp[0] ? s.ntp : kDefaultNtp;
  t["tz"] = s.tzMin;
  t["synced"] = gNtpSynced.load();

  JsonObject a = doc["aibuddy"].to<JsonObject>();
  a["device"] = s.device;
  a["secretSet"] = s.secret[0] != '\0';
  a["state"] = aibuddy::state();
  a["user"] = aibuddy::displayName();

  size_t n = serializeJson(doc, out, size);
  return n < size ? n : 0;
}

bool merge(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) return false;
  xSemaphoreTake(gMutex, portMAX_DELAY);
  Settings next = gSettings;
  bool ok = parse(doc.as<JsonObjectConst>(), next) && save(next);
  if (ok) gSettings = next;
  xSemaphoreGive(gMutex);
  if (ok) gGeneration++;
  return ok;
}

void publishOutcome(const engine::Outcome& o) {
  if (aibuddy::onOutcome(o) || gAibuddy) return;  // a device account may not publish anywhere else
  char json[48];
  int n = snprintf(json, sizeof(json), "{\"ref\":%u,\"option\":%d}", unsigned(o.ref), int(o.option));
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  // Queued, not sent here: the MQTT task sends it (QoS 1, kept a while if offline)
  if (gClient) esp_mqtt_client_enqueue(gClient, gTopicOutcome, json, n, 1, 0, true);
  xSemaphoreGive(gClientMutex);
}

void setMqttPaused(bool paused) { gMqttPaused = paused; }

bool mqttDown() { return gMqtt == Link::Off; }

char* copyPem(Pem which) { return loadPem(int(which)); }

bool setPem(Pem which, const char* pem, size_t len) {
  const int i = int(which);
  if (i < 0 || i >= kPems || len > kMaxPem) return false;
  // Only text that looks like PEM goes in, so a wrong paste cannot leave the client without a CA
  if (len && (!strstr(pem, "-----BEGIN ") || !strstr(pem, "-----END "))) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = len ? prefs.putString(kPemKeys[i], String(pem, len)) == len : prefs.remove(kPemKeys[i]) || !prefs.isKey(kPemKeys[i]);
  prefs.end();
  if (ok) {
    gPemChanges++;
    gGeneration++;  // poll() restarts the MQTT client
  }
  return ok;
}

bool publishDebug(const char* text) {
  if (gAibuddy) return false;
  bool sent = false;
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  if (gClient && gMqtt == Link::Connected) {
    sent = esp_mqtt_client_enqueue(gClient, gTopicDebug, text, int(strlen(text)), 1, 0, true) >= 0;
  }
  xSemaphoreGive(gClientMutex);
  return sent;
}

bool publishAsk(const char* json, size_t len) {
  if (gAibuddy) return false;
  bool sent = false;
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  if (gClient && gMqtt == Link::Connected) sent = esp_mqtt_client_enqueue(gClient, gTopicAsk, json, int(len), 1, 0, true) >= 0;
  xSemaphoreGive(gClientMutex);
  return sent;
}

bool publishBattery(int millivolts, int percent) {
  if (gAibuddy) return false;
  char json[40];
  int n = snprintf(json, sizeof(json), "{\"mv\":%d,\"pct\":%d}", millivolts, percent);
  bool sent = false;
  xSemaphoreTake(gClientMutex, portMAX_DELAY);
  // Retained so a late subscriber sees the last reading. Queued like the outcome: the MQTT task sends it
  if (gClient && gMqtt == Link::Connected) sent = esp_mqtt_client_enqueue(gClient, gTopicBattery, json, n, 1, 1, true) >= 0;
  xSemaphoreGive(gClientMutex);
  return sent;
}

}  // namespace platform::net
