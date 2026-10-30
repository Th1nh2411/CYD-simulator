// Deterministic replay of a scripted session through the portable app (engine,
// view, character, compositor): fixed seeds, a simulated clock and an
// off-screen copy of the screen instead of a display. Prints screen hashes at
// checkpoints, so two builds can be compared frame by frame, e.g. the `slime`
// tag against main after a refactor (docs/HANDOFF.md). Builds against both the
// tag (no ui::Character yet) and later trees.
//   python -m platformio run -e replay -t upload
//   python tools/serial/bootlog.py COM3 40 > replay.txt
// REPLAY_ACTIVE picks the character on screen (index in ui::CharacterSet): 1 plays
// the monster's own script through its three starters, to compare monster builds.

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>

#include "app/engine/engine.h"
#include "app/gfx/compositor.h"
#include "app/ui/view.h"

#if __has_include("app/ui/characters.h")
#define REPLAY_CHARACTERS 1
#include "app/ui/characters.h"
#else
#define REPLAY_CHARACTERS 0
#include "app/ui/blob/character_json.h"
#include "app/ui/blob/character_presets.h"
#endif

#ifndef REPLAY_ACTIVE
#define REPLAY_ACTIVE 0
#endif

namespace {

constexpr int kActive = REPLAY_ACTIVE;
constexpr int kW = 320, kH = 240, kStripLines = 24;
constexpr uint32_t kViewSeed = 12345, kEngineSeed = 777;
constexpr uint32_t kStartMs = 1000, kEndMs = 50000, kCheckpointMs = 1000;
constexpr uint32_t kFrameMs = 16, kIdleFrameMs = 33, kPreviewMs = 2500;

uint32_t mixHash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
constexpr uint32_t kHashSeed = 2166136261u;

// Keeps what the panel would show; hashes the flushed stream on the way
class ShadowDisplay : public hal::Display {
 public:
  bool begin() {
    for (gfx::Color*& row : rows_) {
      row = static_cast<gfx::Color*>(calloc(kW, sizeof(gfx::Color)));
      if (!row) return false;
    }
    return true;
  }
  int width() const override { return kW; }
  int height() const override { return kH; }
  gfx::Color* allocBuffer(size_t pixels) override {
    return static_cast<gfx::Color*>(malloc(pixels * sizeof(gfx::Color)));
  }
  void flush(const gfx::Rect& a, const gfx::Color* px) override {
    uint32_t t0 = micros();
    stream = mixHash(mixHash(stream, uint16_t(a.x) | uint32_t(uint16_t(a.y)) << 16),
                     uint16_t(a.w) | uint32_t(uint16_t(a.h)) << 16);
    for (int y = 0; y < a.h; y++) {
      const gfx::Color* src = px + y * a.w;
      memcpy(rows_[a.y + y] + a.x, src, size_t(a.w) * sizeof(gfx::Color));
      for (int x = 0; x < a.w; x++) stream = mixHash(stream, src[x]);
    }
    pixels += uint32_t(a.area());
    flushUs += micros() - t0;
  }
  void waitIdle() override {}

  uint32_t screenHash() const {
    uint32_t h = kHashSeed;
    for (const gfx::Color* row : rows_) {
      for (int x = 0; x < kW; x += 2) h = mixHash(h, row[x] | uint32_t(row[x + 1]) << 16);
    }
    return h;
  }

  uint32_t stream = kHashSeed, pixels = 0, flushUs = 0;

