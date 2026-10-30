#include "platform/esp32/web_server.h"

#include <ArduinoJson.h>

#include <atomic>
#include <cstring>
#include <memory>

#include "app/ui/expression.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "platform/common/api_json.h"
#include "platform/esp32/network.h"
#include "platform/esp32/ota.h"
#include "platform/esp32/web_page.h"

namespace platform::web {

namespace {

const char* TAG = "web";
constexpr size_t kMaxBody = 4096;
constexpr size_t kJsonMax = 4096;

Hooks gHooks;

esp_err_t sendJson(httpd_req_t* req, const char* json, size_t len) {
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, ssize_t(len));
}

esp_err_t sendOk(httpd_req_t* req, bool ok) {
  return sendJson(req, ok ? "{\"ok\":true}" : "{\"ok\":false}", ok ? 11 : 12);
}

esp_err_t sendConfig(httpd_req_t* req) {
  std::unique_ptr<char[]> out(new char[kJsonMax]);
  size_t n = gHooks.config->toJson(out.get(), kJsonMax);
  return sendJson(req, out.get(), n);
}

// Reads the request body into buf (NUL-terminated)
bool readBody(httpd_req_t* req, std::unique_ptr<char[]>& buf, size_t& len) {
  len = req->content_len;
  if (len > kMaxBody) return false;
  buf.reset(new char[len + 1]);
  size_t got = 0;
  while (got < len) {
    int r = httpd_req_recv(req, buf.get() + got, len - got);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) return false;
    got += size_t(r);
  }
  buf[len] = '\0';
  return true;
}

esp_err_t badRequest(httpd_req_t* req, const char* why) {
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, why);
  return ESP_OK;
}

esp_err_t handleIndex(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleMeta(httpd_req_t* req) {
  std::unique_ptr<char[]> out(new char[kJsonMax]);
  size_t n = api::metaJson(*gHooks.characters, out.get(), kJsonMax);
  return sendJson(req, out.get(), n);
}

esp_err_t handleGetConfig(httpd_req_t* req) { return sendConfig(req); }

esp_err_t handlePostConfig(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  if (!readBody(req, body, len)) return badRequest(req, "body");
  if (!gHooks.config->merge(body.get(), len)) return badRequest(req, "json");
  return sendConfig(req);
}

esp_err_t handleSave(httpd_req_t* req) { return sendOk(req, gHooks.config->save()); }

esp_err_t handlePreset(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  JsonDocument doc;
  if (!readBody(req, body, len) || deserializeJson(doc, body.get(), len)) return badRequest(req, "json");
  if (!gHooks.config->applyPreset(doc["name"] | "")) return badRequest(req, "name");
  return sendConfig(req);
}

esp_err_t handleReset(httpd_req_t* req) {
  gHooks.config->reset();
  return sendConfig(req);
}

esp_err_t handlePreview(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  JsonDocument doc;
  if (!readBody(req, body, len) || deserializeJson(doc, body.get(), len)) return badRequest(req, "json");
  int expr = ui::findExpression(doc["expr"] | "");
  if (expr < 0) return badRequest(req, "expr");
  if (gHooks.preview) gHooks.preview(ui::Expression(expr));
  return sendOk(req, true);
}

esp_err_t handleTime(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  int64_t local;
  if (!readBody(req, body, len) || !api::localTimeFromJson(body.get(), len, local)) return badRequest(req, "time");
  if (gHooks.setTime) gHooks.setTime(local);
  return sendOk(req, true);
}

esp_err_t handleTouchCal(httpd_req_t* req) {
  if (gHooks.touchCalibration) gHooks.touchCalibration();
  return sendOk(req, gHooks.touchCalibration != nullptr);
}

esp_err_t handleCard(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  engine::Event e;
  if (!readBody(req, body, len) || !api::cardFromJson(body.get(), len, e)) return badRequest(req, "card");
  return sendOk(req, gHooks.postCard(e));
}

esp_err_t handleAsk(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  if (!readBody(req, body, len) || !api::isAsk(body.get(), len)) return badRequest(req, "ask");
  return sendOk(req, gHooks.ask && gHooks.ask(body.get(), len));
}

esp_err_t handleOtp(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  JsonDocument doc;
  if (!readBody(req, body, len) || deserializeJson(doc, body.get(), len) || !doc["code"].is<const char*>()) {
    return badRequest(req, "otp");
  }
  return sendOk(req, gHooks.otp && gHooks.otp(doc["code"].as<const char*>()));
}

// restart: the settings were saved and the device restarts into them shortly
esp_err_t sendDisplay(httpd_req_t* req, int degrees, bool mirror, bool restart) {
  char json[72];
  int n = snprintf(json, sizeof(json), "{\"rotation\":%d,\"mirror\":%s,\"restart\":%s}", degrees,
                   mirror ? "true" : "false", restart ? "true" : "false");
  return sendJson(req, json, size_t(n));
}

esp_err_t handleGetDisplay(httpd_req_t* req) {
  int degrees;
  bool mirror;
  gHooks.screen(degrees, mirror);
  return sendDisplay(req, degrees, mirror, false);
}

esp_err_t handlePostDisplay(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  int degrees, nowDegrees;
  bool mirror, nowMirror;
  gHooks.screen(nowDegrees, nowMirror);
  degrees = nowDegrees;
  mirror = nowMirror;
  if (!readBody(req, body, len) || !api::screenFromJson(body.get(), len, degrees, mirror)) {
    return badRequest(req, "json");
  }
  if (!gHooks.setScreen(degrees, mirror)) return badRequest(req, "rotation");
  return sendDisplay(req, degrees, mirror, degrees != nowDegrees || mirror != nowMirror);
}

esp_err_t sendNet(httpd_req_t* req) {
  std::unique_ptr<char[]> out(new char[kJsonMax]);
  size_t n = net::toJson(out.get(), kJsonMax);
  return sendJson(req, out.get(), n);
}

esp_err_t handleGetNet(httpd_req_t* req) { return sendNet(req); }

esp_err_t handlePostNet(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  if (!readBody(req, body, len)) return badRequest(req, "body");
  if (!net::merge(body.get(), len)) return badRequest(req, "net");
  return sendNet(req);
}

esp_err_t sendOta(httpd_req_t* req) {
  char out[320];
  size_t n = ota::toJson(out, sizeof(out));
  return sendJson(req, out, n);
}

esp_err_t handleGetOta(httpd_req_t* req) { return sendOta(req); }

// {"url":"https://..."}: starts the update (it runs on its own task, this answers at once)
esp_err_t handlePostOta(httpd_req_t* req) {
  std::unique_ptr<char[]> body;
  size_t len;
  JsonDocument doc;
  if (!readBody(req, body, len) || deserializeJson(doc, body.get(), len)) return badRequest(req, "json");
  if (!ota::start(doc["url"] | "")) return badRequest(req, "url");
  return sendOta(req);
}

// The body is a PEM for the MQTT client's mutual TLS (net::setPem); an empty body clears it
esp_err_t setPem(httpd_req_t* req, net::Pem which) {
  std::unique_ptr<char[]> body;
  size_t len;
  if (!readBody(req, body, len)) return badRequest(req, "body");
  if (!net::setPem(which, body.get(), len)) return badRequest(req, "pem");
  return sendNet(req);
}

esp_err_t handleTlsCa(httpd_req_t* req) { return setPem(req, net::Pem::Ca); }
esp_err_t handleTlsCert(httpd_req_t* req) { return setPem(req, net::Pem::ClientCert); }
esp_err_t handleTlsKey(httpd_req_t* req) { return setPem(req, net::Pem::ClientKey); }

using Handler = esp_err_t (*)(httpd_req_t*);

httpd_handle_t gServer = nullptr;
std::atomic<uint32_t> gLastRequestMs{0};

// Every route goes through here so the idle timer sees each request; the real
// handler rides in user_ctx.
esp_err_t stamped(httpd_req_t* req) {
  gLastRequestMs = uint32_t(esp_timer_get_time() / 1000);
  return reinterpret_cast<Handler>(req->user_ctx)(req);
}

void route(httpd_handle_t server, const char* uri, httpd_method_t method, Handler handler) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = method;
  u.handler = stamped;
  u.user_ctx = reinterpret_cast<void*>(handler);
  httpd_register_uri_handler(server, &u);
}

}  // namespace

