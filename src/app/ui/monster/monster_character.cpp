#include "app/ui/monster/monster_character.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/ui/monster/monster_json.h"
#include "app/ui/hangul.h"

namespace ui::monster {

namespace {

constexpr float kPi = 3.14159265f;

// Layer depths above the character's base z
enum Depth : int { kZBackdrop = 0, kZFoe = 10, kZFoePlate = 12, kZMonster = 20, kZMinePlate = 22,
                   kZFx = 30, kZEmote = 40, kZBox = 50, kZWipe = 90 };

constexpr uint32_t kBangMs = 450;      // "!" before anything happens
constexpr uint32_t kBoxSlideMs = 180;
constexpr uint32_t kFlickerMs = 140;   // fire starter's tail flame
constexpr uint32_t kStepMs = 160;      // walk frames
constexpr uint32_t kHopMs = 320;
// Carried: art px over its shadow / from the finger down to its head
constexpr int kHeldAir = 6, kHangGap = 5;
constexpr uint32_t kLiftMs = 150, kDropMs = 160, kStruggleMs = 120;
constexpr uint32_t kPauseMs = 800;     // after a battle message is fully shown
constexpr uint32_t kPressedMs = 250;   // a picked move stays lit before it is used
// Encounter: two flashes, bars close, scene changes, bars open
constexpr uint32_t kFlashEnd = 240, kCloseEnd = 640, kOpenEnd = 1000;
constexpr uint32_t kSlideInMs = 400;
// Attack timeline after a move is picked
constexpr uint32_t kLungeAt = 150, kLungeMs = 300, kSparksAt = 350, kSparksEnd = 900, kHitAt = 850,
                   kHitMs = 400, kResultAt = 1300;
constexpr uint32_t kLeaveClose = 360, kLeaveEnd = 720;

const gfx::Color kBlack = gfx::rgb(0, 0, 0), kWhite = gfx::rgb(255, 255, 255);

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
uint32_t since(uint32_t now, uint32_t t) { return int32_t(now - t) > 0 ? now - t : 0; }

}  // namespace

// ---- Settings ----

void MonsterCharacter::resetSettings() { edited_ = MonsterConfig(); }
void MonsterCharacter::mergeSettings(JsonObjectConst patch) { mergeJson(patch, edited_); }
bool MonsterCharacter::applyPreset(const char* name) { return monster::applyPreset(name, edited_); }
void MonsterCharacter::settingsToJson(JsonObject out) const { toJson(edited_, out); }

void MonsterCharacter::describe(JsonObject out) const {
  monster::describe(out);
}

// ---- Setup ----

void MonsterCharacter::attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) {
  width_ = screenW;
  height_ = screenH;
  battleScale_ = std::max(2, std::min(screenW, screenH) / 80);
  seed_ = rng_ = seed ? seed : 1;
  takeSettings();
  applySettings();
  x_ = targetX_ = float(width_ / 2 - 8 * scale_);
  y_ = targetY_ = float(height_ / 2 - 8 * scale_);

  comp.add(&backdrop_, z + kZBackdrop);
  comp.add(&foe_, z + kZFoe);
  comp.add(&foePlate_, z + kZFoePlate);
  comp.add(&monster_, z + kZMonster);
  comp.add(&minePlate_, z + kZMinePlate);
  comp.add(&fx_, z + kZFx);
  comp.add(&emote_, z + kZEmote);
  comp.add(&box_, z + kZBox);
  comp.add(&wipe_, z + kZWipe);
  wipe_.configure(gfx::Rect(0, 0, width_, height_));
  setActive(false);
}

