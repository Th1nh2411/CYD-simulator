#pragma once

#include "app/engine/events.h"
#include "platform/esp32/config_store.h"

namespace platform::web {

struct Hooks {
  ConfigStore* config = nullptr;
  const ui::CharacterSet* characters = nullptr;  // option lists for /api/meta
  bool (*postCard)(const engine::Event& e) = nullptr;
  void (*preview)(ui::Expression e) = nullptr;
  void (*setTime)(int64_t localSeconds) = nullptr;
  void (*touchCalibration)() = nullptr;  // shows the touch calibration screen
  void (*screen)(int& degrees, bool& mirror) = nullptr;  // rotation (clockwise degrees) and mirror
  bool (*setScreen)(int degrees, bool mirror) = nullptr;  // saves them; the device restarts if they changed
  bool (*ask)(const char* json, size_t len) = nullptr;     // to the brain (MQTT <topic>/ask); false if not sent
  bool (*otp)(const char* code) = nullptr;                 // AI Buddy login OTP; false if no login waits
};

// HTTP API + settings page (ESP-IDF esp_http_server, its own task on core 0):
//   GET  /             settings page
//   GET  /api/meta     expressions, characters and their option lists
//   GET  /api/config   active character's settings: {"character":"slime","slime":{...}}
//   POST /api/config   merge a partial settings document (may switch characters), returns GET's
//   POST /api/save     persist every character's settings and the selection
//   POST /api/reset    active character back to defaults (not saved)
//   POST /api/preset   {"name":"kitty"} active character's preset (not saved)
//   POST /api/preview  {"expr":"happy"} shows an expression for a moment
//   POST /api/time     {"epoch":<unix s>,"offset":<min east of UTC>} sets the clock
//   POST /api/card     {"kind":"notification"|"prompt","title","body","options":[...],"ref","sticky"}
//   POST /api/ask      {"type":"chat","text":"..."} to the device's brain over MQTT (api::isAsk)
//   POST /api/otp      {"code":"123456"} the AI Buddy login OTP (aibuddy.h), as typed on the keypad
//   POST /api/touchcal opens the settings menu on the device (touch calibrations, AP mode)
//   GET  /api/display  {"rotation":<clockwise degrees>,"mirror":bool}
//   POST /api/display  {"rotation":0|90|180|270,"mirror":bool} (either) saved; the device restarts into it
//   GET  /api/net      home Wi-Fi and MQTT settings and state (network.h), no passwords
//   POST /api/net      merge network settings, saved and applied right away; returns GET's
//   GET  /api/ota      state of the over-the-air update (ota.h)
//   POST /api/ota      {"url":"https://..."} starts one from that URL; returns GET's
//   POST /api/tls/ca | /api/tls/cert | /api/tls/key   body = a PEM (the CA of the MQTT broker's
//                      certificate, the device's certificate, its private key; mutual TLS, network.h
//                      setPem), an empty body clears; saved, MQTT restarts; returns GET /api/net's
// At most two connections at once (a browser's extra ones push out the idlest).
// The server costs ~15 KB, so main stops it when idle and starts it again on demand.
bool start(const Hooks& hooks);  // no-op if already running
void stop();
bool running();
// millis() of the last request served (0 before any)
uint32_t lastRequestMs();

}  // namespace platform::web