 private:
  gfx::Color* rows_[kH] = {};
};

enum class Op : uint8_t { Notify, Prompt, Tap, Face, Patch, Preset, Answer };

struct Step {
  uint32_t atMs;  // since start
  Op op;
  const char* text;  // Notify/Prompt "title|body|option...", Patch slime JSON, Preset name
  int x, y;          // Tap point; Face: x = expression index; Answer: x = option, as if answered remotely
};

// Card taps: notification card spans y 124..232; prompt buttons are stacked at
// x 150..302, y 160..188 (first of two) / 194..222 (last). Character rests at the center.
const Step kScript[] = {
    {300, Op::Notify, "New message|Your build finished. Tap this card to dismiss it.", 0, 0},
    {2200, Op::Tap, nullptr, 160, 170},
    {3500, Op::Prompt, "Deploy?|Release v1.2 to production?|Approve|Deny", 0, 0},
    {5200, Op::Tap, nullptr, 226, 174},
    {7000, Op::Tap, nullptr, 160, 120},
    {9000, Op::Face, nullptr, 3, 0},
    {12000, Op::Face, nullptr, 4, 0},
    {15000, Op::Preset, "imp", 0, 0},
    {16000, Op::Notify, "Build|Green across the board.", 0, 0},
    {17500, Op::Patch, R"({"bodyShape":"cat","accessories":{"left":{"type":"arm"},"right":{"type":"star","color":"#ffd24a"}}})", 0, 0},
    {18500, Op::Prompt, "Pick|Choose one.|A|B|C", 0, 0},
    {20500, Op::Tap, nullptr, 226, 208},
    {22000, Op::Preset, "robo", 0, 0},
    {23000, Op::Patch, R"({"motion":{"splash":1,"drop":2.4}})", 0, 0},
    {23500, Op::Prompt, "Merge?|Merge the branch now?|Yes|No", 0, 0},
    {25500, Op::Tap, nullptr, 5, 5},
    {26000, Op::Tap, nullptr, 226, 208},
    {28000, Op::Preset, "angel", 0, 0},
    {28500, Op::Tap, nullptr, 160, 120},
    {30000, Op::Notify, "Hello|Wings flap faster while moving.", 0, 0},
    {31000, Op::Patch, R"({"size":0.26,"aspect":1.2,"eyeSet":"pill","brows":{"style":"thick"}})", 0, 0},
    {36000, Op::Preset, "party", 0, 0},
    {37000, Op::Notify, "Party|This one closes by itself after six seconds.", 0, 0},
    {44000, Op::Preset, "mochi", 0, 0},
    {44500, Op::Face, nullptr, 2, 0},
    {47000, Op::Face, nullptr, 5, 0},
    {48000, Op::Tap, nullptr, 160, 120},
};

// The monster: each starter in a battle with its own move, the fire variants' colors
const Step kMonsterScript[] = {
    {300, Op::Notify, "New message|Your build finished. Tap this card to dismiss it.", 0, 0},
    {2200, Op::Tap, nullptr, 160, 170},
    {3500, Op::Prompt, "Deploy?|Release v1.2 to production?|Approve|Deny", 0, 0},
    {7500, Op::Answer, nullptr, 0, 0},
    {12000, Op::Preset, "leaf", 0, 0},
    {13000, Op::Prompt, "Pick|Choose one.|A|B|C", 0, 0},
    {17000, Op::Answer, nullptr, 0, 0},
    {22000, Op::Preset, "bubble", 0, 0},
    {23000, Op::Prompt, "Merge?|Merge the branch now?|Yes|No", 0, 0},
    {27000, Op::Answer, nullptr, 0, 0},
    {31000, Op::Notify, "Hello|Wings flap faster while moving.", 0, 0},
    {33000, Op::Tap, nullptr, 160, 120},
    {36000, Op::Preset, "frost", 0, 0},
    {37000, Op::Notify, "Party|This one closes by itself after six seconds.", 0, 0},
    {44000, Op::Preset, "night", 0, 0},
    {45000, Op::Prompt, "Last?|One more?|Yes|No", 0, 0},
    {48000, Op::Answer, nullptr, 1, 0},
};

struct Rig {
  ShadowDisplay display;
  gfx::Compositor comp;
  engine::Engine engine;
  ui::View view;
#if REPLAY_CHARACTERS
  ui::CharacterSet characters;
#else
  ui::blob::CharacterConfig config;
  bool configChanged = true;  // the UI task applies the store's first publish on its first frame
#endif
  int preview = -1;
  char json[4096];

  // Mirrors setup(): view on the saved (default) config, one engine tick, first frame
  bool begin(uint32_t now) {
    if (!display.begin()) return false;
    size_t strip = size_t(kW) * kStripLines;
    if (!comp.begin(&display, display.allocBuffer(strip), display.allocBuffer(strip), strip)) return false;
#if REPLAY_CHARACTERS
    for (int i = 0; i < characters.count(); i++) characters[i].publishSettings();
    view.begin(comp, kW, kH, characters, kActive, kViewSeed);
    // The tag's UI task re-applied its start config on the first frame (a full,
    // identical redraw); do the same so the flushed streams compare too
    characters[kActive].publishSettings();
#else
    view.begin(comp, kW, kH, config, kViewSeed);
#endif
    engine.seed(kEngineSeed);
    engine.tick(now);
    engine.pollState();
    view.update(engine.state(), now);
    comp.render();
    return true;
  }

  // The character's settings as JSON, to check the codec round trip
  size_t settingsJson() {
#if REPLAY_CHARACTERS
    JsonDocument doc;
    characters[kActive].settingsToJson(doc.to<JsonObject>());
    size_t n = serializeJson(doc, json, sizeof(json));
    return n < sizeof(json) ? n : 0;
#else
    return ui::blob::toJson(config, json, sizeof(json));
#endif
  }

  bool patch(const char* text) {
#if REPLAY_CHARACTERS
    JsonDocument doc;
    if (deserializeJson(doc, text) || doc.as<JsonObjectConst>().isNull()) return false;
    characters[kActive].mergeSettings(doc.as<JsonObjectConst>());
    characters[kActive].publishSettings();
    return true;
#else
    configChanged = true;
    return ui::blob::mergeJson(text, strlen(text), config);
#endif
  }

  bool preset(const char* name) {
#if REPLAY_CHARACTERS
    bool ok = characters[kActive].applyPreset(name);
    characters[kActive].publishSettings();
    return ok;
#else
    configChanged = true;
    return ui::blob::applyPreset(name, config);
#endif
  }

  void syncConfig() {
#if REPLAY_CHARACTERS
    view.syncCharacter(kActive);
#else
    if (configChanged) view.applyConfig(config);
    configChanged = false;
#endif
  }

  void showPreview(uint32_t now) {
    if (preview < 0) return;
#if REPLAY_CHARACTERS
    view.previewExpression(ui::Expression(preview), now + kPreviewMs);
#else
    view.previewExpression(preview, now + kPreviewMs);
#endif
    preview = -1;
  }

  void postCard(engine::CardKind kind, const char* text) {
    char buf[160];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* parts[2 + engine::kMaxOptions] = {};
    int count = 0;
    for (char* tok = strtok(buf, "|"); tok && count < 2 + engine::kMaxOptions; tok = strtok(nullptr, "|")) {
      parts[count++] = tok;
    }
    engine::Event e;
    e.type = engine::EventType::ShowCard;
    e.card.kind = kind;
    strncpy(e.card.title, count > 0 ? parts[0] : "", sizeof(e.card.title) - 1);
    strncpy(e.card.body, count > 1 ? parts[1] : "", sizeof(e.card.body) - 1);
    for (int i = 2; i < count; i++) strncpy(e.card.options[e.card.optionCount++], parts[i], sizeof(e.card.options[0]) - 1);
    engine.post(engine::Engine::kFromSystem, e);
  }
};

void run() {
  Rig* rig = new Rig;
  uint32_t now = kStartMs;
  if (!rig->begin(now)) {
    Serial.println("replay: out of memory");
    return;
  }
  Serial.printf("replay %s, active %d, heap %u\n", REPLAY_CHARACTERS ? "characters" : "slime tag", kActive,
                unsigned(ESP.getFreeHeap()));
  size_t n = rig->settingsJson();
  Serial.printf("config %u %s\n", unsigned(n), n ? rig->json : "");

  const Step* script = kActive == 1 ? kMonsterScript : kScript;
  const size_t steps = kActive == 1 ? sizeof(kMonsterScript) / sizeof(kMonsterScript[0])
                                    : sizeof(kScript) / sizeof(kScript[0]);
  size_t next = 0;
  uint32_t frames = 0, chain = kHashSeed, nextCheckpoint = kCheckpointMs;
  uint64_t updateUs = 0, drawUs = 0;
  while (now - kStartMs < kEndMs) {
    now += rig->view.wantsFullFrameRate(now) ? kFrameMs : kIdleFrameMs;
    uint32_t t = now - kStartMs;

    // Inputs due this frame, in UI task order: config, preview, taps
    struct Tap {
      int x, y;
    } taps[4];
    int tapCount = 0;
    for (; next < steps && script[next].atMs <= t; next++) {
      const Step& s = script[next];
      switch (s.op) {
        case Op::Notify:
          rig->postCard(engine::CardKind::Notification, s.text);
          break;
        case Op::Prompt:
          rig->postCard(engine::CardKind::Prompt, s.text);
          break;
        case Op::Tap:
          if (tapCount < 4) taps[tapCount++] = {s.x, s.y};
          break;
        case Op::Face:
          rig->preview = s.x;
          break;
        case Op::Answer: {
          engine::Event e;
          e.type = engine::EventType::TapOption;
          e.value = s.x;
          rig->engine.post(engine::Engine::kFromUi, e);
          Serial.printf("t=%5u answer %d\n", unsigned(t), s.x);
          break;
        }
        case Op::Patch:
        case Op::Preset: {
          bool ok = s.op == Op::Patch ? rig->patch(s.text) : rig->preset(s.text);
          size_t len = rig->settingsJson();
          uint32_t h = kHashSeed;
          for (size_t i = 0; i < len; i++) h = mixHash(h, uint8_t(rig->json[i]));
          Serial.printf("t=%5u %s %s ok=%d json %u %08x\n", unsigned(t), s.op == Op::Patch ? "patch" : "preset",
                        s.op == Op::Patch ? "" : s.text, ok, unsigned(len), unsigned(h));
          break;
        }
      }
    }
    rig->syncConfig();
    rig->showPreview(now);
    for (int i = 0; i < tapCount; i++) {
      engine::Event e;
      bool hit = rig->view.handleTap(taps[i].x, taps[i].y, e);
      if (hit) rig->engine.post(engine::Engine::kFromUi, e);
      Serial.printf("t=%5u tap %d,%d -> %s %d\n", unsigned(t), taps[i].x, taps[i].y,
                    hit ? (e.type == engine::EventType::TapOption ? "option" : e.type == engine::EventType::TapCard ? "card" : "character") : "miss",
                    int(e.value));
    }

    rig->engine.tick(now);
    rig->engine.pollState();
    uint32_t t0 = micros();
    rig->view.update(rig->engine.state(), now);
    uint32_t t1 = micros();
    rig->display.flushUs = 0;
    rig->comp.render();
    uint32_t t2 = micros();
    updateUs += t1 - t0;
    drawUs += (t2 - t1) - rig->display.flushUs;
    frames++;

    uint32_t screen = rig->display.screenHash();
    chain = mixHash(chain, screen);
    if (t >= nextCheckpoint) {
      Serial.printf("t=%5u f=%4u screen %08x chain %08x px %8u stream %08x\n", unsigned(t), unsigned(frames),
                    unsigned(screen), unsigned(chain), unsigned(rig->display.pixels), unsigned(rig->display.stream));
      nextCheckpoint += kCheckpointMs;
    }
  }
  Serial.printf("done frames %u chain %08x px %u stream %08x\n", unsigned(frames), unsigned(chain),
                unsigned(rig->display.pixels), unsigned(rig->display.stream));
  Serial.printf("perf update %.1f us/frame, draw %.1f us/frame, heap %u\n", double(updateUs) / frames,
                double(drawUs) / frames, unsigned(ESP.getFreeHeap()));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  run();
}

void loop() { delay(1000); }