void MonsterCharacter::applySettings() {
  const MonsterConfig& c = settings_;
  scale_ = c.scale;
  move_ = speciesMove(c.species);
  for (int f = 0; f < 2; f++) {
    gfx::Color* p = monPal_[f];
    p[kOutline] = gfx::mix(c.body, kBlack, 24);
    p[kBody] = c.body;
    p[kShade] = gfx::mix(c.body, kBlack, 8);
    p[kBelly] = c.belly;
    p[kEye] = gfx::rgb(40, 32, 48);
    p[kShine] = kWhite;
    p[kAccent] = f ? gfx::mix(c.accent, c.accent2, 14) : c.accent;
    p[kAccent2] = f ? gfx::mix(c.accent2, kWhite, 12) : c.accent2;
    p[kMouth] = gfx::mix(c.body, kBlack, 16);
    p[kScarf] = c.scarf;
    p[kScarfShade] = gfx::mix(c.scarf, kBlack, 10);
  }
  const gfx::Color tiles[kTileColors] = {0, c.grass, gfx::mix(c.grass, kBlack, 7), gfx::mix(c.grass, kWhite, 9),
                                         gfx::rgb(240, 80, 80), gfx::rgb(248, 248, 248), gfx::rgb(248, 208, 64),
                                         gfx::mix(c.grass, kBlack, 8), gfx::mix(c.grass, kBlack, 14)};
  memcpy(tilePal_, tiles, sizeof(tilePal_));
  const gfx::Color foe[kFoeColors] = {0, gfx::rgb(56, 56, 72), gfx::rgb(200, 200, 216), gfx::rgb(248, 248, 248),
                                      gfx::mix(c.grass, kBlack, 10), gfx::rgb(232, 56, 56), gfx::rgb(168, 216, 248)};
  memcpy(foePal_, foe, sizeof(foePal_));
  const gfx::Color emo[kEmoteColors] = {0, gfx::rgb(48, 48, 64), kWhite, gfx::rgb(232, 56, 56),
                                        gfx::rgb(56, 120, 232)};
  memcpy(emotePal_, emo, sizeof(emotePal_));

  backdrop_.configure(gfx::Rect(0, 0, width_, height_), scale_, tilePal_, seed_);
  const int size = 16 * scale_;
  x_ = std::max(0.0f, std::min(x_, float(width_ - size)));
  y_ = std::max(0.0f, std::min(y_, float(height_ - size)));
  targetX_ = std::max(0.0f, std::min(targetX_, float(width_ - size)));
  targetY_ = std::max(0.0f, std::min(targetY_, float(height_ - size)));
}

bool MonsterCharacter::syncSettings() {
  if (!takeSettings()) return false;
  applySettings();
  return true;
}

void MonsterCharacter::setActive(bool active) {
  active_ = active;
  backdrop_.setVisible(active);
  foe_.hide();
  foePlate_.setVisible(false);
  minePlate_.setVisible(false);
  fx_.clear();
  emote_.hide();
  box_.setVisible(false);
  wipe_.off();
  monster_.setVisible(active);
  stage_ = Stage::Field;
  backdrop_.setMode(SceneLayer::Mode::Field);
  boxState_ = BoxState::Closed;
  battleAt_ = noticeAt_ = 0;
  cardId_ = 0;  // a card already up is staged afresh
  cardUp_ = false;
  started_ = false;
}

// ---- Cards ----

void MonsterCharacter::present(const engine::CardState& card, uint32_t now) {
  if (!active_) return;
  if (card.id != cardId_) {
    cardId_ = card.id;
    if (card.visible) {
      card_ = card.content;
      cardUp_ = true;
      answer_ = -1;
      onCard(now);
    }
  } else if (cardUp_ && !card.visible) {
    cardUp_ = false;
    onCardGone(now);
  }
  if (cardUp_ && card.chosen >= 0 && answer_ < 0) answer_ = card.chosen;
}

void MonsterCharacter::onCard(uint32_t now) {
  if (stage_ != Stage::Field) toField(now);  // a new card cuts the last battle short
  wake(now);
  walking_ = false;
  dir_ = Dir::Down;
  emote(kEmoteBang, now, kBangMs);
  if (card_.kind == engine::CardKind::Prompt) {
    battleAt_ = now + kBangMs;
    if (boxState_ != BoxState::Closed) {
      boxState_ = BoxState::Closing;
      boxMs_ = now;
    }
  } else {
    noticeAt_ = now + kBangMs;
  }
}

