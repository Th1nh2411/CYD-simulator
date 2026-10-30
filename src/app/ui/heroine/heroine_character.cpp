#include "app/ui/heroine/heroine_character.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/ui/heroine/heroine_json.h"

namespace ui::heroine {

namespace {

constexpr float kPi = 3.14159265f;

// Layer depths above the character's base z
enum Depth : int { kZRoom = 0, kZPortrait = 20, kZDialog = 50 };

constexpr uint32_t kOpenDelayMs = 300;  // she notices first, then the box comes up
constexpr uint32_t kBoxSlideMs = 200;
constexpr uint32_t kChoicesDelayMs = 120, kChoicesMs = 220;
constexpr uint32_t kPressedMs = 200;        // a picked choice stays lit before she answers
constexpr uint32_t kReactionHoldMs = 1600;  // her answer stays up this long once typed
constexpr uint32_t kPatMs = 1600, kPokeMs = 1500, kStartleMs = 300;
constexpr uint32_t kHopMs = 320, kBlinkMs = 140, kMouthStepMs = 90;
constexpr int kBoxH = 84, kBoxMargin = 6;
// Her pose changes at most 12 times a second ("on twos", as in hand-drawn
// animation), in whole pixels and painted tilts
constexpr uint32_t kPoseStepMs = 83;
constexpr float kBreathLift = 2;  // px her shoulders rise breathing in
constexpr int kMaxLift = 3;

uint32_t since(uint32_t now, uint32_t t) { return int32_t(now - t) > 0 ? now - t : 0; }
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float easeOut(float u) {
  u = clamp01(u);
  return 1 - (1 - u) * (1 - u) * (1 - u);
}

// Damped spring toward a target, in small steps
void spring(float& x, float& v, float target, float w, float zeta, float dt) {
  constexpr float kStep = 1.0f / 120;
  for (float left = dt; left > 0; left -= kStep) {
    const float h = std::min(left, kStep);
    v += (w * w * (target - x) - 2 * zeta * w * v) * h;
    x += v * h;
  }
}

// A cycle of period ms: 0 .. 1 .. 0 as a sine, phase from the clock
float wave(uint32_t now, uint32_t period) { return sinf(2 * kPi * float(now % period) / float(period)); }

}  // namespace

// ---- Settings ----

void HeroineCharacter::mergeSettings(JsonObjectConst patch) { mergeJson(patch, edited_); }
void HeroineCharacter::settingsToJson(JsonObject out) const { toJson(edited_, out); }

void HeroineCharacter::describe(JsonObject out) const {
  JsonArray times = out["times"].to<JsonArray>();
  for (int i = 0; i < int(TimeOfDay::Count); i++) times.add(timeName(TimeOfDay(i)));
  out["presets"].to<JsonArray>();
}

// ---- Setup ----

void HeroineCharacter::attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) {
  width_ = screenW;
  height_ = screenH;
  rng_ = seed ? seed : 1;
  // The room in the middle of the screen; she stands at the bottom center
  room_.configure(gfx::Rect(0, 0, screenW, screenH));
  baseX_ = (screenW - kHeroineWidth) / 2;
  baseY_ = screenH - kHeroineShownHeight;  // the picture goes on below the screen
  portrait_.configure(palette_, gfx::Rect(0, 0, screenW, screenH));

  comp.add(&room_, z + kZRoom);
  comp.add(&portrait_, z + kZPortrait);
  comp.add(&dialog_, z + kZDialog);
  takeSettings();
  applySettings();
  setActive(false);
}

gfx::Rect HeroineCharacter::boxRect() const {
  return gfx::Rect(kBoxMargin, height_ - kBoxMargin - kBoxH, width_ - 2 * kBoxMargin, kBoxH);
}

void HeroineCharacter::applySettings() {
  dialog_.configure(boxRect(), settings_.accent, settings_.name);
  // Her picture's palette, with saturation and gamma for the panel
  for (int i = 0; i < 256; i++) {
    const uint8_t* rgb = kHeroinePaletteRgb + 3 * i;
    const float luma = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
    float c[3];
    for (int k = 0; k < 3; k++) {
      const float v = std::max(0.0f, std::min(255.0f, luma + (rgb[k] - luma) * settings_.saturation));
      c[k] = 255.0f * powf(v / 255.0f, settings_.gamma);
    }
    gfx::ditherColor(c[0], c[1], c[2], palette_ + i);
  }
  portrait_.refresh();
  period_ = -1;
  applyLight();
}

