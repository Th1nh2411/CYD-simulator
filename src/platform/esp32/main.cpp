// ESP32 runtime: wires the board to the portable app and schedules it.
//   core 0: engine task (50 Hz), Wi-Fi, web server, audio task (boards with a speaker)
//   core 1: UI task (touch -> semantic events, view update, render, ~60 fps)
//   either: MQTT client task (lowest priority, the IDF does not pin it)
//   loop(): serial commands, dev button, prompt outcomes, stats, network upkeep

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include <atomic>
#include <cstring>

#include "app/audio/synth.h"
#include "app/core/battery.h"
#include "app/core/spsc_queue.h"
#include "app/core/utf8.h"
#include "app/engine/engine.h"
#include "app/net/aibuddy.h"
#include "app/gfx/compositor.h"
#include "app/gfx/gfx_bench.h"
#include "app/ui/otp_screen.h"
#include "app/ui/settings_menu.h"
#include "app/ui/touch_calibration.h"
#include "app/ui/touch_gesture.h"
#include "app/ui/view.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "hal/board.h"
#include "platform/common/api_json.h"
#include "platform/common/sample_cards.h"
#include "platform/esp32/aibuddy.h"
#include "platform/esp32/config_store.h"
#include "platform/esp32/network.h"
#include "platform/esp32/ota.h"
#include "platform/esp32/perf_probe.h"
#include "platform/esp32/web_server.h"

#ifndef PERF_PROBE
#define PERF_PROBE 0
#endif
#ifndef HEAP_PROBE
#define HEAP_PROBE 0
#endif
#ifndef WEB_PROBE
#define WEB_PROBE 0
#endif

#if HEAP_PROBE
// Internal free heap after each boot step: how much each subsystem costs
#define HEAP_MARK(what)                                                                                \
  Serial.printf("heap-mark %-14s free %u largest %u\n", what,                                          \
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),                                \
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)))
#else
#define HEAP_MARK(what) ((void)0)
#endif

namespace {

constexpr uint32_t kFrameMs = 16;      // ~60 fps while moving
constexpr uint32_t kIdleFrameMs = 33;  // ~30 fps for idle loops
constexpr uint32_t kEngineMs = 20;
constexpr uint32_t kReleaseMs = 50;  // no contact this long is a lift (mid-press gaps measured up to 20 ms)
constexpr uint32_t kPreviewMs = 2500;
constexpr const char* kApPassword = "blob1234";

// Measures time spent inside flush(): waiting for the previous DMA transfer
// plus command overhead. Render time minus this is pure drawing (CPU) time.
class TimedDisplay : public hal::Display {
 public:
  explicit TimedDisplay(hal::Display* d) : d_(d) {}
  int width() const override { return d_->width(); }
  int height() const override { return d_->height(); }
  gfx::Color* allocBuffer(size_t pixels) override { return d_->allocBuffer(pixels); }
  void flush(const gfx::Rect& a, const gfx::Color* px) override {
    uint32_t t0 = micros();
    d_->flush(a, px);
    flushUs += micros() - t0;
  }
  void present() override {
    uint32_t t0 = micros();
    d_->present();
    flushUs += micros() - t0;
  }
  void waitIdle() override { d_->waitIdle(); }
  bool prefersColumnStrips() const override { return d_->prefersColumnStrips(); }
  bool prefersFullRows() const override { return d_->prefersFullRows(); }
  uint32_t flushUs = 0;  // UI task only

