// Browser runtime (Emscripten) for the simulator: the same app as the firmware
// drawn into a canvas, so screen work needs no flashing (tools/web/sim.py).
// Single-threaded: one animation-frame loop runs the engine at its 50 Hz tick
// and the UI at the firmware's frame cadence (16 ms moving, 33 ms idle).
// It reproduces what is drawn, not timing or load: measure those on the device.

#include <ArduinoJson.h>
#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/audio/synth.h"
#include "app/engine/engine.h"
#include "app/gfx/compositor.h"
#include "app/net/aibuddy.h"
#include "app/ui/characters.h"
#include "app/ui/otp_screen.h"
#include "app/ui/settings_menu.h"
#include "app/ui/touch_calibration.h"
#include "app/ui/touch_gesture.h"
#include "app/ui/view.h"
#include "hal/board.h"
#include "platform/common/api_json.h"
#include "platform/common/sample_cards.h"

namespace {

// Same values as platform/esp32/main.cpp
constexpr int kStripLines = 24;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kIdleFrameMs = 33;
constexpr uint32_t kEngineMs = 20;
constexpr uint32_t kPreviewMs = 2500;

constexpr size_t kJsonMax = 4096;

hal::Board* gBoard = nullptr;
engine::Engine gEngine;
gfx::Compositor gCompositor;
ui::CharacterSet gCharacters;
ui::View gView;
ui::TouchCalibrationScreen gTouchCalScreen;
ui::SettingsMenu gSettingsMenu;
ui::OtpScreen gOtpScreen;
bool gSimAp = false;  // the menu's AP row has no Wi-Fi behind it here: it only flips

// What a pick in the settings menu does
void applyMenuAction(ui::SettingsMenu::Action action, const hal::TouchCalibration& cal) {
  using Action = ui::SettingsMenu::Action;
  using Kind = ui::TouchCalibrationScreen::Kind;
  switch (action) {
    case Action::TouchCal: gSettingsMenu.close(), gTouchCalScreen.start(cal, Kind::Touch); break;
    case Action::LongPressCal: gSettingsMenu.close(), gTouchCalScreen.start(cal, Kind::LongPress); break;
    case Action::DragCal: gSettingsMenu.close(), gTouchCalScreen.start(cal, Kind::Drag); break;
    case Action::ToggleAp: gSimAp = !gSimAp, gSettingsMenu.setAp(gSimAp, false); break;
    case Action::Close: gSettingsMenu.close(); break;
    case Action::None: break;
  }
}
hal::TouchCalibration gTouchCal;  // board default, or the saved one

// Settings: edited by the settings page between frames and published to the
// characters, picked up at the start of the next frame like the firmware's UI
// task does. One thread, so no lock (the firmware's ConfigStore has one).
int gActive = 0;
int gPreviewRequest = -1;
int gTurns = 0;  // screen rotation in quarter turns, from localStorage at load
bool gMirror = false;  // the panel's business: the canvas shows the picture as drawn

uint32_t gNextEngineMs = 0, gNextFrameMs = 0, gHudMs = 0, gHudFrames = 0;
ui::TouchGesture gTouch(0);  // pointer input has clean press / release: no release debounce
ui::MotionGesture gMotion;
uint32_t gSamples = 0;  // test cards shown by shakes so far
char gReply[kJsonMax];  // body of the last sim_api() answer

uint32_t millis() { return uint32_t(emscripten_get_now()); }

uint32_t randomSeed() { return uint32_t(emscripten_random() * 4294967296.0); }

// Saved settings live in localStorage under "cyd.<key>", standing in for NVS
EM_JS(void, storeItem, (const char* key, const char* value), {
  try {
    localStorage.setItem('cyd.' + UTF8ToString(key), UTF8ToString(value));
  } catch (e) {
  }
});

EM_JS(void, removeItem, (const char* key), {
  try {
    localStorage.removeItem('cyd.' + UTF8ToString(key));
  } catch (e) {
  }
});

EM_JS(char*, loadItem, (const char* key), {
  try {
    const s = localStorage.getItem('cyd.' + UTF8ToString(key));
    if (s) return stringToNewUTF8(s);
  } catch (e) {
  }
  return 0;
});

// Sound: samples rendered ahead of the page's audio clock and queued there
// (index.html starts the AudioContext on the first click, as browsers ask)
EM_JS(double, audioTime, (), { return window.simAudio ? simAudio.time() : -1; });
EM_JS(void, audioQueue, (const int16_t* pcm, int n, double at), {
  if (window.simAudio) simAudio.queue(HEAP16.subarray(pcm >> 1, (pcm >> 1) + n), at);
});

// A character's ask goes to the PC brain's HTTP port (tools/capri/capri_brain.py --http),
// standing in for MQTT; its card comes back through /api/card, and prompt answers go there too
EM_JS(void, brainPost, (const char* path, const char* json), {
  const body = UTF8ToString(json), route = UTF8ToString(path);
  fetch('http://localhost:8773' + route, {method: 'POST', body: body})
      .then(r => r.ok ? r.text() : null)
      .then(card => { if (card) Module.ccall('sim_api', 'number', ['string', 'string', 'string'], ['POST', '/api/card', card]); })
      .catch(() => console.log('brain: not running (python tools/capri/capri_brain.py --http)'));
});

// AI Buddy device (tools/web/sim.py --aibuddy HB-0006): sim.py holds the MQTT link, the page
// relays its frames (index.html), and the firmware's protocol runs here
EM_JS(void, aibuddyUp, (const char* json), {
  fetch('/__aibuddy/up', {method: 'POST', body: UTF8ToString(json)}).catch(() => {});
});
EM_JS(double, utcSeconds, (), { return Date.now() / 1000; });

class WebPort : public aibuddy::Port {
 public:
  bool publishUp(const char* json, size_t len) override {
    if (!linked) return false;
    std::string s(json, len);
    aibuddyUp(s.c_str());
    return true;
  }
  void postCard(const engine::Event& e) override { gEngine.post(engine::Engine::kFromNetwork, e); }
  void needOtp(const char* deviceNo, const char* reason) override;
  void saveSession(const char* deviceNo, const char* token) override {
    std::string key = std::string("aibsess.") + deviceNo;
    if (*token) storeItem(key.c_str(), token);
    else removeItem(key.c_str());
  }
  int64_t utcNow() override { return int64_t(utcSeconds()); }
  bool wifiHash(char out[65]) override {
    snprintf(out, 65, "%s", hash);
    return hash[0] != '\0';
  }
  void randomBytes(uint8_t* out, size_t n) override {
    for (size_t i = 0; i < n; i++) out[i] = uint8_t(emscripten_random() * 256);
  }
  void log(const char* line) override { printf("%s\n", line); }

