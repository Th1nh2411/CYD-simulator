#include "app/ui/capri/capri_character.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/ui/capri/capri_json.h"
#include "app/ui/capri/capri_voice.h"

namespace ui::capri {

namespace {

constexpr float kPi = 3.14159265f;

// Layer depths above the character's base z
enum Depth : int { kZSky = 0, kZFigure = 20, kZPanel = 50, kZCutin = 70 };

// Her poses change 15 times a second, in held key poses as in capri-desktop's
// acting (the idle bob 0, -2, -4, -2 px)
constexpr uint32_t kPoseStepMs = 66;
constexpr int kBob[4] = {0, -2, -4, -2};
constexpr int kBobHold[4] = {8, 5, 8, 5};
constexpr uint32_t kOpenMs = 210, kCloseMs = 160, kOpenDelayMs = 120;
constexpr uint32_t kChoicesDelayMs = 120, kChoicesMs = 220, kPressedMs = 200;
constexpr uint32_t kSelfHoldMs = 2600, kReactionHoldMs = 1600;
constexpr uint32_t kCutinMs = 1100, kCutinRevealMs = 200;
constexpr uint32_t kHopMs = 320, kBlipMs = 85;  // a blip every few letters, not each
// Her cels: talking mouth steps, the hello wave, a happy squint, the swing of a poke
constexpr uint32_t kMouthStepMs = 90, kWaveStepMs = 140, kHappyMs = 1400, kSwingMs = 260;
constexpr uint8_t kTalk[4] = {kCelMouth1, kCelMouth2, kCelMouth3, kCelMouth2};
constexpr uint8_t kWave[6] = {kCelWave1, kCelWave2, kCelWave3, kCelWave2, kCelWave3, kCelWave2};
// Her entrance on capri-desktop's 24 fps beats (fx.js INTRO_T), in ms
constexpr uint32_t kBeatMs = 42;
constexpr uint32_t kIntroHands = 6 * kBeatMs, kIntroRight = 8 * kBeatMs, kIntroReach = 10 * kBeatMs,
                   kIntroGrab = 16 * kBeatMs, kIntroHold = 21 * kBeatMs, kIntroHi = 34 * kBeatMs,
                   kIntroWave = 46 * kBeatMs, kIntroRegrab = 63 * kBeatMs, kIntroHappy = 65 * kBeatMs,
                   kIntroEnd = 80 * kBeatMs;
constexpr uint8_t kSayHi[10] = {kCelMouth1, kCelMouth1, kCelMouth3, kCelMouth3, kCelMouth3,
                                kCelMouth3, kCelMouth2, kCelMouth2, kCelMouth1, kCelMouth1};
constexpr uint8_t kIntroWaveCels[5] = {kCelWave1, kCelWave3, kCelWave2, kCelWave3, kCelWave1};
constexpr int kImpact[5] = {24, -17, 11, -6, 3};  // her face hitting the glass, px at 1672 wide
constexpr uint32_t kGreetMs = 1800;             // after she appears, as on the desktop
constexpr uint32_t kBrainWaitMs = 30000;        // her brain's answer comes in by then, or she says it is offline
constexpr uint32_t kNudgeAfterMs = 2 * 3600 * 1000u, kNudgeAnswerMs = 60000;
constexpr uint32_t kSatelliteMs = 14000;

uint32_t since(uint32_t now, uint32_t t) { return int32_t(now - t) > 0 ? now - t : 0; }
bool reached(uint32_t now, uint32_t t) { return int32_t(now - t) >= 0; }

gfx::Color hexColor(uint32_t rgb) { return gfx::rgb(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

const char* const kFaceNames[kFaceCount] = {"calm", "aside", "smug", "wide", "look", "laugh", "many", "grin"};

}  // namespace

// Tags a sender can give a card, after capri-desktop's event kinds
const CapriCharacter::Style CapriCharacter::kStyles[int(Tag::Count)] = {
    {"info", "SYNC", 0, kFaceCalm, -1, audio::Cue::Tick, Lines::LeadInfo},
    {"alert", "WAITING", 0xff2152, kFaceMany, kCutinMany, audio::Cue::Alert, Lines::LeadAlert},
    {"done", "DONE", 0x36ff7a, kFaceLook, kCutinGrin, audio::Cue::Done, Lines::LeadDone},
    {"error", "ERROR", 0xff2152, kFaceLaugh, kCutinLaugh, audio::Cue::Error, Lines::LeadError},
    {"work", "RUNNING", 0x36ff7a, kFaceCalm, -1, audio::Cue::Tick, Lines::LeadWork},
    {"remind", "REMINDER", 0x3d6bff, kFaceLook, kCutinEyes, audio::Cue::Alert, Lines::LeadRemind},
    {"idle", "IDLE", 0x9b5cff, kFaceSmug, -1, audio::Cue::Tick, Lines::LeadIdle},
    {"start", "ONLINE", 0x36ff7a, kFaceGrin, kCutinGrin, audio::Cue::Done, Lines::LeadStart},
    {"end", "OFFLINE", 0x9b5cff, kFaceAside, -1, audio::Cue::Tick, Lines::LeadEnd},
    {"chat", "CAPRI", 0, kFaceLook, -1, audio::Cue::Blip, Lines::LeadInfo},
};

// ---- Settings ----

void CapriCharacter::mergeSettings(JsonObjectConst patch) { mergeJson(patch, edited_); }
void CapriCharacter::settingsToJson(JsonObject out) const { toJson(edited_, out); }

void CapriCharacter::describe(JsonObject out) const {
  JsonArray langs = out["langs"].to<JsonArray>();
  for (int i = 0; i < int(Lang::Count); i++) langs.add(langName(Lang(i)));
  JsonArray tags = out["tags"].to<JsonArray>();
  for (const Style& s : kStyles) tags.add(s.name);
  JsonArray faces = out["faces"].to<JsonArray>();
  for (const char* f : kFaceNames) faces.add(f);
  out["presets"].to<JsonArray>();
}

// ---- Setup ----

void CapriCharacter::attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) {
  width_ = screenW;
  height_ = screenH;
  rng_ = seed ? seed : 1;
  sky_.configure(gfx::Rect(0, 0, screenW, screenH), rng_);
  comp.add(&sky_, z + kZSky);
  comp.add(&figure_, z + kZFigure);
  comp.add(&panel_, z + kZPanel);
  comp.add(&cutin_, z + kZCutin);
  takeSettings();
  layout();
  applyPalettes();
  applySettings();
  setActive(false);
}

void CapriCharacter::layout() {
  // The widest figure that fits, in the upper part; the panel takes the bottom over her hands
  size_ = kFigureSizes - 1;
  for (int i = 0; i < kFigureSizes; i++) {
    if (kFigure[i][0].w <= width_ && kFigure[i][0].h <= height_ - 40) {
      size_ = i;
      break;
    }
  }
  const int fw = kFigure[size_][0].w, fh = kFigure[size_][0].h;
  const bool tall = height_ * 4 > width_ * 5, square = !tall && width_ * 4 <= height_ * 5;
  const int h = tall ? 172 : square ? std::min(124, height_ / 2) : std::min(150, height_ / 2);
  const gfx::Rect box(6, height_ - h - 6 - 5, width_ - 17, h);
  figX_ = (width_ - fw) / 2;
  figY_ = tall ? std::max(8, (box.y - fh) / 2) : 6;
  cutinX_ = width_ - kCutinW - 6 - 10;
  cutinY_ = 8;
  panel_.configure(box, box.w >= 250, true);
  figure_.configure(kFigure[size_], kCelChanged[size_], kFigureNeck[size_], gfx::SpanPalette{main_, kFigurePaletteOpacity},
                    gfx::SpanPalette{ghost_, ghostOpacity_}, gfx::Rect(0, 0, width_, height_));
}

void CapriCharacter::applyPalettes() {
  for (int i = 0; i < 256; i++) {
    const uint8_t* rgb = kFigurePaletteRgb + 3 * i;
    float c[3] = {float(rgb[0]), float(rgb[1]), float(rgb[2])};
    if (dimmed_) {  // asleep she fades into the dark (as on the desktop, at 0.55)
      static const float kNight[3] = {8, 7, 16};
      for (int k = 0; k < 3; k++) c[k] = c[k] * 0.55f + kNight[k] * 0.45f;
    }
    gfx::ditherColor(c[0], c[1], c[2], main_ + i);
    // The afterimage: her shading in cyan or red, at half her opacity
    const float luma = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
    const float tint[3] = {ghostRed_ ? 255.0f : 40.0f, ghostRed_ ? 40.0f : 240.0f, ghostRed_ ? 80.0f : 255.0f};
    float g[3];
    for (int k = 0; k < 3; k++) g[k] = std::min(255.0f, tint[k] * 0.75f + luma * 0.35f);
    gfx::ditherColor(g[0], g[1], g[2], ghost_ + i);
    ghostOpacity_[i] = uint8_t(kFigurePaletteOpacity[i] / 2);
  }
  figure_.refresh();
}

void CapriCharacter::applySettings() {
  if (box_ != Box::Closed && talk_ == Talk::Self) {
    accent_ = settings_.accent;
    panel_.setStyle(accent_, badge_, &kFaces[face_]);
  }
}

bool CapriCharacter::syncSettings() {
  if (!takeSettings()) return false;
  applySettings();
  return true;
}

void CapriCharacter::setActive(bool active) {
  active_ = active;
  sky_.setVisible(active);
  figure_.setVisible(active);
  panel_.setVisible(false);
  cutin_.hide();
  box_ = Box::Closed;
  openAtMs_ = waveMs_ = happyUntilMs_ = swingUntilMs_ = introMs_ = 0;
  sky_.setEdges(0);
  cardId_ = 0;  // a card already up is staged afresh
  cardUp_ = false;
  cutinShown_ = pendingCutin_ = -1;
  started_ = greeted_ = false;
  waitBrainUntilMs_ = 0;
  nudgeOpen_ = false;
  soundCount_ = 0;
  ask_[0] = speech_[0] = '\0';
  sky_.setSatellite(-1, 0);
  satMs_ = 0;
  if (asleep_ || dimmed_) {
    asleep_ = dimmed_ = false;
    applyPalettes();
  }
}

// ---- Cards ----

CapriCharacter::Tag CapriCharacter::tagOf(const engine::CardContent& c) const {
  for (int i = 0; i < int(Tag::Count); i++) {
    if (c.tag[0] && strcmp(c.tag, kStyles[i].name) == 0) return Tag(i);
  }
  return c.kind == engine::CardKind::Prompt ? Tag::Alert : Tag::Info;
}

Face CapriCharacter::faceNamed(const char* name, Face fallback) const {
  for (int i = 0; i < kFaceCount; i++) {
    if (strcmp(name, kFaceNames[i]) == 0) return Face(i);
  }
  return fallback;
}

void CapriCharacter::present(const engine::CardState& card, uint32_t now) {
  if (!active_) return;
  if (card.id != cardId_) {
    cardId_ = card.id;
    if (card.visible) {
      card_ = card.content;
      cardUp_ = true;
      onCard(now);
    }
  } else if (cardUp_ && !card.visible) {
    cardUp_ = false;
    onCardGone(now);
  }
  if (cardUp_ && card.chosen >= 0 && answer_ < 0) answer_ = card.chosen;
}

void CapriCharacter::onCard(uint32_t now) {
  wake(now);
  tag_ = tagOf(card_);
  const Style& st = kStyles[int(tag_)];
  const Face face = faceNamed(card_.face, st.face);
  // Her brain's cards are her own words; anyone else's news gets her lead line first
  char text[260];
  if (tag_ == Tag::Chat) {
    waitBrainUntilMs_ = 0;
    snprintf(text, sizeof(text), "%s", card_.body);
    speakRef(card_.ref);
  } else {
    char lead[96], spoken[96];
    picker_.pick(st.lead, settings_.lang, settings_.call, rand32(), lead, sizeof(lead), spoken, sizeof(spoken));
    snprintf(text, sizeof(text), card_.body[0] ? "%s\n%s" : "%s", lead, card_.body);
    speak(spoken);
  }
  answer_ = -1;
  pressedMs_ = 0;
  reacted_ = false;
  int cut = st.cutin;
  if (tag_ == Tag::Chat) cut = face == kFaceLaugh ? kCutinLaugh : face == kFaceGrin ? kCutinGrin : -1;
  pendingCutin_ = settings_.cutins ? cut : -1;
  if (tag_ == Tag::Start) waveMs_ = now;  // hello, as at boot
  const bool prompt = card_.kind == engine::CardKind::Prompt;
  open(prompt ? Talk::Question : Talk::Message, card_.title, text, prompt, st.accent ? hexColor(st.accent) : settings_.accent,
       st.badge, face, now);
  sound(st.cue);
}

void CapriCharacter::onCardGone(uint32_t now) {
  if (talk_ == Talk::Message) closeBox(now);
  else if (talk_ == Talk::Question && !reacted_) react(answer_, now);  // answered, or expired (-1)
}

void CapriCharacter::react(int answer, uint32_t now) {
  reacted_ = true;
  char line[160];
  char spoken[160];
  if (tag_ == Tag::Chat && answer >= 0) {  // a reply to her brain: it answers with the next card
    picker_.pick(Lines::Thinking, settings_.lang, settings_.call, rand32(), line, sizeof(line), spoken, sizeof(spoken));
    speak(spoken);
    waitBrainUntilMs_ = now + kBrainWaitMs;
    waitCheckIn_ = false;
    open(Talk::Reaction, "", line, false, accent_, "CAPRI", kFaceCalm, now);
    return;
  }
  const char* option = answer >= 0 && answer < card_.optionCount && card_.options[answer][0] ? card_.options[answer] : "OK";
  const Lines set = answer == 0 ? Lines::AnswerYes : answer > 0 ? Lines::AnswerNo : Lines::AnswerNone;
  char pattern[160], spokenPattern[160];
  picker_.pick(set, settings_.lang, settings_.call, rand32(), pattern, sizeof(pattern), spokenPattern,
               sizeof(spokenPattern));
  if (strstr(pattern, "%s")) snprintf(line, sizeof(line), pattern, option);
  else snprintf(line, sizeof(line), "%s", pattern);
  if (strstr(spokenPattern, "%s")) snprintf(spoken, sizeof(spoken), spokenPattern, option);
  else snprintf(spoken, sizeof(spoken), "%s", spokenPattern);
  speak(spoken);
  Face face = kFaceAside;
  if (answer == 0) {
    face = kFaceGrin;
    hopMs_ = now;
    sound(audio::Cue::Done);
  } else if (answer < 0) {
    face = kFaceWide;
    sound(audio::Cue::Tick);
  }
  open(Talk::Reaction, "", line, false, accent_, answer >= 0 ? "SENT" : "TIMEOUT", face, now);
}

void CapriCharacter::sayLine(Lines set, Face face, uint32_t now) {
  if (cardUp_) return;  // the card has the panel
  char line[160], spoken[160];
  picker_.pick(set, settings_.lang, settings_.call, rand32(), line, sizeof(line), spoken, sizeof(spoken));
  // The greetings have her own recorded voice (sound clips); every other line her brain voices
  if (set != Lines::HelloMorning && set != Lines::HelloLate && set != Lines::HelloMiss) speak(spoken);
  open(Talk::Self, "", line, false, settings_.accent, "CAPRI", face, now);
}

void CapriCharacter::open(Talk talk, const char* sys, const char* text, bool withChoices, gfx::Color accent,
                          const char* badge, Face face, uint32_t now) {
  talk_ = talk;
  snprintf(sys_, sizeof(sys_), "%s", sys);
  snprintf(text_, sizeof(text_), "%s", text);
  snprintf(badge_, sizeof(badge_), "%s", badge);
  accent_ = accent;
  face_ = face;
  withChoices_ = withChoices;
  choicesUp_ = false;
  typedMs_ = 0;
  lastTyped_ = 0;
  panel_.setStyle(accent, badge, &kFaces[face]);
  panel_.setMessage(sys, text, withChoices);
  panel_.setChoices(card_.options, withChoices ? card_.optionCount : 0);
  if (face == kFaceGrin || face == kFaceLaugh) happyUntilMs_ = now + kHappyMs;
  if (box_ == Box::Closed || box_ == Box::Closing) {
    if (!openAtMs_) {
      openAtMs_ = now + kOpenDelayMs;
      glitch(250, now);  // the panel transmits in with a crackle of her
      sound(audio::Cue::Glitch);
    }
  } else {  // already up: the new text types out in place
    box_ = Box::Open;
    panel_.setOpen(1);
    textMs_ = now;
    flash(pendingCutin_, now);
    pendingCutin_ = -1;
  }
}

void CapriCharacter::closeBox(uint32_t now) {
  openAtMs_ = 0;
  if (box_ == Box::Closed || box_ == Box::Closing) return;
  box_ = Box::Closing;
  boxMs_ = now;
}

int CapriCharacter::typed(uint32_t now) const {
  return int(since(now, textMs_) / std::max<uint32_t>(1, settings_.textMs));
}

bool CapriCharacter::typing(uint32_t now) const { return box_ == Box::Open && typed(now) < panel_.length(); }

void CapriCharacter::updateBox(uint32_t now) {
  if (openAtMs_ && reached(now, openAtMs_)) {
    openAtMs_ = 0;
    box_ = Box::Opening;
    boxMs_ = now;
    panel_.setOpen(0);
    panel_.setVisible(true);
    flash(pendingCutin_, now);
    pendingCutin_ = -1;
  }
  if (box_ == Box::Closed) return;
  if (box_ == Box::Opening) {
    const float u = float(since(now, boxMs_)) / kOpenMs;
    panel_.setOpen(u);
    if (u >= 1) {
      box_ = Box::Open;
      textMs_ = now;
    }
  } else if (box_ == Box::Closing) {
    const float u = float(since(now, boxMs_)) / kCloseMs;
    panel_.setOpen(1 - u);
    if (u >= 1) {
      box_ = Box::Closed;
      panel_.setVisible(false);
      return;
    }
  }

  // Characters out so far, never past the end: blips stop with the last one
  const int n = box_ == Box::Open ? std::min(typed(now), panel_.length()) : (box_ == Box::Closing ? panel_.length() : 0);
  panel_.setTyped(n, now / 50);
  // Blips while she types, unless her voice says the words (it did last time)
  if (!(settings_.voice && voiceReady_) && box_ == Box::Open && n > lastTyped_ && since(now, lastBlipMs_) >= kBlipMs) {
    sound(audio::Cue::Blip);
    lastBlipMs_ = now;
  }
  lastTyped_ = n;
  const bool done = box_ == Box::Open && n >= panel_.length();
  if (done && !typedMs_) typedMs_ = now;
  panel_.setCursor(done && talk_ != Talk::Question && ((now / 450) & 1));
  panel_.setBadgeOn(!(talk_ == Talk::Question || tag_ == Tag::Alert) || talk_ == Talk::Self ||
                    talk_ == Talk::Reaction || ((now / 900) & 1) == 0);

  if (talk_ == Talk::Question && done) {
    const float shown = float(int32_t(since(now, typedMs_)) - int32_t(kChoicesDelayMs)) / kChoicesMs;
    panel_.showChoices(shown);
    choicesUp_ = shown >= 1;
  }
  if (talk_ == Talk::Question && answer_ >= 0) {
    if (!pressedMs_) {
      pressedMs_ = now;
      panel_.setPressed(answer_);
    }
    if (since(now, pressedMs_) >= kPressedMs) react(answer_, now);
    return;
  }
  if (done && talk_ == Talk::Self && since(now, typedMs_) > kSelfHoldMs) closeBox(now);
  if (done && talk_ == Talk::Reaction && since(now, typedMs_) > kReactionHoldMs) closeBox(now);
}

// ---- Her own life ----

void CapriCharacter::updateLife(uint32_t now) {
  const bool free = box_ == Box::Closed && !cardUp_ && !openAtMs_;
  if (introMs_) return;  // her entrance has the stage
  if (!greeted_ && since(now, startMs_) >= kGreetMs && free) greet(now);


  if (waitBrainUntilMs_ && reached(now, waitBrainUntilMs_)) {
    waitBrainUntilMs_ = 0;
    if (!cardUp_) sayLine(waitCheckIn_ ? Lines::CheckIn : Lines::Offline, waitCheckIn_ ? kFaceLook : kFaceLaugh, now);
  }
  if (!asleep_ && free && !waitBrainUntilMs_ && since(now, lastActiveMs_) > uint32_t(settings_.sleepAfter) * 1000) {
    asleep_ = dimmed_ = true;
    applyPalettes();
  }
  if (settings_.chatter && reached(now, nextCheckInMs_)) {
    nextCheckInMs_ = now + uint32_t(settings_.chatter) * 60000u * uint32_t(70 + rand32() % 61) / 100;
    if (free) {  // her brain says something new; her own line if it does not answer
      wake(now);
      if (settings_.brain) {
        ask("checkin", now);
        waitCheckIn_ = true;
      } else {
        sayLine(Lines::CheckIn, kFaceLook, now);
      }
    }
  }
  if (!nudgedMs_ && free && since(now, lastTouchMs_) > kNudgeAfterMs) {
    nudgedMs_ = now;
    nudgeOpen_ = true;
    wake(now);
    sayLine(Lines::Nudge, kFaceSmug, now);
  }
  if (nudgeOpen_ && since(now, nudgedMs_) > kNudgeAnswerMs) {
    nudgeOpen_ = false;
    if (free) sayLine(Lines::NudgeIgnored, kFaceAside, now);
  }
  if (free && !asleep_ && reached(now, nextIdleMs_)) {
    nextIdleMs_ = now + 9000 + rand32() % 12000;
    idleAction(now);
  }
  // Glitches of her own: every 30-60 s at the default setting, more often the higher it is
  if (settings_.glitch && reached(now, nextGlitchMs_)) {
    nextGlitchMs_ = now + uint32_t(1500000 / settings_.glitch) * uint32_t(100 + rand32() % 101) / 100;
    if (!asleep_) {
      glitch(600, now);
      sound(audio::Cue::Glitch);
    }
  }
}

void CapriCharacter::greet(uint32_t now) {
  // Greeted by the hour, with her voice clip (lachydotmcg/capri-desktop)
  greeted_ = true;
  const int m = minute_;
  Lines set = Lines::HelloMiss;
  int clip = kClipHelloMiss;
  if (m >= 5 * 60 && m < 12 * 60) set = Lines::HelloMorning, clip = kClipHelloMorning;
  else if (m >= 0 && (m >= 22 * 60 || m < 5 * 60)) set = Lines::HelloLate, clip = kClipHelloLate;
  sound(audio::Cue::Clip, &kClips[clip]);
  sayLine(set, kFaceGrin, now);
}

void CapriCharacter::wake(uint32_t now) {
  lastActiveMs_ = now;
  if (!asleep_) return;
  asleep_ = dimmed_ = false;
  applyPalettes();
}

bool CapriCharacter::touched(uint32_t now) {
  lastTouchMs_ = now;
  nudgedMs_ = 0;
  const bool answered = nudgeOpen_;
  nudgeOpen_ = false;
  wake(now);
  return answered;
}

void CapriCharacter::flash(int cutin, uint32_t now) {
  if (cutin < 0) return;
  cutinShown_ = cutin;
  cutinMs_ = now;
  cutin_.show(kCutins[cutin], cutinX_, cutinY_);
}

void CapriCharacter::glitch(uint32_t ms, uint32_t now) {
  glitchUntilMs_ = now + ms;
  ghostRed_ = !ghostRed_;
  applyPalettes();
}

void CapriCharacter::sound(audio::Cue cue, const audio::Clip* clip) {
  if (!settings_.sound || cue == audio::Cue::None || soundCount_ == int(sizeof(sounds_) / sizeof(sounds_[0]))) return;
  sounds_[soundCount_++] = audio::Sound{cue, settings_.volume, clip};
}

void CapriCharacter::ask(const char* type, uint32_t now) {
  snprintf(ask_, sizeof(ask_), "{\"type\":\"%s\",\"lang\":\"%s\"}", type, langName(settings_.lang));
  waitBrainUntilMs_ = now + kBrainWaitMs;
  waitCheckIn_ = false;
}

void CapriCharacter::speak(const char* english) {
  if (!settings_.voice || !*english) return;
  JsonDocument doc;
  doc["text"] = english;
  serializeJson(doc, speech_, sizeof(speech_));
}

void CapriCharacter::speakRef(uint32_t ref) {
  if (settings_.voice && ref) snprintf(speech_, sizeof(speech_), "{\"ref\":%u}", unsigned(ref));
}

bool CapriCharacter::takeSpeech(char* out, size_t size) {
  if (!speech_[0]) return false;
  snprintf(out, size, "%s", speech_);
  speech_[0] = '\0';
  return true;
}

// ---- Frame ----

void CapriCharacter::update(engine::Mood, uint32_t, uint32_t now) {
  if (!active_) return;
  lastMs_ = now;
  if (!started_) {
    started_ = true;
    startMs_ = lastActiveMs_ = lastTouchMs_ = now;
    nextGlitchMs_ = now + 8000;
    nextCheckInMs_ = now + uint32_t(settings_.chatter) * 60000u;
    nextIdleMs_ = now + kIntroEnd + 6000;
    introMs_ = now;  // she makes her entrance each time she appears
    nextSatMs_ = now + 20000 + rand32() % 40000;
    poseMs_ = now - kPoseStepMs;
  }
  updateLife(now);
  updateBox(now);
  updatePose(now);
  updateSky(now);
  sky_.setTalk(settings_.brain && box_ == Box::Closed && !openAtMs_ && !introMs_, waitBrainUntilMs_ != 0);
}

void CapriCharacter::idleAction(uint32_t now) {
  const uint32_t r = rand32() % 100;
  if (r < 25) waveMs_ = now;                       // a little wave
  else if (r < 45) happyUntilMs_ = now + kHappyMs;  // a happy squint
  else if (r < 65) mumbleUntilMs_ = now + 900;      // mumbles to herself
  else if (r < 82) hopMs_ = now;                    // a hop
  else glitch(450, now);
}

bool CapriCharacter::intro(uint32_t now, FigureLayer::Pose& p, int& cel) {
  const uint32_t t = since(now, introMs_);
  const int w = kFigure[size_][0].w, h = kFigure[size_][0].h;
  const float px = float(w) / 1672.0f;  // capri-desktop's numbers are on her 1672 px picture
  p = FigureLayer::Pose();
  p.x = figX_;
  p.y = figY_;
  cel = kCelGrab;
  sky_.setEdges(t < kIntroReach ? 1 + t / (2 * kBeatMs) : 0);  // crackling until her hands are on them
  if (t >= kIntroEnd) {  // in place, at rest: her usual pose takes over at once
    introMs_ = 0;
    poseMs_ = now - kPoseStepMs;
    return false;
  }
  if (t < kIntroHands) {  // nothing of her yet
    p.rowsTo = 0;
  } else if (t < kIntroReach) {  // her hands slam onto the edges, left then right, the screen jolting
    p.rowsFrom = int16_t(h * 52 / 100);
    p.rowsTo = int16_t(h * 72 / 100);
    const uint32_t g = (t - (t >= kIntroRight ? kIntroRight : kIntroHands)) / kBeatMs;
    p.x += int(lrintf((g == 0 ? 7 : g == 1 ? -3 : 0) * px * 3 * (t >= kIntroRight ? 1 : -1)));
    p.y += int(lrintf(70 * px));
  } else if (t < kIntroGrab) {  // she hauls herself up along them, faster and faster
    const float u = float(t - kIntroReach) / float(kIntroGrab - kIntroReach), e = u * u;
    p.rowsFrom = int16_t(h * 52 / 100 * (1 - e));
    p.rowsTo = int16_t(h * 72 / 100 + (h - h * 72 / 100) * e);
    p.y += int(lrintf(70 * px * (1 - e)));
  } else if (t < kIntroHold) {  // her face hits the glass: a shake, her colours apart, settling
    const uint32_t g = std::min<uint32_t>(4, (t - kIntroGrab) / kBeatMs);
    p.x += int(lrintf(kImpact[g] * px * 2));
    p.y -= int(lrintf(kImpact[g] * px * 0.8f));
    p.ghostDx = int(lrintf((16 - int(g) * 4) * px * 3));
    p.ghostY0 = 0;
    p.ghostY1 = int16_t(h);
    if (t - kIntroGrab < kBeatMs) sound(audio::Cue::Glitch);
  } else {  // holding on: hi, a wave, back onto the edge, a beam
    if (!greeted_ && t >= kIntroHi) greet(now);
    if (t >= kIntroHi && t < kIntroHi + 10 * kBeatMs) cel = kSayHi[(t - kIntroHi) / kBeatMs];
    if (t >= kIntroWave && t < kIntroWave + 2 * kBeatMs) cel = kCelSmear;  // her arm off the edge
    else if (t >= kIntroWave + 2 * kBeatMs && t < kIntroRegrab)
      cel = kIntroWaveCels[std::min<uint32_t>(4, (t - kIntroWave - 2 * kBeatMs) / (3 * kBeatMs))];
    else if (t >= kIntroRegrab && t < kIntroHappy) cel = kCelSmear;  // and back onto it
    else if (t >= kIntroHappy) cel = kCelHappy;
    if (t >= kIntroHappy && t < kIntroHappy + 2 * kBeatMs) p.x += int(lrintf((t < kIntroHappy + kBeatMs ? 8 : -4) * px * 2));
    const uint32_t h0 = t >= kIntroHi + 2 * kBeatMs ? (t - kIntroHi - 2 * kBeatMs) / kBeatMs : 99;
    if (h0 < 8) p.y -= (h0 < 2 ? 3 : h0 < 4 ? -2 : h0 < 6 ? 1 : 0);  // her head bobbing as she says it
  }
  return true;
}

int CapriCharacter::cel(uint32_t now) const {
  if (swingUntilMs_ && int32_t(swingUntilMs_ - now) > 0) return kCelSmear;
  if (waveMs_ && since(now, waveMs_) < kWaveStepMs * 6) return kWave[since(now, waveMs_) / kWaveStepMs];
  if (happyUntilMs_ && int32_t(happyUntilMs_ - now) > 0 && !typing(now)) return kCelHappy;
  // Her mouth moves while her text decodes (and while she mumbles)
  if (typing(now) || (mumbleUntilMs_ && int32_t(mumbleUntilMs_ - now) > 0)) return kTalk[(now / kMouthStepMs) & 3];
  return kCelGrab;
}

void CapriCharacter::updatePose(uint32_t now) {
  // The cut-in: bands settle in, it stays a moment, then goes
  if (cutinShown_ >= 0) {
    cutin_.setReveal(float(since(now, cutinMs_)) / kCutinRevealMs);
    if (since(now, cutinMs_) > kCutinMs) {
      cutin_.hide();
      cutinShown_ = -1;
    }
  }
  int introCel = 0;
  if (introMs_ && intro(now, pose_, introCel)) {
    pose_.cel = introCel;
    figure_.setPose(pose_);
    return;
  }
  // Cels every frame (only their rows redraw); the rest of the pose 15 times a second
  pose_.cel = cel(now);
  if (since(now, poseMs_) < kPoseStepMs) {
    figure_.setPose(pose_);
    return;
  }
  poseMs_ = now;

  if (++bobHeld_ >= kBobHold[bobStep_] * (asleep_ ? 2 : 1)) {
    bobHeld_ = 0;
    bobStep_ = (bobStep_ + 1) & 3;
  }
  float hop = 0;
  if (hopMs_ && since(now, hopMs_) < kHopMs) hop = 5 * sinf(kPi * float(since(now, hopMs_)) / kHopMs);

  FigureLayer::Pose& p = pose_;
  const int h = kFigure[size_][0].h;
  p.x = figX_;
  p.y = figY_ + kBob[bobStep_] + (asleep_ ? 4 : 0) - int(lrintf(hop));
  p.sway = int(lrintf((asleep_ ? 0.5f : 1.5f) * sinf(2 * kPi * float(now % 3100) / 3100.0f)));

  // Glitching: bands of her slide aside and a tinted copy lags behind
  for (FigureLayer::Slice& s : p.slices) s = FigureLayer::Slice();
  p.ghostDx = 0;
  if (glitching(now)) {
    const int strength = 6 + int(rand32() % 10);
    for (FigureLayer::Slice& s : p.slices) {
      s.y0 = int16_t(rand32() % uint32_t(h));
      s.y1 = int16_t(s.y0 + 6 + rand32() % 30);
      s.dx = int16_t(int(rand32() % uint32_t(2 * strength + 1)) - strength);
    }
    p.ghostDx = (rand32() & 1 ? 1 : -1) * (3 + strength / 3);
    p.ghostY0 = int16_t(rand32() % uint32_t(h));
    p.ghostY1 = int16_t(p.ghostY0 + h / 4 + rand32() % uint32_t(h / 2));
  }
  figure_.setPose(p);
}

void CapriCharacter::updateSky(uint32_t now) {
  // The satellite she dreams of reaching crosses the sky now and then
  if (!satMs_ && reached(now, nextSatMs_)) satMs_ = now;
  if (!satMs_) return;
  const float u = float(since(now, satMs_)) / kSatelliteMs;
  if (u >= 1) {
    satMs_ = 0;
    nextSatMs_ = now + 60000 + rand32() % 120000;
    sky_.setSatellite(-1, 0);
    return;
  }
  sky_.setSatellite(int(-10 + (width_ + 20) * u), int(height_ * (0.10f + 0.08f * u)));
}

void CapriCharacter::previewExpression(Expression e, uint32_t) {
  const uint32_t now = lastMs_;
  wake(now);
  switch (e) {
    case Expression::Happy: flash(kCutinGrin, now), hopMs_ = now, happyUntilMs_ = now + kHappyMs; break;
    case Expression::Surprised: flash(kCutinEyes, now); break;
    case Expression::Sleepy:
      asleep_ = dimmed_ = true;
      applyPalettes();
      break;
    case Expression::Angry: flash(kCutinMany, now), glitch(600, now); break;
    case Expression::Sad: flash(kCutinLook, now); break;
    default: break;
  }
}

bool CapriCharacter::glitching(uint32_t now) const { return glitchUntilMs_ && int32_t(glitchUntilMs_ - now) > 0; }

float CapriCharacter::randf() { return float(rand32() & 0xFFFFFF) / float(0x1000000); }

uint32_t CapriCharacter::rand32() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

// ---- Input ----

bool CapriCharacter::hitTest(int x, int y) const {
  if (!active_) return false;
  if (box_ != Box::Closed && panel_.box().contains(x, y)) return false;
  if (cutinShown_ >= 0 && cutin_.bounds().contains(x, y)) return false;
  return figure_.contains(x, y);
}

void CapriCharacter::poke(float, float y) {
  const uint32_t now = lastMs_;
  const bool wasAsleep = asleep_;
  if (touched(now)) {
    sayLine(Lines::NudgeAnswered, kFaceGrin, now);
    return;
  }
  if (wasAsleep) {
    sayLine(Lines::Wake, kFaceAside, now);
    return;
  }
  const FigureLayer::Pose& p = figure_.pose();
  if (y < p.y + kFigureNeck[size_] + 10) {  // on her head: a pat, and a happy squint
    hopMs_ = now;
    happyUntilMs_ = now + kHappyMs;
    if (settings_.cutins && randf() < 0.25f) flash(kCutinGrin, now);
    sayLine(Lines::Pet, kFaceGrin, now);
  } else {
    swingUntilMs_ = now + kSwingMs;  // she swings an arm at the poke
    glitch(400, now);
    sound(audio::Cue::Glitch);
    sayLine(Lines::Poke, kFaceWide, now);
  }
}

bool CapriCharacter::longPress(float, float) {
  const uint32_t now = lastMs_;
  touched(now);
  if (settings_.brain) {
    ask("talk", now);
    sayLine(Lines::Thinking, kFaceCalm, now);
  } else {
    sayLine(Lines::CheckIn, kFaceLook, now);
  }
  return true;
}

bool CapriCharacter::handleTap(int x, int y, engine::Event& out) {
  if (!active_) return false;
  const uint32_t now = lastMs_;
  if (box_ != Box::Closed && panel_.box().contains(x, y)) {
    touched(now);
    if (talk_ == Talk::Question && cardUp_ && answer_ < 0) {
      const int option = panel_.choiceAt(x, y);
      if (option >= 0) {
        out.type = engine::EventType::TapOption;
        out.value = option;
        panel_.setPressed(option);
        return true;
      }
    }
    if (typing(now)) {  // a tap decodes the rest at once
      textMs_ = now - uint32_t(panel_.length()) * settings_.textMs;
      return false;
    }
    if (talk_ == Talk::Message && cardUp_) {
      out.type = engine::EventType::TapCard;
      return true;
    }
    if (talk_ == Talk::Self || talk_ == Talk::Reaction) closeBox(now);
    return false;
  }
  if (settings_.brain && box_ == Box::Closed && sky_.talkRect().inflate(6).contains(x, y)) {
    longPress(float(x), float(y));  // the chip does what a long press on her does
    return false;
  }
  if (hitTest(x, y)) {
    out.type = engine::EventType::TapCharacter;
    poke(float(x), float(y));
    return true;
  }
  touched(now);
  return false;
}

bool CapriCharacter::busy(uint32_t now) const {
  const bool sliding = box_ == Box::Opening || box_ == Box::Closing || openAtMs_;
  const bool choicesRising = talk_ == Talk::Question && typedMs_ && !choicesUp_ && box_ == Box::Open;
  const bool moving = introMs_ || (hopMs_ && since(now, hopMs_) < kHopMs) || glitching(now) || int32_t(mumbleUntilMs_ - now) > 0 ||
                      (waveMs_ && since(now, waveMs_) < kWaveStepMs * 6) || int32_t(swingUntilMs_ - now) > 0;
  const bool cutting = cutinShown_ >= 0 && since(now, cutinMs_) < kCutinRevealMs;
  return sliding || typing(now) || choicesRising || moving || cutting;
}

// ---- Outbox ----

bool CapriCharacter::takeAsk(char* out, size_t size) {
  if (!ask_[0]) return false;
  snprintf(out, size, "%s", ask_);
  ask_[0] = '\0';
  return true;
}

audio::Sound CapriCharacter::takeSound() {
  if (!soundCount_) return audio::Sound{};
  const audio::Sound s = sounds_[0];
  for (int i = 1; i < soundCount_; i++) sounds_[i - 1] = sounds_[i];
  soundCount_--;
  return s;
}

}  // namespace ui::capri