void HeroineCharacter::applyLight() {
  int period = int(settings_.time) - 1;
  if (settings_.time == TimeOfDay::Auto) {
    const int m = minute_;
    period = m < 0 ? 1 : (m >= 5 * 60 && m < 10 * 60) ? 0 : (m >= 10 * 60 && m < 16 * 60 + 30) ? 1
                       : (m >= 16 * 60 + 30 && m < 19 * 60 + 30) ? 2 : 3;
  }
  if (period == period_) return;
  period_ = period;
  room_.show(kRoom[period]);  // drawn beforehand for each time of day (tools/rig/scene.py)
}

bool HeroineCharacter::syncSettings() {
  if (!takeSettings()) return false;
  applySettings();
  return true;
}

void HeroineCharacter::setClock(int minuteOfDay) {
  if (minuteOfDay == minute_) return;
  minute_ = minuteOfDay;
  applyLight();
}

void HeroineCharacter::setActive(bool active) {
  active_ = active;
  room_.setVisible(active);
  portrait_.setVisible(active);
  dialog_.setVisible(false);
  boxState_ = BoxState::Closed;
  openAtMs_ = 0;
  cardId_ = 0;  // a card already up is staged afresh
  cardUp_ = false;
  reaction_ = Reaction::None;
  asleep_ = false;
  started_ = false;
}

// ---- Cards ----