void MonsterCharacter::onCardGone(uint32_t now) {
  if (card_.kind == engine::CardKind::Notification) {
    noticeAt_ = 0;
    if (boxState_ == BoxState::Opening || boxState_ == BoxState::Open) {
      boxState_ = BoxState::Closing;
      boxMs_ = now;
    }
    nextIdeaMs_ = now + 600;
    return;
  }
  battleAt_ = 0;
  if (answer_ >= 0 || stage_ == Stage::Field) return;
  // Unanswered prompt expired: the request flies off
  stage_ = Stage::Attack;
  stageMs_ = now - kResultAt;
  menu_ = false;
  resultShown_ = true;
  box_.setOptions(nullptr, 0);
  char text[96], foe[72];
  withParticle(foe, sizeof(foe), card_.title[0] ? card_.title : "요청", "은", "는");
  snprintf(text, sizeof(text), "%s 날아가 버렸다...", foe);
  say("", text, now);
}

// ---- Frame ----

void MonsterCharacter::update(engine::Mood mood, uint32_t moodSinceMs, uint32_t now) {
  if (!active_) return;
  float dt = started_ ? std::min(0.05f, float(since(now, lastMs_)) / 1000.0f) : 0.0f;
  lastMs_ = now;
  if (!started_) {
    started_ = true;
    lastActiveMs_ = now;
    nextIdeaMs_ = now + 1000;
  }
  // Happy (a tap on it, an answered prompt): a little hop on the field
  if (mood != mood_ || moodSinceMs != moodSince_) {
    mood_ = mood;
    moodSince_ = moodSinceMs;
    if (mood == engine::Mood::Happy && stage_ == Stage::Field && !hopping_) {
      wake(now);
      hop(now);
      emote(kEmoteHeart, now, 1200);
    }
  }

  switch (stage_) {
    case Stage::Field:
      updateField(dt, now);
      break;
    case Stage::Encounter:
      updateEncounter(now);
      break;
    case Stage::Battle:
    case Stage::Attack:
      updateBattle(now);
      break;
    case Stage::Leave:
      updateLeave(now);
      break;
  }
  fx_.step(dt);
}

void MonsterCharacter::updateField(float dt, uint32_t now) {
  if (battleAt_ && int32_t(now - battleAt_) >= 0) {
    battleAt_ = 0;
    stage_ = Stage::Encounter;
    stageMs_ = now;
    emote_.hide();
    updateEncounter(now);
    return;
  }
  if (noticeAt_ && int32_t(now - noticeAt_) >= 0) {
    noticeAt_ = 0;
    boxState_ = BoxState::Opening;
    boxMs_ = now;
    box_.setOptions(nullptr, 0);
    box_.setMessage(card_.title, card_.body);
  }
  updateDialog(now);

  if (carry_ == Carry::Held) followFinger(now);
  if (carry_ == Carry::Dropping && since(now, carryMs_) >= kDropMs) {
    carry_ = Carry::None;
    walkTo(homeX_, homeY_, now);
  }
  const bool carried = carry_ != Carry::None;

  bool previewing = preview_ >= 0 && int32_t(previewUntilMs_ - now) > 0;
  bool attending = cardUp_ || noticeAt_ || battleAt_;
  if (boxState_ != BoxState::Closed && !carried) {
    // Keep clear of the dialog box, facing it
    float limit = float(fieldBoxRect().y - 16 * scale_ - scale_);
    if (y_ > limit) walkTo(x_, limit, now);
    else if (!walking_) dir_ = Dir::Down;
  }
  if (!carried && (walking_ || (!previewing && !asleep_ && !attending))) updateWander(dt, now);

  if (!asleep_ && !attending && !walking_ && !carried && boxState_ == BoxState::Closed &&
      since(now, lastActiveMs_) > uint32_t(settings_.sleepAfter) * 1000) {
    asleep_ = true;
  }
  showFieldMonster(now);
}