  bool linked = false;
  char hash[65] = "";
};

WebPort gAibPort;
aibuddy::Client gAibuddy(gAibPort);

// Her voice: the brain speaks the line (Gemini, tools/capri/capri_brain.py --http) and the page plays it
EM_JS(void, brainSpeak, (const char* json), {
  const body = UTF8ToString(json);
  fetch('http://localhost:8773/tts', {method: 'POST', body: body})
      .then(r => r.status === 200 ? r.arrayBuffer() : null)
      .then(wav => {
        Module.ccall('sim_voice', null, ['number'], [wav ? 1 : 0]);
        if (wav && window.simAudio) simAudio.playWav(wav);
      })
      .catch(() => Module.ccall('sim_voice', null, ['number'], [0]));
});

audio::Synth gSynth;
double gAudioAt = 0;  // page audio time the next queued samples start at

void pumpAudio() {
  // Sounds made while the page may not play yet are dropped, not kept to come out late on another screen
  const double t = audioTime();
  audio::Sound s;
  while ((s = gView.takeSound()).cue != audio::Cue::None) {
    if (t >= 0) gSynth.play(s);
  }
  if (t < 0 || !gSynth.active()) return;
  if (gAudioAt < t) gAudioAt = t + 0.03;
  static int16_t pcm[2048];
  while (gAudioAt < t + 0.12) {  // ~120 ms ahead, in 64 ms blocks
    constexpr int kBlock = audio::kSampleRate / 16;
    gSynth.render(pcm, kBlock);
    audioQueue(pcm, kBlock, gAudioAt);
    gAudioAt += double(kBlock) / audio::kSampleRate;
  }
}

// The browser knows the local time, so the clock needs no setting here
EM_JS(int, localMinuteOfDay, (), {
  const d = new Date();
  return d.getHours() * 60 + d.getMinutes();
});

// Same keys as the firmware's NVS: one per character, "active", and the
// slime-only "character" from before characters could be switched
void loadInto(const char* key, ui::Character& c) {
  char* saved = loadItem(key);
  if (!saved) return;
  JsonDocument doc;
  if (!deserializeJson(doc, saved) && doc.is<JsonObject>()) c.mergeSettings(doc.as<JsonObjectConst>());
  free(saved);
}

void loadSettings() {
  loadInto("character", gCharacters[0]);
  for (int i = 0; i < gCharacters.count(); i++) loadInto(gCharacters[i].name(), gCharacters[i]);
  // Unlike the device, the page always opens on the slime: a browser plays no sound before a click,
  // so a character with an entrance (Capri's greeting) is picked by one and is heard
  for (int i = 0; i < gCharacters.count(); i++) gCharacters[i].publishSettings();
}

bool saveSettings() {
  char json[kJsonMax];
  for (int i = 0; i < gCharacters.count(); i++) {
    JsonDocument doc;
    gCharacters[i].settingsToJson(doc.to<JsonObject>());
    size_t n = serializeJson(doc, json, sizeof(json));
    if (n >= sizeof(json)) return false;
    storeItem(gCharacters[i].name(), json);
  }
  storeItem("active", gCharacters[gActive].name());
  removeItem("character");
  return true;
}

void WebPort::needOtp(const char* deviceNo, const char* reason) {
  printf("aibuddy: OTP needed for %s%s%s (keypad, or the settings page)\n", deviceNo, *reason ? ": " : "", reason);
  gOtpScreen.open(deviceNo, aibuddy::reasonText(reason));
}

void uiFrame(uint32_t now) {
  gView.syncCharacter(gActive);
  gView.setClock(localMinuteOfDay());
  if (gPreviewRequest >= 0) {
    gView.previewExpression(ui::Expression(gPreviewRequest), now + kPreviewMs);
    gPreviewRequest = -1;
  }

  hal::TouchPoint tp;
  const bool touching = gBoard->touch->read(tp);
  hal::ImuSample m;
  if (gBoard->imu && gBoard->imu->read(m)) {
    const ui::MotionGesture::Kind kind = gMotion.update(m, now);
    gView.setTilt(gMotion.tiltX(), gMotion.tiltY());
    engine::Event e;
    if (!gTouchCalScreen.active() && !gSettingsMenu.active()) {
      if (gView.handleMotion(kind, e)) gEngine.post(engine::Engine::kFromUi, e);
      else if (kind == ui::MotionGesture::Kind::Shake && !gView.showsCard())
        gEngine.post(engine::Engine::kFromSystem, platform::sampleCard(gSamples++));
    }
  }
  const ui::TouchGesture::Event g = gTouch.update(touching, tp.x, tp.y, now);
  if (gTouchCalScreen.active()) {
    gTouchCalScreen.onTouch(touching, tp.x, tp.y, gTouch, g, now);
  } else if (g.kind != ui::TouchGesture::Kind::None) {
    int x = g.x, y = g.y;
    ui::applyTouchCalibration(gTouchCal, gBoard->display->width(), gBoard->display->height(), x, y);
    if (gSettingsMenu.active()) {
      if (g.kind == ui::TouchGesture::Kind::Tap) applyMenuAction(gSettingsMenu.onTap(x, y), gTouchCal);
    } else if (gOtpScreen.active()) {
      if (g.kind == ui::TouchGesture::Kind::Tap && gOtpScreen.onTap(x, y) == ui::OtpScreen::Action::Submit) {
        gAibuddy.submitOtp(gOtpScreen.code());
      }
    } else {
      engine::Event e;
      if (gView.handleGesture(g.kind, x, y, now, e)) gEngine.post(engine::Engine::kFromUi, e);
    }
  }
  if (gTouchCalScreen.state() == ui::TouchCalibrationScreen::State::Saved) {
    gTouchCal = gTouchCalScreen.result();
    gTouch.tune(gTouchCal);
    char json[128] = "";
    const int w = gBoard->display->width(), h = gBoard->display->height();
    const hal::TouchCalibration board =
        ui::rotateTouchCalibration(gMirror ? ui::mirrorTouchCalibration(gTouchCal, w) : gTouchCal, 4 - gTurns, w, h);
    if (ui::touchCalibrationToJson(board, json, sizeof(json))) storeItem("touchcal", json);
    printf("touch calibration saved: %s\n", json);
  }
  if (gTouchCalScreen.state() == ui::TouchCalibrationScreen::State::Saved ||
      gTouchCalScreen.state() == ui::TouchCalibrationScreen::State::Cancelled) {
    gTouchCalScreen.finish();
    gSettingsMenu.open(true);  // back where it was picked
  }

  gEngine.pollState();
  if (!gSettingsMenu.active() && !gTouchCalScreen.active() && !gOtpScreen.active()) gView.update(gEngine.state(), now);
  char ask[128];
  if (gView.takeAsk(ask, sizeof(ask))) {
    printf("ask: %s\n", ask);
    brainPost("/ask", ask);
  }
  char speech[256];
  if (gView.takeSpeech(speech, sizeof(speech))) brainSpeak(speech);
  if (gCompositor.render()) gHudFrames++;

  if (now - gHudMs >= 1000) {
    char hud[24];
    snprintf(hud, sizeof(hud), "%ufps sim", unsigned(gHudFrames));
    gView.setDebugText(hud);
    gHudMs = now;
    gHudFrames = 0;
  }
}

void loop() {
  uint32_t now = millis();

  // After a hidden tab (no animation frames) jump ahead instead of catching up
  if (int32_t(now - gNextEngineMs) > 1000) gNextEngineMs = now;
  while (int32_t(now - gNextEngineMs) >= 0) {
    gEngine.tick(gNextEngineMs);
    gNextEngineMs += kEngineMs;
  }

  engine::Outcome o;
  while (gEngine.pollOutcome(o)) {
    if (o.option >= 0) printf("prompt %u answered: option %d\n", unsigned(o.ref), o.option);
    else printf("prompt %u expired\n", unsigned(o.ref));
    if (gAibuddy.onOutcome(o)) continue;
    char json[48];
    snprintf(json, sizeof(json), "{\"ref\":%u,\"option\":%d}", unsigned(o.ref), int(o.option));
    if (o.ref) brainPost("/outcome", json);
  }
  gAibuddy.poll(now);
  pumpAudio();

  if (int32_t(now - gNextFrameMs) < 0) return;
  uiFrame(now);

  // Fixed schedule like vTaskDelayUntil, so the average rate holds on any monitor refresh rate
  uint32_t period = gView.wantsFullFrameRate(now) || gTouch.pressed() ? kFrameMs : kIdleFrameMs;
  gNextFrameMs += period;
  if (int32_t(gNextFrameMs - now) <= 0) gNextFrameMs = now + period;  // fell behind: no burst
}

int replyConfig() {
  JsonDocument doc;
  gCharacters.toJson(gActive, doc.to<JsonObject>());
  size_t n = serializeJson(doc, gReply, sizeof(gReply));
  return n < sizeof(gReply) ? 200 : 500;
}

int replyDisplay(int degrees, bool mirror, bool restart) {
  snprintf(gReply, sizeof(gReply), "{\"rotation\":%d,\"mirror\":%s,\"restart\":%s}", degrees,
           mirror ? "true" : "false", restart ? "true" : "false");
  return 200;
}

// The simulator restarts the way the device does after a rotation change
EM_JS(void, reloadSoon, (), { setTimeout(() => location.reload(), 300); });

int replyOk(bool ok) {
  strcpy(gReply, ok ? "{\"ok\":true}" : "{\"ok\":false}");
  return ok ? 200 : 400;
}

void publish(uint32_t touched) {
  for (int i = 0; i < gCharacters.count(); i++) {
    if (touched & (1u << i)) gCharacters[i].publishSettings();
  }
}

// Same routes and payloads as platform/esp32/web_server.cpp
int handleApi(const char* method, const char* path, const char* body) {
  size_t len = strlen(body);
  if (strcmp(method, "POST") != 0) {
    if (strcmp(path, "/api/meta") == 0) return platform::api::metaJson(gCharacters, gReply, sizeof(gReply)) ? 200 : 500;
    if (strcmp(path, "/api/config") == 0) return replyConfig();
    if (strcmp(path, "/api/display") == 0) return replyDisplay(gTurns * 90, gMirror, false);
    return 404;
  }
  if (strcmp(path, "/api/save") == 0) return replyOk(saveSettings());
  if (strcmp(path, "/api/display") == 0) {  // saved, then the page reloads as the device restarts
    int degrees = gTurns * 90;
    bool mirror = gMirror;
    if (!platform::api::screenFromJson(body, len, degrees, mirror) || degrees < 0 || degrees >= 360 || degrees % 90) {
      return replyOk(false);
    }
    const bool changed = degrees / 90 != gTurns || mirror != gMirror;
    if (changed) {
      char turns[2] = {char('0' + degrees / 90), 0};
      storeItem("rotation", turns);
      storeItem("mirror", mirror ? "1" : "0");
      reloadSoon();
    }
    return replyDisplay(degrees, mirror, changed);
  }
  if (strcmp(path, "/api/touchcal") == 0) {
    gSettingsMenu.open(true);
    return replyOk(true);
  }
  if (strcmp(path, "/api/time") == 0) {  // the page lends its time; the browser's own is used anyway
    int64_t local;
    return replyOk(platform::api::localTimeFromJson(body, len, local));
  }
  if (strcmp(path, "/api/reset") == 0) {
    gCharacters[gActive].resetSettings();
    publish(1u << gActive);
    return replyConfig();
  }
  if (strcmp(path, "/api/otp") == 0) {
    JsonDocument doc;
    if (deserializeJson(doc, body, len)) return replyOk(false);
    const bool sent = gAibuddy.submitOtp(doc["code"] | "");
    if (sent) gOtpScreen.close();
    return replyOk(sent);
  }
  if (strcmp(path, "/api/ask") == 0) {
    if (!platform::api::isAsk(body, len)) return replyOk(false);
    brainPost("/ask", body);
    return replyOk(true);
  }
  if (strcmp(path, "/api/card") == 0) {
    engine::Event e;
    return replyOk(platform::api::cardFromJson(body, len, e) && gEngine.post(engine::Engine::kFromNetwork, e));
  }

  JsonDocument doc;
  if (deserializeJson(doc, body, len) || !doc.is<JsonObject>()) return replyOk(false);
  if (strcmp(path, "/api/config") == 0) {
    uint32_t touched = 0;
    if (!gCharacters.merge(doc.as<JsonObjectConst>(), gActive, touched)) return replyOk(false);
    publish(touched);
    return replyConfig();
  }
  if (strcmp(path, "/api/preset") == 0) {
    if (!gCharacters[gActive].applyPreset(doc["name"] | "")) return replyOk(false);
    publish(1u << gActive);
    return replyConfig();
  }
  if (strcmp(path, "/api/preview") == 0) {
    int expr = ui::findExpression(doc["expr"] | "");
    if (expr >= 0) gPreviewRequest = expr;
    return replyOk(expr >= 0);
  }
  return 404;
}

}  // namespace