void HeroineCharacter::present(const engine::CardState& card, uint32_t now) {
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

void HeroineCharacter::onCard(uint32_t now) {
  wake(now);
  reaction_ = Reaction::None;
  preview_ = -1;
  surprisedUntilMs_ = now + 450;
  talk_ = card_.kind == engine::CardKind::Prompt ? Talk::Question : Talk::Message;
  answer_ = -1;
  pressedMs_ = 0;
  reacted_ = false;
  say(card_.title, card_.body, talk_ == Talk::Question, now);
  if (boxState_ == BoxState::Closed || boxState_ == BoxState::Closing) openAtMs_ = now + kOpenDelayMs;
}

void HeroineCharacter::onCardGone(uint32_t now) {
  if (talk_ == Talk::Message) closeBox(now);
  else if (talk_ == Talk::Question && !reacted_) react(answer_, now);  // answered, or expired (-1)
}

void HeroineCharacter::react(int answer, uint32_t now) {
  reacted_ = true;
  talk_ = Talk::Reaction;
  const char* option = answer >= 0 && answer < card_.optionCount && card_.options[answer][0] ? card_.options[answer] : "네";
  char line[96];
  if (answer == 0) {
    snprintf(line, sizeof(line), "좋아요! 「%s」, 바로 전할게요♪", option);
    reactionLook_ = Look::Happy;
    hopMs_ = now;
  } else if (answer > 0) {
    snprintf(line, sizeof(line), "「%s」… 알겠어요. 그렇게 전할게요.", option);
    reactionLook_ = Look::Normal;
  } else {
    snprintf(line, sizeof(line), "대답이 없네요… 이번 건 넘겨 둘게요.");
    reactionLook_ = Look::Sad;
  }
  say("", line, false, now);
  if (boxState_ != BoxState::Open && boxState_ != BoxState::Opening) openBox(now);
}

void HeroineCharacter::say(const char* title, const char* body, bool withChoices, uint32_t now) {
  dialog_.setMessage(title, body, withChoices);
  dialog_.setChoices(card_.options, withChoices ? card_.optionCount : 0);
  textMs_ = now;
  typedMs_ = 0;
  choicesUp_ = false;
}

void HeroineCharacter::openBox(uint32_t now) {
  openAtMs_ = 0;
  boxState_ = BoxState::Opening;
  boxMs_ = now;
  dialog_.setDrop(kBoxH + 30);
  dialog_.setVisible(true);
}

void HeroineCharacter::closeBox(uint32_t now) {
  openAtMs_ = 0;
  if (boxState_ == BoxState::Closed || boxState_ == BoxState::Closing) return;
  boxState_ = BoxState::Closing;
  boxMs_ = now;
}

int HeroineCharacter::typed(uint32_t now) const {
  return int(since(now, textMs_) / std::max<uint32_t>(1, settings_.textMs));
}

bool HeroineCharacter::typing(uint32_t now) const {
  return boxState_ == BoxState::Open && typed(now) < dialog_.length();
}

void HeroineCharacter::updateDialog(uint32_t now) {
  if (openAtMs_ && int32_t(now - openAtMs_) >= 0) openBox(now);
  if (boxState_ == BoxState::Closed) return;
  const int hidden = kBoxH + 30;
  const float u = float(since(now, boxMs_)) / kBoxSlideMs;
  if (boxState_ == BoxState::Opening) {
    dialog_.setDrop(int((1 - easeOut(u)) * hidden));
    if (u >= 1) {
      boxState_ = BoxState::Open;
      textMs_ = now;
    }
  } else if (boxState_ == BoxState::Closing) {
    dialog_.setDrop(int(clamp01(u) * clamp01(u) * hidden));
    if (u >= 1) {
      boxState_ = BoxState::Closed;
      dialog_.setVisible(false);
      return;
    }
  }

  const int n = boxState_ == BoxState::Open ? typed(now) : (boxState_ == BoxState::Closing ? dialog_.length() : 0);
  dialog_.setTyped(n);
  const bool done = boxState_ == BoxState::Open && n >= dialog_.length();
  if (done && !typedMs_) typedMs_ = now;

  if (talk_ == Talk::Question && done) {
    // The choices rise in once the question is out; a picked one lights up, then she answers
    const float shown = float(int32_t(since(now, typedMs_)) - int32_t(kChoicesDelayMs)) / kChoicesMs;
    dialog_.showChoices(shown);
    choicesUp_ = shown >= 1;
  }
  if (talk_ == Talk::Question && answer_ >= 0) {
    if (!pressedMs_) {
      pressedMs_ = now;
      dialog_.setPressed(answer_);
    }
    if (since(now, pressedMs_) >= kPressedMs) {
      react(answer_, now);
      return;  // her reply starts typing next frame
    }
  }
  dialog_.setMore(done && talk_ != Talk::Question, ((now / 350) & 1) ? 2 : 0);
  if (talk_ == Talk::Reaction && done && since(now, typedMs_) > kReactionHoldMs) closeBox(now);
}

// ---- Frame ----

void HeroineCharacter::update(engine::Mood mood, uint32_t, uint32_t now) {
  if (!active_) return;
  const float dt = started_ ? std::min(0.05f, float(since(now, lastMs_)) / 1000.0f) : 0.0f;
  lastMs_ = now;
  if (!started_) {
    started_ = true;
    lastActiveMs_ = now;
    nextBlinkMs_ = now + 1500;
    nextGlanceMs_ = now + 2500;
    nextTiltMs_ = now + 5000;
  }
  mood_ = mood;
  if (reaction_ != Reaction::None && since(now, reactionMs_) > std::max(kPatMs, kPokeMs)) reaction_ = Reaction::None;

  updateDialog(now);
  if (!asleep_ && boxState_ == BoxState::Closed && !cardUp_ && !openAtMs_ &&
      since(now, lastActiveMs_) > uint32_t(settings_.sleepAfter) * 1000) {
    asleep_ = true;
  }
  updateFace(now);
  updatePose(dt, now);
}

HeroineCharacter::Look HeroineCharacter::look(uint32_t now) const {
  if (preview_ >= 0 && int32_t(previewUntilMs_ - now) > 0) {
    static const Look kByExpression[kExpressionCount] = {Look::Normal, Look::Happy,   Look::Surprised,
                                                         Look::Sleepy, Look::Annoyed, Look::Sad};
    return kByExpression[preview_];
  }
  if (asleep_) return Look::Asleep;
  if (reaction_ == Reaction::Pat) return Look::Happy;
  if (reaction_ == Reaction::Poke) return since(now, reactionMs_) < kStartleMs ? Look::Surprised : Look::Annoyed;
  if (int32_t(surprisedUntilMs_ - now) > 0) return Look::Surprised;
  if (boxState_ != BoxState::Closed && talk_ == Talk::Reaction) return reactionLook_;
  if (mood_ == engine::Mood::Happy) return Look::Happy;
  return Look::Normal;
}

void HeroineCharacter::updateFace(uint32_t now) {
  const Look l = look(now);
  const bool talking = boxState_ != BoxState::Closed;

  // Glances to the side now and then; while asking, between the choices
  if (int32_t(now - nextGlanceMs_) >= 0) {
    const bool thinking = talking && talk_ == Talk::Question && choicesUp_;
    if (thinking || (!talking && randf() < 0.45f)) {
      glance_ = randf() < 0.5f ? kHeroineFrameLookLeft : kHeroineFrameLookRight;
      glanceUntilMs_ = now + 800 + uint32_t(randf() * 1000);
    }
    nextGlanceMs_ = now + (thinking ? 1400 : 2500 + uint32_t(randf() * 3500));
  }
  if (int32_t(now - glanceUntilMs_) >= 0 || (talking && talk_ != Talk::Question)) glance_ = -1;

  int face = -1, mouth = -1;
  switch (l) {
    case Look::Normal: face = glance_; break;
    case Look::Happy: face = kHeroineFrameEyesSmile; mouth = kHeroineFrameMouthSmile; break;
    case Look::Surprised: face = kHeroineFrameSurprised; break;
    case Look::Sleepy: face = kHeroineFrameEyesHalf; break;
    case Look::Asleep: face = kHeroineFrameEyesClosed; break;
    case Look::Annoyed: face = kHeroineFrameAnnoyed; break;
    case Look::Sad: face = kHeroineFrameSad; break;
  }

  // Blinks every few seconds, sometimes twice: half, closed, half
  if (!blinkMs_ && int32_t(now - nextBlinkMs_) >= 0) {
    blinkMs_ = now;
    doubleBlink_ = randf() < 0.15f;
  }
  if (blinkMs_ && int32_t(now - blinkMs_) >= 0) {
    const uint32_t t = now - blinkMs_;
    if (t >= kBlinkMs) {
      if (doubleBlink_) {
        doubleBlink_ = false;
        blinkMs_ = now + 80;
      } else {
        blinkMs_ = 0;
        nextBlinkMs_ = now + 2000 + uint32_t(randf() * 4000);
      }
    } else if (l == Look::Normal || l == Look::Sleepy) {  // the other looks have their own eyes
      face = (t < kBlinkMs / 3 || t >= kBlinkMs * 2 / 3) ? kHeroineFrameEyesHalf : kHeroineFrameEyesClosed;
    }
  }

  // Her lips move while her line types out
  if (typing(now)) {
    static const int8_t kTalk[4] = {kHeroineFrameMouthHalf, kHeroineFrameMouthOpen, kHeroineFrameMouthHalf, -1};
    static const int8_t kLaugh[4] = {kHeroineFrameMouthSmile, kHeroineFrameMouthOpen, kHeroineFrameMouthSmile, -1};
    mouth = (l == Look::Happy ? kLaugh : kTalk)[(now / kMouthStepMs) & 3];
  }
  portrait_.setFrames(face, mouth);
}

void HeroineCharacter::updatePose(float dt, uint32_t now) {
  const Look l = look(now);
  const bool talking = boxState_ != BoxState::Closed;

  // Where her head wants to be: a slow drift, now and then a tilt, toward
  // where she glances, into a patting hand, back from a poke...
  if (!asleep_ && !talking && int32_t(now - nextTiltMs_) >= 0) {
    tilt_ = (randf() < 0.5f ? -1 : 1) * (0.03f + 0.03f * randf());
    tiltUntilMs_ = now + 1500 + uint32_t(randf() * 1500);
    nextTiltMs_ = now + 6000 + uint32_t(randf() * 7000);
  }
  if (int32_t(now - tiltUntilMs_) >= 0) tilt_ = 0;
  float roll = 0.02f * wave(now, 8300) + tilt_, headY = 0;
  if (glance_ == kHeroineFrameLookLeft) roll -= 0.03f;
  if (glance_ == kHeroineFrameLookRight) roll += 0.03f;
  if (reaction_ == Reaction::Pat) {
    roll += patSide_ * 0.06f;
    headY += 1;
  }
  if (l == Look::Surprised) headY -= 2;
  if (l == Look::Sad) headY += 1.5f;
  if (talking && talk_ == Talk::Question && choicesUp_) roll += 0.04f;
  if (typing(now)) headY += 1.2f * fabsf(wave(now, 920));  // nods along with her words
  if (asleep_) {
    roll = 0.06f;
    headY = 3;
  }
  spring(roll_, rollV_, roll, 7, 0.7f, dt);
  spring(headY_, headYV_, headY, 10, 0.7f, dt);
  if (since(now, poseMs_) < kPoseStepMs) return;
  poseMs_ = now;

  // The painted tilt nearest her head's, kept a little past halfway to the
  // next so that she does not flicker between two
  const float level = roll_ / kHeroineTiltStep;
  if (fabsf(level - float(tiltLevel_)) > 0.7f) tiltLevel_ = int(lrintf(level));
  tiltLevel_ = std::max(-kHeroineTiltMax, std::min(kHeroineTiltMax, tiltLevel_));
  // Breathing lifts her head and shoulders, nods and slumps lower them; a hop lifts all of her
  const float breath = 0.5f - 0.5f * cosf(2 * kPi * float(now % (asleep_ ? 5200 : 3600)) / (asleep_ ? 5200.0f : 3600.0f));
  float hop = 0;
  if (hopMs_ && since(now, hopMs_) < kHopMs) hop = 3 * sinf(kPi * float(since(now, hopMs_)) / kHopMs);
  PortraitLayer::Pose p;
  p.x = baseX_;
  p.y = baseY_ - int(lrintf(hop));
  p.tilt = tiltLevel_;
  p.lift = std::max(-kMaxLift, std::min(kMaxLift, int(lrintf(breath * kBreathLift - headY_))));
  portrait_.setPose(p);
}

// ---- Small behaviours ----

void HeroineCharacter::wake(uint32_t now) {
  if (asleep_) {
    asleep_ = false;
    surprisedUntilMs_ = now + 300;  // startled awake
  }
  lastActiveMs_ = now;
}

void HeroineCharacter::previewExpression(Expression e, uint32_t untilMs) {
  preview_ = int(e);
  previewUntilMs_ = untilMs;
  wake(lastMs_);
}

float HeroineCharacter::randf() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(rng_ & 0xFFFFFF) / float(0x1000000);
}