void MonsterCharacter::updateWander(float dt, uint32_t now) {
  const float size = float(16 * scale_);
  if (walking_) {
    // Grid-style: along x first, then along y
    float step = settings_.walkSpeed * float(scale_) * dt;
    float dx = targetX_ - x_, dy = targetY_ - y_;
    if (fabsf(dx) > 0.5f) {
      dir_ = dx > 0 ? Dir::Right : Dir::Left;
      x_ += std::max(-step, std::min(step, dx));
    } else if (fabsf(dy) > 0.5f) {
      dir_ = dy > 0 ? Dir::Down : Dir::Up;
      y_ += std::max(-step, std::min(step, dy));
    } else {
      walking_ = false;
      nextIdeaMs_ = now + 1000 + uint32_t(randf() * 2500);
    }
    return;
  }
  if (int32_t(now - nextIdeaMs_) < 0) return;
  float r = randf();
  if (r < 0.6f) {
    walkTo(randf() * (float(width_) - size), size * 0.25f + randf() * (float(height_) - size * 1.25f), now);
  } else if (r < 0.85f) {
    dir_ = Dir(int(randf() * 4) & 3);
    nextIdeaMs_ = now + 800 + uint32_t(randf() * 1500);
  } else {
    emote(kEmoteDots, now, 1000);
    nextIdeaMs_ = now + 2000;
  }
}

void MonsterCharacter::walkTo(float x, float y, uint32_t now) {
  const float size = float(16 * scale_);
  targetX_ = std::max(0.0f, std::min(float(width_) - size, x));
  targetY_ = std::max(0.0f, std::min(float(height_) - size, y));
  // Land on the art pixel grid so the sprite lines up with the tiles
  targetX_ = float(int(targetX_) / scale_ * scale_);
  targetY_ = float(int(targetY_) / scale_ * scale_);
  if (!walking_) walkMs_ = now;
  walking_ = true;
}

gfx::Rect MonsterCharacter::fieldBoxRect() const {
  const int h = 4 * 16 + 14;
  return gfx::Rect(4, height_ - h - 4, width_ - 8, h);
}

void MonsterCharacter::updateDialog(uint32_t now) {
  if (boxState_ == BoxState::Closed) return;
  const gfx::Rect rest = fieldBoxRect();
  float open = 1;
  uint32_t t = since(now, boxMs_);
  if (boxState_ == BoxState::Opening) {
    open = clamp01(float(t) / kBoxSlideMs);
    if (open >= 1) {
      boxState_ = BoxState::Open;
      textMs_ = now;
    }
  } else if (boxState_ == BoxState::Closing) {
    open = 1 - clamp01(float(t) / kBoxSlideMs);
    if (open <= 0) {
      boxState_ = BoxState::Closed;
      box_.setVisible(false);
      return;
    }
  }
  box_.place(gfx::Rect(rest.x, rest.y + int((1 - open) * float(height_ - rest.y)), rest.w, rest.h));
  box_.setVisible(true);
  box_.setTyped(boxState_ == BoxState::Open ? typed(now) : (boxState_ == BoxState::Closing ? box_.length() : 0));
  box_.setMore(boxState_ == BoxState::Open && saidAll(now) && ((now / 400) & 1));
}

gfx::Rect MonsterCharacter::monsterRect() const {
  int x = int(x_) / scale_ * scale_, y = int(y_) / scale_ * scale_;
  return gfx::Rect(x, y, 16 * scale_, 16 * scale_);
}

const gfx::Color* MonsterCharacter::palette(uint32_t now) const {
  bool flickers = move_ == Move::Flame;  // only a flame flickers
  return monPal_[flickers ? (now / kFlickerMs) & 1 : 0];
}

const gfx::IndexedImage* MonsterCharacter::frames() const { return kSpecies[settings_.species % kSpeciesCount].frames; }