 private:
  hal::Display* d_;
};

hal::Board* gBoard = nullptr;
TimedDisplay* gDisplay = nullptr;
engine::Engine gEngine;
gfx::Compositor gCompositor;
ui::CharacterSet gCharacters;
ui::View gView;
ui::TouchCalibrationScreen gTouchCalScreen;  // UI task
ui::SettingsMenu gSettingsMenu;               // UI task
ui::OtpScreen gOtpScreen;                     // UI task
hal::TouchCalibration gTouchCal;             // UI task after setup: board default or the saved one, screen frame
platform::ConfigStore gConfig;
TaskHandle_t gUiTask = nullptr, gEngineTask = nullptr;
core::SpscQueue<audio::Sound, 8> gSounds;  // UI task -> audio task
SemaphoreHandle_t gNetworkInbox = nullptr;  // see postFromNetwork()
char gApName[24] = "";

struct Stats {
  std::atomic<uint32_t> frames{0}, renderUs{0}, flushUs{0}, pixels{0}, engineUs{0}, engineTicks{0};
} gStats;

// Requests handed to the UI task
std::atomic<uint32_t> gStressUntilMs{0};  // full-screen redraw every frame until then
std::atomic<bool> gBenchRequested{false};
std::atomic<int> gPreviewRequest{-1};
std::atomic<bool> gMenuOpen{false}, gTouchCalButton{false};
std::atomic<uint8_t> gMenuButton{0};         // BOOT in the settings menu: 1 a press (next), 2 held (pick)
std::atomic<bool> gTouchCalRunning{false};   // mirrors for the loop task (BOOT cancels the calibration)
std::atomic<bool> gMenuActive{false};        //   or drives the menu
std::atomic<bool> gApRequest{false};         // the menu's AP row was picked: the loop task toggles it
std::atomic<bool> gApOn{false}, gApLocked{false};  // the loop task's view of the AP, for the menu
std::atomic<bool> gSampleCardRequest{false};  // the next test card (BOOT, a shake with no card up)
// AI Buddy asks for the login OTP (MQTT task): the UI task opens the keypad with these
std::atomic<bool> gOtpRequest{false};
char gOtpDevice[16] = "", gOtpReason[24] = "";

// Latest per-core CPU load (%), sampled once per second by the UI task for the HUD
std::atomic<float> gLoad0{0}, gLoad1{0};

// Latest motion reading (UI task writes, serial "imu" prints it a while)
hal::ImuSample gImu;
float gImuPeakJolt = 0;  // largest jolt since the last print (g)
std::atomic<uint32_t> gImuPrintUntilMs{0};

// Wall clock. No battery-backed RTC: SNTP sets it once the home Wi-Fi is joined (network.cpp),
// and until then the settings page (or serial) lends the board its local time; kept as the
// time-of-day offset from uptime, -1 until set. Any thread.
std::atomic<int32_t> gClockOffsetS{-1};

// Screen rotation in quarter turns and mirror, fixed from boot: every layer and
// buffer is sized at start, so a change is saved and the device restarts at this time
int gTurns = 0;
bool gMirror = false;
std::atomic<uint32_t> gRestartAtMs{0};  // 0 = no restart pending

// Touch calibration is stored in the board's own frame: turned, then mirrored
hal::TouchCalibration toScreenFrame(const hal::TouchCalibration& board) {
  const bool odd = gTurns & 1;
  hal::TouchCalibration cal = ui::rotateTouchCalibration(
      board, gTurns, odd ? gDisplay->height() : gDisplay->width(), odd ? gDisplay->width() : gDisplay->height());
  return gMirror ? ui::mirrorTouchCalibration(cal, gDisplay->width()) : cal;
}

hal::TouchCalibration toBoardFrame(const hal::TouchCalibration& screen) {
  const hal::TouchCalibration cal = gMirror ? ui::mirrorTouchCalibration(screen, gDisplay->width()) : screen;
  return ui::rotateTouchCalibration(cal, 4 - gTurns, gDisplay->width(), gDisplay->height());
}

// Web server task or loop: clockwise degrees (a multiple of 90) and mirror
bool setScreen(int degrees, bool mirror) {
  if (degrees < 0 || degrees >= 360 || degrees % 90 != 0) return false;
  if (degrees / 90 == gTurns && mirror == gMirror) return true;
  if (!platform::ConfigStore::saveScreen(degrees / 90, mirror)) return false;
  gRestartAtMs = (millis() + 500) | 1;  // after the reply has gone out
  return true;
}

void screen(int& degrees, bool& mirror) {
  degrees = gTurns * 90;
  mirror = gMirror;
}

int64_t uptimeS() { return esp_timer_get_time() / 1000000; }

void setLocalTime(int64_t localSeconds) {
  gClockOffsetS = int32_t(((localSeconds - uptimeS()) % 86400 + 86400) % 86400);
}

int minuteOfDay() {
  int32_t offset = gClockOffsetS;
  return offset < 0 ? -1 : int((uptimeS() % 86400 + offset) % 86400 / 60);
}

uint32_t clockUs() { return uint32_t(micros()); }

// Runs on the UI task so nothing else draws meanwhile
void runBench() {
  constexpr int kSide = gfx::bench::kSide;
  // Internal RAM: boards with PSRAM would hand out a large malloc from there
  auto* buf = static_cast<gfx::Color*>(heap_caps_malloc(kSide * kSide * sizeof(gfx::Color), MALLOC_CAP_INTERNAL));
  if (!buf) {
    Serial.println("bench: no memory");
    return;
  }
  gfx::bench::Result r[12];
  int n = gfx::bench::run(buf, clockUs, r, 12);
  free(buf);

  uint32_t mhz = getCpuFrequencyMhz();
  Serial.printf("--- draw cost (CPU only, %u MHz) ---\n", unsigned(mhz));
  for (int i = 0; i < n; i++) {
    Serial.printf("%-22s %7.2f Mpx/s %6.1f cycles/px\n", r[i].name,
                  float(r[i].pixels) / float(r[i].us), float(r[i].us) * mhz / float(r[i].pixels));
  }

  const esp_partition_t* fs =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  Serial.println("--- memory ---");
  Serial.printf("internal heap free %u, largest block %u, min ever %u\n",
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                unsigned(ESP.getMinFreeHeap()));
  Serial.printf("DMA-capable free %u, largest block %u\n",
                unsigned(heap_caps_get_free_size(MALLOC_CAP_DMA)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
  Serial.printf("stack headroom: ui %u, engine %u bytes\n",
                unsigned(uxTaskGetStackHighWaterMark(gUiTask)),
                unsigned(uxTaskGetStackHighWaterMark(gEngineTask)));
  Serial.printf("flash: app %u of %u slot, fs partition %u, chip %u\n", unsigned(ESP.getSketchSize()),
                unsigned(ESP.getFreeSketchSpace()), unsigned(fs ? fs->size : 0),
                unsigned(ESP.getFlashChipSize()));
}

// Plays the active character's sounds: synthesized in blocks of 16 ms, each write
// waits for room in the driver's DMA queue, so it runs at the speaker's pace
void audioTask(void*) {
  audio::Synth synth;
  int16_t block[256];
  for (;;) {
    audio::Sound s;
    while (gSounds.pop(s)) synth.play(s);
    if (synth.active()) {
      synth.render(block, 256);
      gBoard->speaker->write(block, 256);
    } else {
      gBoard->speaker->idle();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void engineTask(void*) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    uint32_t t0 = micros();
    gEngine.tick(millis());
    gStats.engineUs += micros() - t0;
    gStats.engineTicks++;
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kEngineMs));
  }
}

// UI task: what a pick in the settings menu does
void applyMenuAction(ui::SettingsMenu::Action action) {
  using Action = ui::SettingsMenu::Action;
  using Kind = ui::TouchCalibrationScreen::Kind;
  switch (action) {
    case Action::TouchCal: gSettingsMenu.close(), gTouchCalScreen.start(gTouchCal, Kind::Touch); break;
    case Action::LongPressCal: gSettingsMenu.close(), gTouchCalScreen.start(gTouchCal, Kind::LongPress); break;
    case Action::DragCal: gSettingsMenu.close(), gTouchCalScreen.start(gTouchCal, Kind::Drag); break;
    case Action::ToggleAp: gApRequest = true; break;  // the loop task owns Wi-Fi
    case Action::Close: gSettingsMenu.close(); break;
    case Action::None: break;
  }
}

void uiTask(void*) {
  ui::TouchGesture touch(kReleaseMs);
  touch.tune(gTouchCal);
  ui::MotionGesture motion;
  uint32_t hudMs = millis(), hudFrames = 0, hudUs = 0;
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    uint32_t now = millis();

    gView.syncCharacter(gConfig.active());
    gView.setClock(minuteOfDay());
    int preview = gPreviewRequest.exchange(-1);
    if (preview >= 0) gView.previewExpression(ui::Expression(preview), now + kPreviewMs);
    if (gMenuOpen.exchange(false) && !gTouchCalScreen.active()) gSettingsMenu.open(gBoard->touch != nullptr);
    if (gOtpRequest.exchange(false) && gBoard->touch) gOtpScreen.open(gOtpDevice, aibuddy::reasonText(gOtpReason));
    if (gTouchCalButton.exchange(false)) gTouchCalScreen.onButton();
    gSettingsMenu.setAp(gApOn, gApLocked);
    if (const uint8_t button = gMenuButton.exchange(0)) applyMenuAction(gSettingsMenu.onButton(button == 2));

    hal::TouchPoint tp;
    const bool touching = gBoard->touch && gBoard->touch->read(tp);
    hal::ImuSample m;
    if (gBoard->imu && gBoard->imu->read(m)) {
      gImu = m;
      const ui::MotionGesture::Kind kind = motion.update(m, now);
      gImuPeakJolt = std::max(gImuPeakJolt, motion.jolt());
      if (kind != ui::MotionGesture::Kind::None) {
        static const char* const kNames[] = {"none", "shake", "hold left", "hold right"};
        Serial.printf("motion: %s\n", kNames[int(kind)]);
      }
      gView.setTilt(motion.tiltX(), motion.tiltY());
      engine::Event e;
      if (!gTouchCalScreen.active() && !gSettingsMenu.active()) {
        if (gView.handleMotion(kind, e)) gEngine.post(engine::Engine::kFromUi, e);
        else if (kind == ui::MotionGesture::Kind::Shake && !gView.showsCard()) gSampleCardRequest = true;
      }
    }
    const ui::TouchGesture::Event g = touch.update(touching, tp.x, tp.y, now);
    if (gTouchCalScreen.active()) {
      gTouchCalScreen.onTouch(touching, tp.x, tp.y, touch, g, now);  // uncorrected points
    } else if (g.kind != ui::TouchGesture::Kind::None) {
      int x = g.x, y = g.y;
      ui::applyTouchCalibration(gTouchCal, gDisplay->width(), gDisplay->height(), x, y);
      if (gSettingsMenu.active()) {
        if (g.kind == ui::TouchGesture::Kind::Tap) applyMenuAction(gSettingsMenu.onTap(x, y));
      } else if (gOtpScreen.active()) {
        if (g.kind == ui::TouchGesture::Kind::Tap && gOtpScreen.onTap(x, y) == ui::OtpScreen::Action::Submit) {
          Serial.println(platform::aibuddy::submitOtp(gOtpScreen.code()) ? "aibuddy: OTP sent" : "aibuddy: no login waits for an OTP");
        }
      } else {
        engine::Event e;
        if (gView.handleGesture(g.kind, x, y, now, e)) gEngine.post(engine::Engine::kFromUi, e);
      }
    }
    auto calState = gTouchCalScreen.state();
    if (calState == ui::TouchCalibrationScreen::State::Saved) {
      gTouchCal = gTouchCalScreen.result();
      touch.tune(gTouchCal);
      bool saved = gConfig.saveTouchCalibration(toBoardFrame(gTouchCal));
      Serial.printf("touch calibration %s: x = %.4f*X %+.2f  y = %.4f*Y %+.2f  drag > %d px  long press %d ms\n",
                    saved ? "saved" : "NOT saved", gTouchCal.ax, gTouchCal.bx, gTouchCal.ay, gTouchCal.by,
                    gTouchCal.slopPx, gTouchCal.longPressMs);
    }
    if (calState == ui::TouchCalibrationScreen::State::Saved ||
        calState == ui::TouchCalibrationScreen::State::Cancelled) {
      gTouchCalScreen.finish();
      gSettingsMenu.open(gBoard->touch != nullptr);  // back where it was picked
    }
    gTouchCalRunning = gTouchCalScreen.active();
    gMenuActive = gSettingsMenu.active();

    gEngine.pollState();
    // The menu and the calibration screens cover everything: animating behind them only costs draw time
    if (!gSettingsMenu.active() && !gTouchCalScreen.active() && !gOtpScreen.active()) gView.update(gEngine.state(), now);
    // What the character sends out: sounds to the audio task, asks to the brain over MQTT
    for (audio::Sound s = gView.takeSound(); s.cue != audio::Cue::None; s = gView.takeSound()) {
      if (gBoard->speaker) gSounds.push(s);
    }
    char ask[128];
    if (gView.takeAsk(ask, sizeof(ask))) {
      const bool sent = platform::net::publishAsk(ask, strlen(ask));
      Serial.printf("ask %s: %s\n", sent ? "sent" : "not sent (MQTT down)", ask);
    }
    if (int32_t(gStressUntilMs.load() - now) > 0) gCompositor.invalidateAll();
    if (gBenchRequested.exchange(false)) runBench();

    gDisplay->flushUs = 0;
    uint32_t t0 = micros();
    uint32_t px = gCompositor.render();
    uint32_t us = micros() - t0;
    if (px) {
      gStats.frames++;
      gStats.renderUs += us;
      gStats.flushUs += gDisplay->flushUs;
      gStats.pixels += px;
      hudFrames++;
      hudUs += us - gDisplay->flushUs;  // drawing only, without waiting for SPI
    }

    if (now - hudMs >= 1000) {
      float drawMs = hudFrames ? hudUs / 1000.0f / hudFrames : 0.0f;
      char hud[48];
#if PERF_PROBE
      float load[2];
      perf::sample(load);
      gLoad0 = load[0];
      gLoad1 = load[1];
      snprintf(hud, sizeof(hud), "%ufps cpu %.0f%%/%.0f%% draw %.1fms", unsigned(hudFrames), load[0],
               load[1], drawMs);
#else
      snprintf(hud, sizeof(hud), "%ufps draw %.1fms", unsigned(hudFrames), drawMs);
#endif
      gView.setDebugText(hud);
      hudMs = now;
      hudFrames = hudUs = 0;
    }

    // Full rate while things move fast; idle loops (breathing, flapping) at half rate
    bool fast = gView.wantsFullFrameRate(now) || int32_t(gStressUntilMs.load() - now) > 0 || touch.pressed();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(fast ? kFrameMs : kIdleFrameMs));
  }
}

