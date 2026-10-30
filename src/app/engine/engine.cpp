#include "app/engine/engine.h"

#include <algorithm>

namespace engine {

namespace {
constexpr uint32_t kHappyMs = 1500;
constexpr uint32_t kAlertMs = 1500;
constexpr uint32_t kNotificationMs = 6000;  // auto-dismiss
constexpr uint32_t kPromptMs = 60000;       // unanswered prompts expire
constexpr uint32_t kCardGapMs = 450;        // let the previous card slide out first
constexpr uint32_t kAnswerHoldMs = 220;     // keep the pressed button visible briefly
}  // namespace

void Engine::tick(uint32_t now) {
  if (!started_) {
    started_ = true;
    enterMood(Mood::Idle, now);
  }

  Event e;
  for (auto& q : inbox_) {
    while (q.pop(e)) handle(e, now);
  }
  step(now);

  world_.timeMs = now;
  world_.queued = uint16_t(queueCount_);
  out_.back() = world_;
  out_.publish();
}

void Engine::handle(const Event& e, uint32_t now) {
  const CardState& card = world_.card;
  bool showing = card.visible && !hidePending_;
  switch (e.type) {
    case EventType::TapCharacter:
      enterMood(Mood::Happy, now);
      break;
    case EventType::TapCard:
    case EventType::Button:
      if (showing && card.content.kind == CardKind::Notification) hideCard(now);
      break;
    case EventType::TapOption:
      if (showing && card.content.kind == CardKind::Prompt && e.value >= 0 &&
          e.value < card.content.optionCount) {
        answer(int8_t(e.value), now);
      }
      break;
    case EventType::ShowCard:
      enqueue(e.card);
      break;
  }
}

void Engine::step(uint32_t now) {
  CharacterState& ch = world_.character;
  uint32_t inMood = now - ch.moodSinceMs;
  if (ch.mood == Mood::Happy && inMood > kHappyMs) enterMood(Mood::Idle, now);
  if (ch.mood == Mood::Alert && inMood > kAlertMs) enterMood(Mood::Idle, now);

  CardState& card = world_.card;
  if (hidePending_ && int32_t(now - hideAtMs_) >= 0) {
    hidePending_ = false;
    hideCard(now);
  }
  if (card.visible && !hidePending_ && !card.content.sticky) {
    uint32_t shown = now - card.changedMs;
    if (card.content.kind == CardKind::Notification && shown > kNotificationMs) hideCard(now);
    if (card.content.kind == CardKind::Prompt && shown > kPromptMs) {
      outcomes_.push(Outcome{card.content.ref, -1});
      hideCard(now);
    }
  }
  if (!card.visible && queueCount_ > 0 && now - card.changedMs > kCardGapMs) showNext(now);
}

void Engine::enterMood(Mood m, uint32_t now) {
  world_.character.mood = m;
  world_.character.moodSinceMs = now;
}

// Prompts go ahead of notifications, otherwise first in first out
void Engine::enqueue(const CardContent& c) {
  if (queueCount_ == kMaxQueued) {  // full: drop the oldest notification, else the oldest card
    int drop = 0;
    for (int i = 0; i < queueCount_; i++) {
      if (queue_[i].kind == CardKind::Notification) {
        drop = i;
        break;
      }
    }
    for (int i = drop; i < queueCount_ - 1; i++) queue_[i] = queue_[i + 1];
    queueCount_--;
  }
  int at = queueCount_;
  if (c.kind == CardKind::Prompt) {
    at = 0;
    while (at < queueCount_ && queue_[at].kind == CardKind::Prompt) at++;
  }
  for (int i = queueCount_; i > at; i--) queue_[i] = queue_[i - 1];
  queue_[at] = c;
  queueCount_++;
}

void Engine::showNext(uint32_t now) {
  CardState& card = world_.card;
  card.content = queue_[0];
  for (int i = 0; i < queueCount_ - 1; i++) queue_[i] = queue_[i + 1];
  queueCount_--;
  card.id++;
  card.visible = true;
  card.chosen = -1;
  card.changedMs = now;
  enterMood(Mood::Alert, now);
}

void Engine::hideCard(uint32_t now) {
  CardState& card = world_.card;
  if (!card.visible) return;
  card.visible = false;
  card.changedMs = now;
}

void Engine::answer(int8_t option, uint32_t now) {
  CardState& card = world_.card;
  card.chosen = option;
  outcomes_.push(Outcome{card.content.ref, option});
  hidePending_ = true;
  hideAtMs_ = now + kAnswerHoldMs;
  enterMood(Mood::Happy, now);
}

}  // namespace engine