// Called by the settings page's fetch shim (see index.html): returns the HTTP
// status; the body is in sim_reply()
extern "C" EMSCRIPTEN_KEEPALIVE int sim_api(const char* method, const char* path, const char* body) {
  gReply[0] = '\0';
  return handleApi(method, path, body);
}

extern "C" EMSCRIPTEN_KEEPALIVE const char* sim_reply() { return gReply; }

// Whether the brain voiced the last line: without it she blips as she types
extern "C" EMSCRIPTEN_KEEPALIVE void sim_voice(int ok) { gView.setVoiceReady(ok != 0); }

// The page's side of sim.py's AI Buddy bridge (index.html)
extern "C" EMSCRIPTEN_KEEPALIVE void sim_aibuddy_begin(const char* device, const char* wifiHash, int tzMinutes) {
  snprintf(gAibPort.hash, sizeof(gAibPort.hash), "%s", wifiHash);
  const std::string key = std::string("aibsess.") + device;
  char* session = loadItem(key.c_str());
  gAibuddy.setDevice(device, tzMinutes, session ? session : "");
  free(session);
  printf("aibuddy: simulating device %s\n", device);
}

extern "C" EMSCRIPTEN_KEEPALIVE void sim_aibuddy_link(int up) {
  gAibPort.linked = up != 0;
  if (up) gAibuddy.onConnected();
  else gAibuddy.onDisconnected();
}

