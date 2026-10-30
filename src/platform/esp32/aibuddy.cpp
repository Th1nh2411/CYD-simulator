#include "platform/esp32/aibuddy.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include <cstring>
#include <ctime>

#include "app/net/aibuddy.h"

namespace platform::aibuddy {

namespace {

constexpr const char* kNamespace = "cyd";
constexpr const char* kSessionKey = "aibsess";  // the session token, for the number in kDeviceKey
constexpr const char* kDeviceKey = "aibdev";

Hooks gHooks;
SemaphoreHandle_t gLock = nullptr;  // the client: MQTT task, loop task, whoever submits an OTP

struct Lock {
  Lock() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gLock); }
};

class EspPort : public ::aibuddy::Port {
 public:
  bool publishUp(const char* json, size_t len) override { return gHooks.publishUp && gHooks.publishUp(json, len); }
  void postCard(const engine::Event& e) override {
    if (gHooks.postCard) gHooks.postCard(e);
  }
  void needOtp(const char* deviceNo, const char* reason) override {
    Serial.printf("aibuddy: OTP needed%s%s\n", *reason ? ": " : "", reason);
    if (gHooks.needOtp) gHooks.needOtp(deviceNo, reason);
  }
  void saveSession(const char* deviceNo, const char* token) override {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    if (*token) prefs.putString(kSessionKey, token);
    else prefs.remove(kSessionKey);
    prefs.putString(kDeviceKey, deviceNo);
    prefs.end();
  }
  int64_t utcNow() override { return int64_t(time(nullptr)); }
  // The Wi-Fi the device is on, hashed: the name itself never travels (B4 #6)
  bool wifiHash(char out[65]) override {
    const String ssid = WiFi.SSID();
    if (!ssid.length()) return false;
    uint8_t sum[32];
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256(reinterpret_cast<const uint8_t*>(ssid.c_str()), ssid.length(), sum, 0);
#else
    mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(ssid.c_str()), ssid.length(), sum, 0);
#endif
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", sum[i]);
    return true;
  }
  void randomBytes(uint8_t* out, size_t n) override { esp_fill_random(out, n); }
  void log(const char* line) override { Serial.println(line); }
};

EspPort gPort;
::aibuddy::Client gClient(gPort);

}  // namespace

void begin(const Hooks& hooks) {
  gHooks = hooks;
  gLock = xSemaphoreCreateMutex();
}

void setDevice(const char* deviceNo, int tzMinutes) {
  // The saved session belongs to the number it was given to
  String session;
  Preferences prefs;
  if (*deviceNo && prefs.begin(kNamespace, true)) {
    if (prefs.getString(kDeviceKey, "") == deviceNo) session = prefs.getString(kSessionKey, "");
    prefs.end();
  }
  Lock lock;
  gClient.setDevice(deviceNo, tzMinutes, session.c_str());
}

const char* device() { return gClient.device(); }

void onConnected() {
  Lock lock;
  gClient.onConnected();
}

void onDisconnected() {
  Lock lock;
  gClient.onDisconnected();
}

void onFrame(const char* json, size_t len) {
  Lock lock;
  gClient.onFrame(json, len);
}

bool onOutcome(const engine::Outcome& o) {
  Lock lock;
  return gClient.onOutcome(o);
}

bool submitOtp(const char* code) {
  Lock lock;
  return gClient.submitOtp(code);
}

void poll(uint32_t now) {
  Lock lock;
  gClient.poll(now);
}

const char* state() { return gClient.stateName(); }

const char* displayName() { return gClient.displayName(); }

}  // namespace platform::aibuddy