void MonsterCharacter::showFieldMonster(uint32_t now) {
  const gfx::Color* pal = palette(now);
  const gfx::IndexedImage* f = frames();
  gfx::Rect r = monsterRect();
  int lift = 0;
  if (hopping_) {
    uint32_t t = since(now, hopMs_);
    if (t >= kHopMs) hopping_ = false;
    else lift = int(sinf(kPi * float(t) / kHopMs) * 5.0f) * scale_;
  }
  const int air = airPx(now);

  bool previewing = preview_ >= 0 && int32_t(previewUntilMs_ - now) > 0;
  Expression face = previewing ? Expression(preview_) : Expression::Normal;
  const gfx::IndexedImage* img = &f[kDown0];
  bool flip = false;
  if (carry_ != Carry::None) {
    img = &f[kHeld0 + ((since(now, carryMs_) / kStruggleMs) & 1)];
  } else if (asleep_ || face == Expression::Sleepy) {
    img = &f[kSleep];
  } else if (!previewing) {
    int frame = walking_ ? int((since(now, walkMs_) / kStepMs) & 1) : 0;
    switch (dir_) {
      case Dir::Down: img = &f[kDown0 + frame]; break;
      case Dir::Up: img = &f[kUp0 + frame]; break;
      case Dir::Right: img = &f[kSide0 + frame]; break;
      case Dir::Left: img = &f[kSide0 + frame]; flip = true; break;
    }
  }
  monster_.show(*img, pal, r.x, r.y - lift - air, scale_, flip, true, air);

  // Speech-bubble emote over its head
  int e = -1;
  if (previewing) {
    static const int8_t kByExpr[kExpressionCount] = {-1, kEmoteHeart, kEmoteBang, kEmoteZzz, kEmoteAnger, kEmoteDrop};
    e = kByExpr[size_t(face)];
  } else if (asleep_) {
    e = kEmoteZzz;
  } else if (int32_t(emoteUntilMs_ - now) > 0) {
    e = emoteKind_;
  }
  if (e < 0) {
    emote_.hide();
    return;
  }
  const int es = std::max(2, scale_ - 1);
  int bob = asleep_ ? int((now / 500) & 1) * es : 0;
  emote_.show(kEmotes[e], emotePal_, r.x + 8 * scale_ - 2 * es, r.y - lift - air - 12 * es + 2 * scale_ - bob, es,
              false);
}

// Held: eases from where it was picked up to hanging just below the finger
void MonsterCharacter::followFinger(uint32_t now) {
  const float size = float(16 * scale_), u = carryHeight(now), air = float(airPx(now));
  float x = fingerX_ + grabDx_ + (-size / 2 - grabDx_) * u;
  float y = fingerY_ + grabDy_ + (float(kHangGap * scale_) - grabDy_) * u;
  x_ = std::max(0.0f, std::min(float(width_) - size, x));
  y_ = std::max(air, std::min(float(height_) - size, y + air));
}

float MonsterCharacter::carryHeight(uint32_t now) const {
  float t = float(since(now, carryMs_));
  if (carry_ == Carry::Held) {
    float u = std::min(1.0f, t / kLiftMs);
    return u * (2 - u);  // eases out
  }
  if (carry_ == Carry::Dropping) {
    float u = std::min(1.0f, t / kDropMs);
    return dropFrom_ * (1 - u * u);  // falls
  }
  return 0;
}

int MonsterCharacter::airPx(uint32_t now) const { return int(carryHeight(now) * kHeldAir + 0.5f) * scale_; }

// ---- Battle ----

void MonsterCharacter::toBattleScene() {
  backdrop_.setMode(SceneLayer::Mode::Battle);
  const int W = width_, H = height_, boxH = 82;
  backdrop_.setPlatforms(gfx::Rect(W * 73 / 100 - W * 18 / 100, H * 40 / 100 - H * 9 / 200, W * 36 / 100, H * 9 / 100),
                         gfx::Rect(W * 27 / 100 - W * 21 / 100, H * 66 / 100 - H * 10 / 200, W * 42 / 100, H * 10 / 100));
  box_.place(gfx::Rect(0, H - boxH, W, boxH));
  box_.setOptions(nullptr, 0);
  box_.setMessage("", "");
  box_.setVisible(true);
  boxState_ = BoxState::Closed;  // the battle box is managed by the battle
  foePlate_.place(gfx::Rect(8, 10, W * 47 / 100, 38));
  minePlate_.place(gfx::Rect(W - 8 - W * 47 / 100, H - boxH - 46, W * 47 / 100, 40));
  foePlate_.set(card_.title[0] ? card_.title : "요청", std::max(1, int(settings_.level) + 2), 1);
  minePlate_.set(settings_.name, settings_.level, 1);
  foePlate_.setVisible(true);
  minePlate_.setVisible(true);
  emote_.hide();
  fx_.clear();
  foeHp_ = 1;
  foeGone_ = false;
  pressedMs_ = 0;
}