// Called from the web server task
void requestPreview(ui::Expression e) { gPreviewRequest = int(e); }

// The web server and MQTT tasks share the engine's network inbox, which takes
// one producer at a time
bool postFromNetwork(const engine::Event& e) {
  xSemaphoreTake(gNetworkInbox, portMAX_DELAY);
  bool ok = gEngine.post(engine::Engine::kFromNetwork, e);
  xSemaphoreGive(gNetworkInbox);
  return ok;
}

// Serial and the dev button post as Engine::kFromSystem (loop task only)
void postCard(engine::CardKind kind, const char* title, const char* body, const char* const* options,
              int optionCount, uint32_t ref, bool sticky = false) {
  engine::Event e;
  e.type = engine::EventType::ShowCard;
  e.card.kind = kind;
  e.card.sticky = sticky;
  e.card.ref = ref;
  core::copyUtf8(e.card.title, sizeof(e.card.title), title);
  core::copyUtf8(e.card.body, sizeof(e.card.body), body);
  for (int i = 0; i < optionCount && i < engine::kMaxOptions; i++) {
    core::copyUtf8(e.card.options[i], sizeof(e.card.options[i]), options[i]);
    e.card.optionCount++;
  }
  gEngine.post(engine::Engine::kFromSystem, e);
}