bool start(const Hooks& hooks) {
  if (gServer) return true;
  gHooks = hooks;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.core_id = 0;
  cfg.stack_size = 8192;
  cfg.max_uri_handlers = 24;
  cfg.max_open_sockets = 2;
  cfg.lru_purge_enable = true;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return false;
  }
  gServer = server;
  route(server, "/", HTTP_GET, handleIndex);
  route(server, "/api/meta", HTTP_GET, handleMeta);
  route(server, "/api/config", HTTP_GET, handleGetConfig);
  route(server, "/api/config", HTTP_POST, handlePostConfig);
  route(server, "/api/save", HTTP_POST, handleSave);
  route(server, "/api/preset", HTTP_POST, handlePreset);
  route(server, "/api/reset", HTTP_POST, handleReset);
  route(server, "/api/preview", HTTP_POST, handlePreview);
  route(server, "/api/card", HTTP_POST, handleCard);
  route(server, "/api/ask", HTTP_POST, handleAsk);
  route(server, "/api/otp", HTTP_POST, handleOtp);
  route(server, "/api/time", HTTP_POST, handleTime);
  route(server, "/api/touchcal", HTTP_POST, handleTouchCal);
  route(server, "/api/display", HTTP_GET, handleGetDisplay);
  route(server, "/api/display", HTTP_POST, handlePostDisplay);
  route(server, "/api/net", HTTP_GET, handleGetNet);
  route(server, "/api/net", HTTP_POST, handlePostNet);
  route(server, "/api/ota", HTTP_GET, handleGetOta);
  route(server, "/api/ota", HTTP_POST, handlePostOta);
  route(server, "/api/tls/ca", HTTP_POST, handleTlsCa);
  route(server, "/api/tls/cert", HTTP_POST, handleTlsCert);
  route(server, "/api/tls/key", HTTP_POST, handleTlsKey);
  return true;
}

void stop() {
  if (!gServer) return;
  httpd_stop(gServer);
  gServer = nullptr;
}

bool running() { return gServer != nullptr; }

uint32_t lastRequestMs() { return gLastRequestMs; }

}  // namespace platform::web