void MonsterCharacter::updateEncounter(uint32_t now) {
  uint32_t t = since(now, stageMs_);
  if (t < kFlashEnd) {
    wipe_.flash(((t / 60) & 1) == 0);
  } else if (t < kCloseEnd) {
    wipe_.bars(float(t - kFlashEnd) / float(kCloseEnd - kFlashEnd));
  } else {
    if (backdrop_.mode() != SceneLayer::Mode::Battle) toBattleScene();
    if (t < kOpenEnd) {
      wipe_.bars(1 - float(t - kCloseEnd) / float(kOpenEnd - kCloseEnd));
    } else {
      wipe_.off();
      stage_ = Stage::Battle;
      stageMs_ = now;
      menu_ = resultShown_ = false;
      char text[96], foe[72];
      withParticle(foe, sizeof(foe), card_.title[0] ? card_.title : "요청", "이", "가");
      snprintf(text, sizeof(text), "야생의 %s 나타났다!", foe);
      say("", text, now);
    }
  }
  if (backdrop_.mode() == SceneLayer::Mode::Battle) showBattleSprites(now);
  else showFieldMonster(now);
}

void MonsterCharacter::updateBattle(uint32_t now) {
  uint32_t t = since(now, stageMs_);
  box_.setTyped(typed(now));

  if (stage_ == Stage::Battle) {
    // Intro line, then the moves
    if (!menu_ && saidAll(now) && since(now, textMs_) > uint32_t(box_.length()) * settings_.textMs + kPauseMs) {
      menu_ = true;
      char prompt[sizeof(card_.body) + 32];
      if (card_.body[0]) {
        snprintf(prompt, sizeof(prompt), "%s", card_.body);
      } else {
        char name[48];
        withParticle(name, sizeof(name), settings_.name, "은", "는");
        snprintf(prompt, sizeof(prompt), "%s 무엇을 할까?", name);
      }
      box_.setOptions(card_.options, card_.optionCount);
      say("", prompt, now);
    }
    box_.setMore(!menu_ && saidAll(now) && ((now / 400) & 1));
    // The picked move stays lit for a moment before it is used
    if (answer_ >= 0 && !pressedMs_) {
      pressedMs_ = now;
      box_.setPressed(answer_);
    }
    if (answer_ >= 0 && since(now, pressedMs_) >= kPressedMs) {
      stage_ = Stage::Attack;
      stageMs_ = now;
      t = 0;
      char text[96];
      snprintf(text, sizeof(text), "%s의 %s!", settings_.name, card_.options[answer_ < card_.optionCount ? answer_ : 0]);
      box_.setOptions(nullptr, 0);
      say("", text, now);
    }
  }

  if (stage_ == Stage::Attack) {
    box_.setMore(false);
    if (answer_ >= 0 && t >= kSparksAt && t < kSparksEnd) spawnMove();
    if (answer_ == 0 && t >= kHitAt) foeHp_ = 1 - clamp01(float(t - kHitAt) / kHitMs);
    foePlate_.set(card_.title[0] ? card_.title : "요청", std::max(1, int(settings_.level) + 2), foeHp_);
    if (!resultShown_ && t >= kResultAt) {
      resultShown_ = true;
      char text[96], foe[72];
      if (answer_ == 0) {
        snprintf(text, sizeof(text), "효과는 굉장했다!");
      } else {
        withParticle(foe, sizeof(foe), card_.title[0] ? card_.title : "요청", "은", "는");
        snprintf(text, sizeof(text), "%s 도망쳤다!", foe);
      }
      say("", text, now);
    }
    if (resultShown_ && saidAll(now) && since(now, textMs_) > uint32_t(box_.length()) * settings_.textMs + kPauseMs) {
      stage_ = Stage::Leave;
      stageMs_ = now;
    }
  }
  showBattleSprites(now);
}