void printHelp() {
  Serial.println("commands:");
  Serial.println("  n Title|Body                 notification");
  Serial.println("  p Title|Body|Opt1|Opt2[|Opt3] prompt with buttons");
  Serial.println("  char slime|monster|heroine|capri  switch character (save to keep it)");
  Serial.println("  ask <text>                   say something to Capri's brain (MQTT <topic>/ask)");
  Serial.println("  otp 123456                   AI Buddy login OTP (from the backoffice), as on the keypad");
  Serial.println("  preset <name>                active character's preset");
  Serial.println("                               slime: aqua|sprout|kitty|robo|angel|imp|royal|party|mochi");
  Serial.println("                               monster: ember|leaf|bubble (starters), frost|shiny|night");
  Serial.println("  time HH:MM                   set the clock (the settings page sets it too)");
  Serial.println("  rotate 0|90|180|270          screen rotation, clockwise (saved, then restarts)");
  Serial.println("  mirror on|off                screen mirrored left to right, for a prism (same)");
  Serial.println("  menu                         settings menu: touch / long press / drag calibration, AP mode (BOOT held)");
  Serial.println("  slime:");
  Serial.println("    shape round|egg|mochi|drop|squircle|cat|bean");
  Serial.println("    brows none|line|arch|thick|dot");
  Serial.println("    eyes oval|robot|dot|pill   switch eye set");
  Serial.println("    acc head none|antenna|sprout|bow|ears|horns|crown");
  Serial.println("    acc back none|wings|fin|spikes");
  Serial.println("    acc left|right none|arm|heart|star|flag|balloon");
  Serial.println("  face normal|happy|surprised|sleepy|angry|sad   preview expression");
  Serial.println("  config [json]                print active character / merge settings");
  Serial.println("  net [json]                   home Wi-Fi + MQTT: print / merge, saved at once");
  Serial.println("                               {\"wifi\":{\"ssid\",\"pass\"},\"mqtt\":{\"host\",\"port\",\"tls\",\"user\",\"pass\",\"topic\"}}");
  Serial.println("  web [on|off]                 settings web server: status / start / stop (stops itself when idle)");
  Serial.println("  ota [https://url]            update the firmware over the air from that URL / show the state");
  Serial.println("  save | reset                 persist all / active character to defaults");
  Serial.println("  stress | bench               load test / draw cost + memory");
  Serial.println("  imu                          motion sensor readings for 5 s (screen frame)");
  Serial.println("  sleep                        screen off, deep sleep until reset or power cycle");
}

char gJsonBuf[3072];  // loop task only: serial replies

// The web server costs ~15 KB, so it runs only while someone uses it: at boot (the first
// setup goes through it), then until it has been idle this long. `web on` or the menu's
// AP row starts it again. The AP goes down with it once the home Wi-Fi is joined.
// All on the loop task.
constexpr uint32_t kWebBootMs = 10 * 60 * 1000;
constexpr uint32_t kWebIdleMs = 5 * 60 * 1000;
platform::web::Hooks gWebHooks;
uint32_t gWebStartedMs = 0;

bool startWeb() {
  bool ok = platform::web::start(gWebHooks);
  gWebStartedMs = millis();
  return ok;
}

void pollWeb(uint32_t now) {
  if (!platform::web::running() || now < kWebBootMs) return;
  uint32_t last = platform::web::lastRequestMs();
  if (!last || int32_t(last - gWebStartedMs) < 0) last = gWebStartedMs;
  if (now - last < kWebIdleMs) return;
  platform::web::stop();
  Serial.println("web: stopped, idle (`web on` or the menu's AP mode starts it)");
  if (platform::net::apActive() && !platform::net::apRequired() && WiFi.status() == WL_CONNECTED) {
    platform::net::setAp(false);
    Serial.println("wifi: AP off");
  }
}

#if WEB_PROBE
// Diagnostic for a web server that stopped answering under load (HANDOFF): every 10 s the board
// connects to its own port 80. Failing here while the PC cannot connect either: the listener is
// stuck; connecting here but not from outside: the network path. Build with
// PLATFORMIO_BUILD_FLAGS="-DWEB_PROBE=1".
void probeWeb(uint32_t now) {
  static uint32_t nextMs = 10000;
  static bool wasOk = true;
  static int beats = 0;
  if (int32_t(now - nextMs) < 0 || !platform::web::running() || WiFi.status() != WL_CONNECTED) return;
  nextMs = now + 10000;
  WiFiClient c;
  const bool ok = c.connect(WiFi.localIP(), 80, 500);
  c.stop();
  const bool beat = ++beats % 6 == 0;
  if (!ok || !wasOk || beat) {
    char line[128];
    snprintf(line, sizeof(line), "web-probe: self-connect %s (last request %u s ago, heap %u, rssi %d)",
             ok ? (wasOk ? "ok" : "ok again") : "FAILED", unsigned((now - platform::web::lastRequestMs()) / 1000),
             unsigned(ESP.getFreeHeap()), int(WiFi.RSSI()));
    Serial.println(line);
    platform::net::publishDebug(line);  // for runs on battery, with no serial port
  }
  wasOk = ok;
}
#endif

