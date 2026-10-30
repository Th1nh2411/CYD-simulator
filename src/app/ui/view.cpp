#include "app/ui/view.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

// Layer depth. Gaps leave room for backdrops, effects, character parts.
enum Z : int {
  kZBackground = 0,
  kZCharacter = 200,
  kZHud = 800,
  kZClock = 850,  // under the card: a prompt card may cover it
  kZCard = 900,
};
constexpr float kSlideMs = 260.0f;
constexpr uint32_t kCardDelayMs = 70;  // character starts moving first, card follows
constexpr uint32_t kHeldFaceMs = 2500;  // a long press shows an expression this long (as a preview)
const gfx::Color kHudColor = gfx::rgb(150, 150, 180);

uint32_t elapsed(uint32_t now, uint32_t since) {
  int32_t d = int32_t(now - since);  // snapshot may be a tick newer than now
  return d > 0 ? uint32_t(d) : 0;
}

float easeOut(float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

}  // namespace

void View::begin(gfx::Compositor& comp, int width, int height, CharacterSet& characters, int active,
                 uint32_t seed) {
  width_ = width;
  height_ = height;
  margin_ = std::max(4, std::min(width, height) / 30);
  textScale_ = std::min(width, height) >= 200 ? 2 : 1;

  characters_ = &characters;
  active_ = active;
  character_ = &characters[active];
  Layout center = layoutFor(engine::CardKind::None);
  for (int i = 0; i < characters.count(); i++) {
    // The default character keeps the seed itself, so it replays as before switching existed
    characters[i].attach(comp, kZCharacter, width, height, seed + uint32_t(i) * 0x9E3779B9u);
    characters[i].setAnchor(center.charX, center.charY);
    characters[i].setActive(i == active);
  }

  applyBackground();
  card_.setVisible(false);
  clock_.configure(width, margin_, textScale_);
  comp.add(&background_, kZBackground);
  comp.add(&hud_, kZHud);
  comp.add(&clock_, kZClock);
  comp.add(&card_, kZCard);
}

void View::syncCharacter(int index) {
  if (index != active_ && index >= 0 && index < characters_->count()) {
    endDrag();
    character_->setActive(false);
    active_ = index;
    character_ = &(*characters_)[index];
    character_->setActive(true);
    character_->setClock(minute_);
    character_->syncSettings();
    applyBackground();
  } else if (character_->syncSettings()) {
    applyBackground();
  }
}

void View::applyBackground() {
  background_.configure(gfx::Rect(0, 0, width_, height_), character_->background());
}

void View::setClock(int minuteOfDay) {
  minute_ = minuteOfDay;
  clock_.set(minuteOfDay);
  if (character_) character_->setClock(minuteOfDay);
}

void View::previewExpression(Expression e, uint32_t untilMs) { character_->previewExpression(e, untilMs); }

View::Layout View::layoutFor(engine::CardKind kind) const {
  Layout l;
  l.charX = width_ * 0.5f;
  l.charY = height_ * 0.5f;
  const bool landscape = width_ >= height_;
  const int m = margin_;

  bool beside = kind == engine::CardKind::Prompt && landscape;
  if (beside) {  // card on the right, character moves left
    int left = int(width_ * 0.44f);
    l.card = gfx::Rect(left, m, width_ - left - m, height_ - 2 * m);
    l.charX = left * 0.5f;
    l.hideDx = width_ - left + 4;
    l.narrowCard = true;
  } else if (kind != engine::CardKind::None) {  // card below, character moves up
    int top = int(height_ * (kind == engine::CardKind::Prompt ? 0.45f : 0.52f));
    l.card = gfx::Rect(m, top, width_ - 2 * m, height_ - top - m);
    l.charY = top * 0.5f;
    l.hideDy = height_ - top + 4;
  }
  return l;
}