// The move flies from its mouth to the request: sparks, leaves or bubbles
void MonsterCharacter::spawnMove() {
  const int bs = battleScale_;
  float sx = float(width_ * 27 / 100 + 6 * bs), sy = float(height_ - 82 - 96 + 30);
  float fx = float(width_ * 73 / 100), fy = float(height_ * 40 / 100 - 10 * bs);
  for (int i = 0; i < 2; i++) {
    FxLayer::Spark s;
    float flight = 0.30f + randf() * 0.12f, spread = 60, size = 2 + randf() * 3;
    s.color = randf() < 0.5f ? settings_.accent : settings_.accent2;
    if (move_ == Move::Leaf) {  // bigger, slower, more scattered leaves
      flight += 0.12f;
      spread = 100;
      size += 1;
    } else if (move_ == Move::Bubble) {  // round bubbles, water and foam
      s.round = true;
      s.color = randf() < 0.5f ? settings_.body : gfx::rgb(240, 248, 255);
    }
    s.x = sx + randf() * 8;
    s.y = sy + randf() * 8;
    s.vx = (fx - sx) / flight + (randf() - 0.5f) * spread;
    s.vy = (fy - sy) / flight + (randf() - 0.5f) * spread;
    s.life = flight;
    s.size = uint8_t(size);
    fx_.spawn(s);
  }
}

void MonsterCharacter::showBattleSprites(uint32_t now) {
  const int W = width_, H = height_, bs = battleScale_;
  const gfx::Color* pal = palette(now);
  const gfx::IndexedImage& back = frames()[kBack];
  const int foeSize = kFoeEnvelope.w * bs, backSize = back.w * bs;
  int foeX = W * 73 / 100 - foeSize / 2, foeY = H * 40 / 100 - foeSize + 2 * bs;
  int mineX = W * 27 / 100 - backSize / 2, mineY = H - 82 - backSize + 6 * bs;

  uint32_t t = since(now, stageMs_);
  if (stage_ == Stage::Encounter || (stage_ == Stage::Battle && t < kSlideInMs)) {  // both slide in from their sides
    float k = stage_ == Stage::Encounter ? 1 : 1 - clamp01(float(t) / kSlideInMs);
    foeX += int(k * float(W - foeX));
    mineX -= int(k * float(mineX + backSize));
  }
  bool foeShown = !foeGone_;
  if (stage_ == Stage::Attack) {
    if (t >= kLungeAt && t < kLungeAt + kLungeMs) {  // lunge toward the request and back
      float k = sinf(kPi * float(t - kLungeAt) / kLungeMs);
      mineX += int(k * 8 * bs);
      mineY -= int(k * 4 * bs);
    }
    if (answer_ >= 0 && t >= kHitAt && t < kHitAt + kHitMs) foeShown = ((t / 80) & 1) == 0;  // blink when hit
    if (t >= kResultAt) {
      uint32_t u = t - kResultAt;
      if (answer_ == 0) foeY += int(std::min(1.0f, float(u) / 300) * float(foeSize));      // faints, sinks away
      else if (answer_ > 0) foeX += int(std::min(1.0f, float(u) / 300) * float(W - foeX));  // runs off
      else foeY -= int(std::min(1.0f, float(u) / 400) * float(foeY + foeSize));              // flies off
      if (u >= 400) foeGone_ = true;
    }
  }
  if (foeShown && !foeGone_) foe_.show(kFoeEnvelope, foePal_, foeX, foeY, bs, false);
  else foe_.hide();
  monster_.show(back, pal, mineX, mineY, bs, false);
}

void MonsterCharacter::updateLeave(uint32_t now) {
  uint32_t t = since(now, stageMs_);
  if (t < kLeaveClose) {
    wipe_.bars(float(t) / kLeaveClose);
    showBattleSprites(now);
    return;
  }
  if (backdrop_.mode() == SceneLayer::Mode::Battle) {
    toField(now);
    stage_ = Stage::Leave;  // still opening the bars
    stageMs_ = now - kLeaveClose;
    t = kLeaveClose;
  }
  if (t < kLeaveEnd) {
    wipe_.bars(1 - float(t - kLeaveClose) / float(kLeaveEnd - kLeaveClose));
    showFieldMonster(now);
    return;
  }
  // Back on the field: pleased after a super-effective move, pensive otherwise
  wipe_.off();
  stage_ = Stage::Field;
  nextIdeaMs_ = now + 800;
  emote(answer_ == 0 ? kEmoteHeart : kEmoteDots, now, 1200);
  if (answer_ == 0) hop(now);
  showFieldMonster(now);
}