// Battery voltage at the pin's divider, averaged over a few samples (ADC noise is ~5 mV).
int batteryMillivolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(gBoard->batteryPin);
  return int(sum / 16.0f * gBoard->batteryScale + 0.5f);
}

// A reading every 30 s goes into a median of the last 5 (one reading jumps by 20-40 mV with the
// load); that median and its percentage are sent every 5 minutes, and at once when
// <topic>/battery/get arrives (with a fresh reading in it). While MQTT is down it tries again
// every 5 s, so the first one goes out as soon as the broker is joined. Loop task.
constexpr uint32_t kBatterySampleMs = 30 * 1000;
constexpr uint32_t kBatteryEveryMs = 5 * 60 * 1000;
constexpr uint32_t kBatteryRetryMs = 5 * 1000;
std::atomic<bool> gBatteryRequested{false};
core::MedianOfLast<5> gBatteryMedian;

void pollBattery(uint32_t now) {
  static uint32_t nextSampleMs = 0, nextSendMs = 0;
  if (gBoard->batteryPin < 0) return;
  const bool asked = gBatteryRequested.exchange(false);
  if (asked || int32_t(now - nextSampleMs) >= 0) {
    gBatteryMedian.add(batteryMillivolts());
    nextSampleMs = now + kBatterySampleMs;
  }
  if (!asked && int32_t(now - nextSendMs) < 0) return;
  const int mv = gBatteryMedian.median(), percent = core::batteryPercent(mv);
  const bool sent = platform::net::publishBattery(mv, percent);
  nextSendMs = now + (sent ? kBatteryEveryMs : kBatteryRetryMs);
  if (sent || asked) {
    Serial.printf("battery: %d mV, %d%% %s%s\n", mv, percent, sent ? "sent" : "not sent, MQTT is down",
                  asked ? " (asked)" : "");
  }
}

// Machine protocol for the PC settings bridge (tools/studio): one request line
// starting with '@', answered by exactly one line starting with '@', so replies
// can be picked out from the periodic stats. Payloads match the web API.
//   @meta | @config | @merge {json} | @save | @reset | @preset <name> |
//   @preview <expr> | @card {json} | @time {json} | @net [json] | @display [json]
void handleMachine(const char* line) {
  char* out = gJsonBuf;
  auto replyConfig = [out] {
    size_t n = gConfig.toJson(out, sizeof(gJsonBuf));
    Serial.printf("@config %s\n", n ? out : "{}");
  };
  auto reply = [](bool ok) { Serial.println(ok ? "@ok" : "@err"); };

  if (strcmp(line, "@meta") == 0) {
    size_t n = platform::api::metaJson(gCharacters, out, sizeof(gJsonBuf));
    Serial.printf("@meta %s\n", n ? out : "{}");
  } else if (strcmp(line, "@config") == 0) {
    replyConfig();
  } else if (strncmp(line, "@merge ", 7) == 0) {
    if (gConfig.merge(line + 7, strlen(line + 7))) replyConfig();
    else reply(false);
  } else if (strcmp(line, "@save") == 0) {
    reply(gConfig.save());
  } else if (strcmp(line, "@reset") == 0) {
    gConfig.reset();
    replyConfig();
  } else if (strncmp(line, "@preset ", 8) == 0) {
    if (gConfig.applyPreset(line + 8)) replyConfig();
    else reply(false);
  } else if (strncmp(line, "@preview ", 9) == 0) {
    int e = ui::findExpression(line + 9);
    if (e >= 0) gPreviewRequest = e;
    reply(e >= 0);
  } else if (strncmp(line, "@time ", 6) == 0) {
    int64_t local;
    bool ok = platform::api::localTimeFromJson(line + 6, strlen(line + 6), local);
    if (ok) setLocalTime(local);
    reply(ok);
  } else if (strncmp(line, "@card ", 6) == 0) {
    engine::Event e;
    reply(platform::api::cardFromJson(line + 6, strlen(line + 6), e) &&
          gEngine.post(engine::Engine::kFromSystem, e));
  } else if (strcmp(line, "@display") == 0 || strncmp(line, "@display ", 9) == 0) {
    int degrees;
    bool mirror;
    screen(degrees, mirror);
    if (line[8] == ' ' && !(platform::api::screenFromJson(line + 9, strlen(line + 9), degrees, mirror) &&
                             setScreen(degrees, mirror))) {
      reply(false);
    } else {
      Serial.printf("@display {\"rotation\":%d,\"mirror\":%s,\"restart\":%s}\n", degrees, mirror ? "true" : "false",
                    gRestartAtMs ? "true" : "false");
    }
  } else if (strcmp(line, "@net") == 0 || strncmp(line, "@net ", 5) == 0) {
    if (line[4] == ' ' && !platform::net::merge(line + 5, strlen(line + 5))) {
      reply(false);
    } else {
      size_t n = platform::net::toJson(out, sizeof(gJsonBuf));
      Serial.printf("@net %s\n", n ? out : "{}");
    }
  } else {
    reply(false);
  }
}