extern "C" EMSCRIPTEN_KEEPALIVE void sim_aibuddy_frame(const char* json) { gAibuddy.onFrame(json, strlen(json)); }

int main() {
  if (char* saved = loadItem("rotation")) {
    gTurns = (saved[0] - '0') & 3;
    free(saved);
  }
  if (char* saved = loadItem("mirror")) {
    gMirror = saved[0] == '1';
    free(saved);
  }
  gBoard = &hal::initBoard(gTurns, gMirror);
  hal::Display* display = gBoard->display;

  size_t stripPixels = size_t(display->width()) * kStripLines;
  gfx::Color* buf0 = display->allocBuffer(stripPixels);
  gfx::Color* buf1 = display->allocBuffer(stripPixels);
  if (!gCompositor.begin(display, buf0, buf1, stripPixels)) {
    printf("compositor init failed\n");
    return 1;
  }

  loadSettings();
  uint32_t now = millis();
  gView.begin(gCompositor, display->width(), display->height(), gCharacters, gActive, randomSeed());
  gTouchCalScreen.attach(gCompositor, ui::kTouchCalibrationZ, display->width(), display->height());
  gSettingsMenu.attach(gCompositor, ui::kSettingsMenuZ, display->width(), display->height());
  gOtpScreen.attach(gCompositor, ui::kOtpScreenZ, display->width(), display->height());
  gSettingsMenu.setApInfo("CYD-Blob-SIM", "blob1234", "http://192.168.4.1/");
  hal::TouchCalibration board = gBoard->touchCalibration;  // stored in the board's own frame
  if (char* saved = loadItem("touchcal")) {
    ui::touchCalibrationFromJson(saved, board);
    free(saved);
  }
  const bool odd = gTurns & 1;
  gTouchCal = ui::rotateTouchCalibration(board, gTurns, odd ? display->height() : display->width(),
                                         odd ? display->width() : display->height());
  if (gMirror) gTouchCal = ui::mirrorTouchCalibration(gTouchCal, display->width());
  gTouch.tune(gTouchCal);
  gEngine.seed(randomSeed());
  gEngine.tick(now);
  gEngine.pollState();
  gView.update(gEngine.state(), now);
  gCompositor.render();

  gNextEngineMs = now + kEngineMs;
  gNextFrameMs = now + kFrameMs;
  gHudMs = now;
  printf("=== %s | %dx%d ===\n", gBoard->name, display->width(), display->height());

  emscripten_set_main_loop(loop, 0, false);  // every animation frame; loop() keeps the cadence
  return 0;
}