void View::update(const engine::WorldState& w, uint32_t now) {
  const engine::CardState& card = w.card;
  shownKind_ = card.visible ? card.content.kind : engine::CardKind::None;
  shownOptions_ = card.content.optionCount;
  if (character_->presentsCards()) {
    card_.setVisible(false);
    cardId_ = 0;  // a later character that uses the shared card sets it up afresh
    character_->present(card, now);
    character_->update(w.character.mood, w.character.moodSinceMs, now);
    return;
  }
  if (card.id != cardId_) {
    cardId_ = card.id;
    cardKind_ = card.content.kind;
    cardLayout_ = layoutFor(cardKind_);
    pressed_ = -1;
    int bodyScale = cardLayout_.narrowCard ? 1 : textScale_;
    card_.setContent(card.content, cardLayout_.card.w, cardLayout_.card.h, textScale_, bodyScale);
  }

  // Card slides in from its side, after the character starts making room
  cardChangedMs_ = card.changedMs;
  uint32_t t = elapsed(now, card.changedMs);
  float shown = card.visible ? easeOut((float(t) - kCardDelayMs) / kSlideMs)
                             : 1.0f - easeOut(float(t) / kSlideMs);
  int cx = cardLayout_.card.x + int(float(cardLayout_.hideDx) * (1.0f - shown));
  int cy = cardLayout_.card.y + int(float(cardLayout_.hideDy) * (1.0f - shown));
  card_.setPosition(cx, cy);
  card_.setVisible(card.id != 0 && shown > 0.002f);
  card_.setPressed(card.chosen >= 0 ? card.chosen : pressed_);

  // Character: float centered, or move aside while a card is up, eyes on the card
  engine::CardKind focus = card.visible ? cardKind_ : engine::CardKind::None;
  Layout target = layoutFor(focus);
  character_->setAnchor(target.charX, target.charY);
  if (focus != engine::CardKind::None) {
    const gfx::Rect& b = card_.bounds();
    character_->setAttention(true, b.x + b.w * 0.5f, b.y + b.h * 0.5f);
  } else {
    character_->setAttention(false, 0, 0);
  }
  character_->update(w.character.mood, w.character.moodSinceMs, now);
}

bool View::handleTap(int x, int y, engine::Event& out) {
  out = engine::Event{};
  if (character_->presentsCards()) return character_->handleTap(x, y, out);
  if (card_.visible() && card_.bounds().contains(x, y)) {
    if (cardKind_ == engine::CardKind::Prompt) {
      int option = card_.optionAt(x, y);
      if (option < 0) return false;
      out.type = engine::EventType::TapOption;
      out.value = option;
      pressed_ = option;
      card_.setPressed(option);
    } else {
      out.type = engine::EventType::TapCard;
    }
    return true;
  }
  if (character_->hitTest(x, y)) {
    out.type = engine::EventType::TapCharacter;
    character_->poke(float(x), float(y));  // react right where the finger landed
    return true;
  }
  return false;
}

bool View::onCharacter(int x, int y) const {
  return !(card_.visible() && card_.bounds().contains(x, y)) && character_->hitTest(x, y);
}

bool View::handleGesture(TouchGesture::Kind kind, int x, int y, uint32_t now, engine::Event& out) {
  out = engine::Event{};
  switch (kind) {
    case TouchGesture::Kind::Tap:
      return handleTap(x, y, out);
    case TouchGesture::Kind::LongPress:
      if (onCharacter(x, y) && !character_->longPress(float(x), float(y))) {
        // Every expression but Normal in turn, which the character mostly shows anyway
        const int face = 1 + heldFaces_++ % int(kExpressionCount - 1);
        character_->previewExpression(Expression(face), now + kHeldFaceMs);
      }
      break;
    case TouchGesture::Kind::DragStart:
    case TouchGesture::Kind::HeldDragStart:
      carrying_ = onCharacter(x, y) && character_->grab(float(x), float(y), kind == TouchGesture::Kind::HeldDragStart);
      break;
    case TouchGesture::Kind::Drag:
      if (carrying_) character_->drag(float(x), float(y));
      break;
    case TouchGesture::Kind::DragEnd:
      endDrag();
      break;
    case TouchGesture::Kind::None:
      break;
  }
  return false;
}

bool View::handleMotion(MotionGesture::Kind kind, engine::Event& out) {
  out = engine::Event{};
  switch (kind) {
    case MotionGesture::Kind::Shake:
      if (shownKind_ != engine::CardKind::Notification) return false;
      out.type = engine::EventType::TapCard;
      return true;
    case MotionGesture::Kind::HoldLeft:
    case MotionGesture::Kind::HoldRight:
      if (shownKind_ != engine::CardKind::Prompt || shownOptions_ == 0) return false;
      out.type = engine::EventType::TapOption;
      out.value = kind == MotionGesture::Kind::HoldLeft ? 0 : shownOptions_ - 1;
      if (!character_->presentsCards()) {
        pressed_ = out.value;
        card_.setPressed(out.value);
      }
      return true;
    case MotionGesture::Kind::None:
      break;
  }
  return false;
}

void View::endDrag() {
  if (carrying_) character_->release();
  carrying_ = false;
}

void View::setDebugText(const char* text) { hud_.set(text, 4, 4, kHudColor, 1); }

bool View::wantsFullFrameRate(uint32_t now) const {
  bool sliding = cardId_ != 0 && elapsed(now, cardChangedMs_) < kCardDelayMs + uint32_t(kSlideMs) + 100;
  return sliding || character_->busy(now);
}

}  // namespace ui