void handleCommand(char* line) {
  if (line[0] == '@') {
    handleMachine(line);
    return;
  }
  static uint32_t ref = 1000;
  if ((line[0] == 'n' || line[0] == 'p') && line[1] == ' ') {
    char* parts[2 + engine::kMaxOptions] = {};
    int count = 0;
    for (char* tok = strtok(line + 2, "|"); tok && count < 2 + engine::kMaxOptions; tok = strtok(nullptr, "|")) {
      parts[count++] = tok;
    }
    if (line[0] == 'n') {
      postCard(engine::CardKind::Notification, count > 0 ? parts[0] : "", count > 1 ? parts[1] : "",
               nullptr, 0, 0);
    } else if (count >= 3) {
      postCard(engine::CardKind::Prompt, parts[0], parts[1], parts + 2, count - 2, ++ref);
      Serial.printf("prompt ref %u queued\n", unsigned(ref));
    } else {
      Serial.println("p needs Title|Body|Option...");
    }
  } else if (strncmp(line, "otp ", 4) == 0) {
    Serial.println(platform::aibuddy::submitOtp(line + 4) ? "OTP sent" : "no AI Buddy login waits for an OTP (6 digits)");
  } else if (strncmp(line, "ask ", 4) == 0) {
    JsonDocument doc;
    doc["type"] = "chat";
    doc["text"] = line + 4;
    char json[600];
    const size_t n = serializeJson(doc, json, sizeof(json));
    Serial.println(n < sizeof(json) && platform::net::publishAsk(json, n) ? "sent" : "not sent (MQTT down?)");
  } else if (strncmp(line, "char ", 5) == 0) {
    char json[48];
    snprintf(json, sizeof(json), "{\"character\":\"%s\"}", line + 5);
    Serial.println(gConfig.merge(json, strlen(json)) ? "ok (not saved)" : "unknown character");
  } else if (strncmp(line, "rotate ", 7) == 0 || strncmp(line, "mirror ", 7) == 0) {
    const bool rotate = line[0] == 'r';
    const bool mirror = rotate ? gMirror : strcmp(line + 7, "on") == 0;
    if (!rotate && !mirror && strcmp(line + 7, "off") != 0) Serial.println("mirror on|off");
    else if (!setScreen(rotate ? atoi(line + 7) : gTurns * 90, mirror)) Serial.println("rotate 0|90|180|270");
    else Serial.println(gRestartAtMs ? "saved, restarting" : "already so");
  } else if (strncmp(line, "time ", 5) == 0) {
    int h, m;
    bool ok = sscanf(line + 5, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60;
    if (ok) setLocalTime(h * 3600 + m * 60);
    Serial.println(ok ? "ok" : "time HH:MM");
  } else if (strncmp(line, "eyes ", 5) == 0) {
    char json[64];
    snprintf(json, sizeof(json), "{\"slime\":{\"eyeSet\":\"%s\"}}", line + 5);
    Serial.println(gConfig.merge(json, strlen(json)) ? "ok" : "bad name");
  } else if (strncmp(line, "preset ", 7) == 0) {
    Serial.println(gConfig.applyPreset(line + 7) ? "ok (not saved)" : "unknown preset");
  } else if (strncmp(line, "shape ", 6) == 0 || strncmp(line, "brows ", 6) == 0) {
    char json[80];
    if (line[0] == 's') snprintf(json, sizeof(json), "{\"slime\":{\"bodyShape\":\"%s\"}}", line + 6);
    else snprintf(json, sizeof(json), "{\"slime\":{\"brows\":{\"style\":\"%s\"}}}", line + 6);
    Serial.println(gConfig.merge(json, strlen(json)) ? "ok" : "bad name");
  } else if (strncmp(line, "acc ", 4) == 0) {
    char slot[12] = "", type[16] = "";
    if (sscanf(line + 4, "%11s %15s", slot, type) == 2) {
      char json[112];
      snprintf(json, sizeof(json), "{\"slime\":{\"accessories\":{\"%s\":{\"type\":\"%s\"}}}}", slot, type);
      Serial.println(gConfig.merge(json, strlen(json)) ? "ok" : "bad json");
    } else {
      Serial.println("acc head|back|left|right <type>");
    }
  } else if (strncmp(line, "face ", 5) == 0) {
    int e = ui::findExpression(line + 5);
    if (e >= 0) gPreviewRequest = e;
    Serial.println(e >= 0 ? "ok" : "unknown expression");
  } else if (strncmp(line, "config", 6) == 0) {
    if (line[6] == ' ') {
      Serial.println(gConfig.merge(line + 7, strlen(line + 7)) ? "ok" : "bad json");
    } else {
      size_t n = gConfig.toJson(gJsonBuf, sizeof(gJsonBuf));
      Serial.println(n ? gJsonBuf : "config too large");
    }
  } else if (strcmp(line, "net") == 0 || strncmp(line, "net ", 4) == 0) {
    if (line[3] == ' ' && !platform::net::merge(line + 4, strlen(line + 4))) {
      Serial.println("bad json, or a value too long");
    } else {
      size_t n = platform::net::toJson(gJsonBuf, sizeof(gJsonBuf));
      Serial.println(n ? gJsonBuf : "{}");
    }
  } else if (strcmp(line, "save") == 0) {
    Serial.println(gConfig.save() ? "saved" : "save failed");
  } else if (strcmp(line, "reset") == 0) {
    gConfig.reset();
    Serial.println("defaults (not saved)");
  } else if (strncmp(line, "web", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    if (strcmp(line, "web on") == 0) Serial.println(startWeb() ? "web: on" : "web: start failed");
    else if (strcmp(line, "web off") == 0) { platform::web::stop(); Serial.println("web: off"); }
    else if (!platform::web::running()) Serial.println("web: off");
    else Serial.printf("web: on, last request %u s ago\n", unsigned((millis() - platform::web::lastRequestMs()) / 1000));
  } else if (strncmp(line, "ota", 3) == 0 && (line[3] == '\0' || line[3] == ' ')) {
    if (line[3] == ' ' && line[4]) {
      Serial.println(platform::ota::start(line + 4) ? "ota: started" : "ota: refused (an https:// URL is needed, or an update is running)");
    } else if (platform::ota::toJson(gJsonBuf, sizeof(gJsonBuf))) {
      Serial.println(gJsonBuf);
    }
  } else if (strcmp(line, "menu") == 0 || strcmp(line, "touchcal") == 0) {
    gMenuOpen = true;
    Serial.printf("settings menu on screen; touch calibration now x = %.4f*X %+.2f  y = %.4f*Y %+.2f  drag > %d px  "
                  "long press %d ms\n",
                  gTouchCal.ax, gTouchCal.bx, gTouchCal.ay, gTouchCal.by, gTouchCal.slopPx, gTouchCal.longPressMs);
  } else if (strcmp(line, "batt") == 0) {
    if (gBoard->batteryPin < 0) Serial.println("no battery sense pin");
    else {
      const int mv = gBatteryMedian.median();
      Serial.printf("batt: %d mV now (x%.2f); median of the last %u: %d mV, about %d%%\n", batteryMillivolts(),
                    gBoard->batteryScale, unsigned(gBatteryMedian.count()), mv, core::batteryPercent(mv));
    }
  } else if (strncmp(line, "light ", 6) == 0) {
    if (gBoard->backlight) gBoard->backlight->set(uint8_t(constrain(atoi(line + 6), 0, 255)));
  } else if (strcmp(line, "imu") == 0) {
    if (!gBoard->imu) Serial.println("no motion sensor");
    else gImuPrintUntilMs = millis() + 5000;
  } else if (strcmp(line, "stress") == 0) {
    gStressUntilMs = millis() + 6000;
    Serial.println("stress: full-screen redraw every frame for 6 s");
  } else if (strcmp(line, "bench") == 0) {
    gBenchRequested = true;
  } else if (strcmp(line, "sleep") == 0) {
    // No wake source: only the reset button or a power cycle brings it back
    Serial.println("sleeping: screen off until reset or power cycle");
    Serial.flush();
    if (gBoard->backlight) gBoard->backlight->holdOff();
    WiFi.mode(WIFI_OFF);
    esp_deep_sleep_start();
  } else {
    printHelp();
  }
}


void printStats(uint32_t periodMs) {
  uint32_t frames = gStats.frames.exchange(0), renderUs = gStats.renderUs.exchange(0);
  uint32_t flushUs = gStats.flushUs.exchange(0), pixels = gStats.pixels.exchange(0);
  uint32_t engineUs = gStats.engineUs.exchange(0), ticks = gStats.engineTicks.exchange(0);
  char cpu[32] = "";
#if PERF_PROBE
  snprintf(cpu, sizeof(cpu), " | cpu0 %.0f%% cpu1 %.0f%%", gLoad0.load(), gLoad1.load());
#endif
  float perFrame = frames ? 1000.0f * frames : 1.0f;
  Serial.printf(
      "fps %.1f | render %.2f ms (draw %.2f + spi wait %.2f) | %u px/frame | engine %u us | heap %u (min %u)%s\n",
      frames * 1000.0f / periodMs, renderUs / perFrame, (renderUs - flushUs) / perFrame,
      flushUs / perFrame, unsigned(frames ? pixels / frames : 0),
      unsigned(ticks ? engineUs / ticks : 0), unsigned(ESP.getFreeHeap()),
      unsigned(ESP.getMinFreeHeap()), cpu);
}

void startNetwork() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(gApName, sizeof(gApName), "CYD-Blob-%02X%02X", mac[4], mac[5]);
  gNetworkInbox = xSemaphoreCreateMutex();
  platform::net::Hooks net;
  net.postCard = postFromNetwork;
  net.setTime = setLocalTime;
  net.batteryRequest = [] { gBatteryRequested = true; };
  net.needOtp = [](const char* device, const char* reason) {
    snprintf(gOtpDevice, sizeof(gOtpDevice), "%s", device);
    snprintf(gOtpReason, sizeof(gOtpReason), "%s", reason);
    gOtpRequest = true;
    Serial.printf("aibuddy: type the OTP for %s on the screen, or `otp <6 digits>`\n", device);
  };
  HEAP_MARK("before net");
  platform::net::begin(gApName, kApPassword, gBoard->name, net);
  HEAP_MARK("net::begin");

  gWebHooks.config = &gConfig;
  gWebHooks.characters = &gCharacters;
  gWebHooks.postCard = postFromNetwork;
  gWebHooks.preview = requestPreview;
  gWebHooks.setTime = setLocalTime;
  gWebHooks.touchCalibration = [] { gMenuOpen = true; };
  gWebHooks.screen = screen;
  gWebHooks.setScreen = setScreen;
  gWebHooks.ask = platform::net::publishAsk;
  gWebHooks.otp = platform::aibuddy::submitOtp;
  bool web = startWeb();
  HEAP_MARK("web::start");
  gSettingsMenu.setApInfo(gApName, kApPassword, "http://192.168.4.1/");
  if (platform::net::apActive()) {
    Serial.printf("wifi AP \"%s\" pw \"%s\" -> http://%s/ %s\n", gApName, kApPassword,
                  WiFi.softAPIP().toString().c_str(), web ? "" : "(web server failed)");
  } else {
    Serial.printf("wifi: station only, the AP comes from the menu %s\n", web ? "" : "(web server failed)");
  }
}

}  // namespace