void MonsterCharacter::toField(uint32_t now) {
  stage_ = Stage::Field;
  backdrop_.setMode(SceneLayer::Mode::Field);
  foe_.hide();
  foePlate_.setVisible(false);
  minePlate_.setVisible(false);
  fx_.clear();
  wipe_.off();
  box_.setOptions(nullptr, 0);
  box_.setVisible(false);
  boxState_ = BoxState::Closed;
  menu_ = false;
  lastActiveMs_ = now;
}

// ---- Text ----

void MonsterCharacter::say(const char* title, const char* body, uint32_t now) {
  box_.setMessage(title, body);
  textMs_ = now;
}

int MonsterCharacter::typed(uint32_t now) const {
  return int(since(now, textMs_) / std::max<uint32_t>(1, settings_.textMs));
}

// ---- Small behaviours ----

void MonsterCharacter::wake(uint32_t now) {
  asleep_ = false;
  lastActiveMs_ = now;
}

void MonsterCharacter::hop(uint32_t now) {
  hopping_ = true;
  hopMs_ = now;
}

void MonsterCharacter::emote(Emote e, uint32_t now, uint32_t ms) {
  emoteKind_ = e;
  emoteUntilMs_ = now + ms;
}

void MonsterCharacter::previewExpression(Expression e, uint32_t untilMs) {
  preview_ = int(e);
  previewUntilMs_ = untilMs;
  walking_ = false;
  asleep_ = false;
  if (e == Expression::Happy) hop(lastMs_);
}

float MonsterCharacter::randf() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(rng_ & 0xFFFFFF) / float(0x1000000);
}

// ---- Input ----

bool MonsterCharacter::hitTest(int x, int y) const {
  const bool onBox = boxState_ == BoxState::Open && box_.bounds().contains(x, y);
  return stage_ == Stage::Field && monsterRect().inflate(scale_ * 2).contains(x, y) && !onBox;
}

void MonsterCharacter::poke(float, float) {
  wake(lastMs_);
  hop(lastMs_);
  emote(kEmoteHeart, lastMs_, 1200);
}

bool MonsterCharacter::grab(float x, float y, bool) {
  if (stage_ != Stage::Field) return false;
  carry_ = Carry::Held;
  carryMs_ = lastMs_;
  homeX_ = x_;
  homeY_ = y_;
  fingerX_ = x;
  fingerY_ = y;
  grabDx_ = x_ - x;
  grabDy_ = y_ - y;
  walking_ = false;
  dir_ = Dir::Down;
  wake(lastMs_);
  emote(kEmoteBang, lastMs_, 800);
  return true;
}

void MonsterCharacter::drag(float x, float y) {
  fingerX_ = x;
  fingerY_ = y;
}

void MonsterCharacter::release() {
  if (carry_ != Carry::Held) return;
  dropFrom_ = carryHeight(lastMs_);
  carry_ = Carry::Dropping;
  carryMs_ = lastMs_;
}

bool MonsterCharacter::handleTap(int x, int y, engine::Event& out) {
  if (!active_) return false;
  if (stage_ == Stage::Battle) {
    int option = menu_ && answer_ < 0 ? box_.optionAt(x, y) : -1;
    if (option < 0) return false;
    out.type = engine::EventType::TapOption;
    out.value = option;
    box_.setPressed(option);
    return true;
  }
  if (stage_ != Stage::Field) return false;
  if (boxState_ == BoxState::Open && box_.bounds().contains(x, y)) {
    out.type = engine::EventType::TapCard;
    return true;
  }
  if (hitTest(x, y)) {
    out.type = engine::EventType::TapCharacter;
    poke(float(x), float(y));
    return true;
  }
  if (!cardUp_) {  // walk over to where the finger landed
    wake(lastMs_);
    walkTo(float(x - 8 * scale_), float(y - 12 * scale_), lastMs_);
  }
  return false;
}

bool MonsterCharacter::busy(uint32_t now) const {
  bool animating = stage_ == Stage::Encounter || stage_ == Stage::Attack || stage_ == Stage::Leave ||
                   (stage_ == Stage::Battle && since(now, stageMs_) < kSlideInMs);
  return animating || wipe_.on() || fx_.active() || hopping_ || carry_ != Carry::None ||
         boxState_ == BoxState::Opening || boxState_ == BoxState::Closing;
}

}  // namespace ui::monster