// ---- Input ----

bool HeroineCharacter::hitTest(int x, int y) const {
  const bool onDialog = boxState_ != BoxState::Closed && dialog_.bounds().contains(x, y);
  return active_ && portrait_.contains(x, y) && !onDialog;
}

void HeroineCharacter::poke(float x, float y) {
  const uint32_t now = lastMs_;
  wake(now);
  const PortraitLayer::Pose& p = portrait_.pose();
  if (y < p.y + kHeroineShownHeight * 0.45f) {  // on her head: a pat, and she leans into the hand
    reaction_ = Reaction::Pat;
    patSide_ = x < p.x + kHeroineWidth * 0.5f ? -1.0f : 1.0f;
    hopMs_ = now;
  } else {
    reaction_ = Reaction::Poke;
  }
  reactionMs_ = now;
}

bool HeroineCharacter::handleTap(int x, int y, engine::Event& out) {
  if (!active_) return false;
  const uint32_t now = lastMs_;
  if (boxState_ != BoxState::Closed && dialog_.bounds().contains(x, y)) {
    if (talk_ == Talk::Question && cardUp_ && answer_ < 0) {
      const int option = dialog_.choiceAt(x, y);
      if (option >= 0) {
        out.type = engine::EventType::TapOption;
        out.value = option;
        dialog_.setPressed(option);
        return true;
      }
    }
    if (typing(now)) {  // a tap shows the rest of the line at once
      textMs_ = now - uint32_t(dialog_.length()) * settings_.textMs;
      return false;
    }
    if (talk_ == Talk::Message && cardUp_) {
      out.type = engine::EventType::TapCard;
      return true;
    }
    if (talk_ == Talk::Reaction) closeBox(now);
    return false;
  }
  if (hitTest(x, y)) {
    out.type = engine::EventType::TapCharacter;
    poke(float(x), float(y));
    return true;
  }
  wake(now);
  return false;
}

bool HeroineCharacter::busy(uint32_t now) const {
  const bool sliding = boxState_ == BoxState::Opening || boxState_ == BoxState::Closing;
  const bool choicesRising = talk_ == Talk::Question && typedMs_ && !choicesUp_ && boxState_ == BoxState::Open;
  return sliding || typing(now) || choicesRising || blinkMs_ != 0;
}

}  // namespace ui::heroine