void setup() {
  Serial.setRxBufferSize(2048);  // the PC bridge sends whole JSON patches in one line
  Serial.begin(115200);
  HEAP_MARK("boot");
  platform::ConfigStore::loadScreen(gTurns, gMirror);
  gBoard = &hal::initBoard(gTurns, gMirror);
  HEAP_MARK("board");
  static TimedDisplay timed(gBoard->display);
  gDisplay = &timed;
  hal::Display* display = gDisplay;

  size_t stripPixels = size_t(display->width()) * gBoard->stripLines;
  gfx::Color* buf0 = display->allocBuffer(stripPixels);
  gfx::Color* buf1 = display->allocBuffer(stripPixels);
  if (!gCompositor.begin(display, buf0, buf1, stripPixels)) {
    Serial.println("compositor init failed (strip buffers?)");
    for (;;) delay(1000);
  }
  HEAP_MARK("compositor");

  gConfig.begin(gCharacters);
  gView.begin(gCompositor, display->width(), display->height(), gCharacters, gConfig.active(), esp_random());
  HEAP_MARK("config+view");
  gTouchCalScreen.attach(gCompositor, ui::kTouchCalibrationZ, display->width(), display->height());
  gSettingsMenu.attach(gCompositor, ui::kSettingsMenuZ, display->width(), display->height());
  gOtpScreen.attach(gCompositor, ui::kOtpScreenZ, display->width(), display->height());
  hal::TouchCalibration cal = gBoard->touchCalibration;
  gConfig.loadTouchCalibration(cal);
  gTouchCal = toScreenFrame(cal);
  gEngine.seed(esp_random());
  gEngine.tick(millis());
  gEngine.pollState();
  gView.update(gEngine.state(), millis());

  // First full frame before the backlight comes on, so no stale image flashes
  gCompositor.render();
  display->waitIdle();
  if (gBoard->backlight) gBoard->backlight->set(255);
  if (gBoard->devButtonPin >= 0) pinMode(gBoard->devButtonPin, INPUT_PULLUP);

#if PERF_PROBE
  perf::begin(300);  // calibrate on an idle system, before the app tasks and Wi-Fi exist
#endif
  xTaskCreatePinnedToCore(engineTask, "engine", 4096, nullptr, 3, &gEngineTask, 0);
  xTaskCreatePinnedToCore(uiTask, "ui", 6144, nullptr, 2, &gUiTask, 1);
  if (gBoard->speaker) xTaskCreatePinnedToCore(audioTask, "audio", 3072, nullptr, 1, nullptr, 0);
  HEAP_MARK("tasks");
  startNetwork();

  // Why the last run ended: a brownout from a weak USB supply looks like a crash otherwise
  static const char* const kResetNames[] = {"unknown", "power-on", "external", "software", "panic",
                                            "interrupt watchdog", "task watchdog", "watchdog", "deep sleep",
                                            "brownout", "sdio"};
  esp_reset_reason_t reset = esp_reset_reason();
  Serial.printf("\n=== %s | %dx%d (rotation %d%s) | heap %u | reset: %s | build %s ===\n", gBoard->name,
                display->width(), display->height(), gTurns * 90, gMirror ? ", mirrored" : "",
                unsigned(ESP.getFreeHeap()),
                unsigned(reset) < sizeof(kResetNames) / sizeof(kResetNames[0]) ? kResetNames[reset] : "?",
                platform::ota::build());
  printHelp();

  // Only while the AP is the way in (no home Wi-Fi saved)
  if (platform::net::apActive()) {
    char body[96];
    snprintf(body, sizeof(body), "Join Wi-Fi %s (pw %s), then open 192.168.4.1", gApName, kApPassword);
    // Stays until tapped: whoever looks at the screen later still learns the way in.
    // Without a touch panel nothing could close it, so it goes on its own there.
    postCard(engine::CardKind::Notification, "Blob Studio", body, nullptr, 0, 0, gBoard->touch != nullptr);
  }
}

void loop() {
  static char line[3072];
  static size_t len = 0;
  while (Serial.available()) {
    char c = char(Serial.read());
    if (c == '\n' || c == '\r') {
      if (len) {
        line[len] = '\0';
        handleCommand(line);
      }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }

  // BOOT button: a press cycles sample cards (notification, prompt, then the same
  // in Korean) when released; held, it opens the settings menu, the way in when
  // the touch is too far off to reach anything (or there is none). In the menu a
  // press moves to the next row and a hold picks the row. A calibration screen
  // takes any press as Cancel.
  constexpr uint32_t kHoldMs = 1000, kDebounceMs = 30;
  static bool buttonWasDown = false, pressUsed = false;
  static uint32_t downMs = 0;
  if (gBoard->devButtonPin >= 0) {
    bool down = digitalRead(gBoard->devButtonPin) == LOW;
    uint32_t ms = millis();
    if (down && !buttonWasDown) {
      downMs = ms;
      pressUsed = gTouchCalRunning;
      if (pressUsed) gTouchCalButton = true;
    } else if (down && !pressUsed && ms - downMs >= kHoldMs) {
      pressUsed = true;
      if (gMenuActive) gMenuButton = 2;
      else gMenuOpen = true;
    } else if (!down && buttonWasDown && !pressUsed && ms - downMs >= kDebounceMs) {
      if (gMenuActive) gMenuButton = 1;
      else gSampleCardRequest = true;
    }
    buttonWasDown = down;
  }
  // The BOOT button and a shake with no card up take turns through the test cards
  static uint32_t samples = 0;
  if (gSampleCardRequest.exchange(false)) gEngine.post(engine::Engine::kFromSystem, platform::sampleCard(samples++));

  engine::Outcome o;
  while (gEngine.pollOutcome(o)) {
    if (o.option >= 0) Serial.printf("prompt %u answered: option %d\n", unsigned(o.ref), o.option);
    else Serial.printf("prompt %u expired\n", unsigned(o.ref));
    platform::net::publishOutcome(o);
  }

  uint32_t now = millis();
  platform::net::poll(now);
  pollWeb(now);
#if WEB_PROBE
  probeWeb(now);
#endif
  pollBattery(now);
  platform::ota::poll(now);
  if (gApRequest.exchange(false)) {  // the menu's AP row: the settings page then needs the server too
    if (platform::net::apActive()) {
      platform::net::setAp(false);
    } else {
      platform::net::setAp(true);
      startWeb();
    }
    Serial.printf("wifi: AP %s (menu)\n", platform::net::apActive() ? "on" : "off");
  }
  gApOn = platform::net::apActive();
  gApLocked = platform::net::apRequired();

  const uint32_t restartAt = gRestartAtMs;
  if (restartAt && int32_t(now - restartAt) >= 0) {
    Serial.println("restarting for the new screen rotation");
    Serial.flush();
    ESP.restart();
  }

  static uint32_t imuPrintMs = 0;
  if (int32_t(gImuPrintUntilMs.load() - now) > 0 && now - imuPrintMs >= 200) {
    imuPrintMs = now;
    const hal::ImuSample s = gImu;
    Serial.printf("imu accel %+.2f %+.2f %+.2f g  gyro %+6.1f %+6.1f %+6.1f dps  jolt max %.2f g\n", s.ax, s.ay,
                  s.az, s.gx, s.gy, s.gz, gImuPeakJolt);
    gImuPeakJolt = 0;  // a race with the UI task only loses a peak
  }

  static uint32_t statsMs = 0;
  if (now - statsMs >= 2000) {
    printStats(now - statsMs);
    statsMs = now;
  }
  delay(10);
}
