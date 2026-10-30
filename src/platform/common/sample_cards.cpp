#include "platform/common/sample_cards.h"

#include "app/core/utf8.h"

namespace platform {

namespace {
struct Sample {
  engine::CardKind kind;
  const char* title;
  const char* body;
  const char* options[2];
};

constexpr Sample kSamples[] = {
    {engine::CardKind::Notification, "New message", "Your build finished. Tap this card to dismiss it.", {}},
    {engine::CardKind::Prompt, "Deploy?", "Release v1.2 to production?", {"Approve", "Deny"}},
    {engine::CardKind::Notification, "새 메시지", "빌드가 끝났어요. 카드를 누르면 닫힙니다.", {}},
    {engine::CardKind::Prompt, "배포 요청", "v1.2를 운영 서버에 배포할까요?", {"승인", "거절"}},
};
constexpr uint32_t kSampleCount = sizeof(kSamples) / sizeof(kSamples[0]);
}  // namespace

engine::Event sampleCard(uint32_t n) {
  const Sample& s = kSamples[n % kSampleCount];
  engine::Event e;
  e.type = engine::EventType::ShowCard;
  e.card.kind = s.kind;
  core::copyUtf8(e.card.title, sizeof(e.card.title), s.title);
  core::copyUtf8(e.card.body, sizeof(e.card.body), s.body);
  if (s.kind == engine::CardKind::Prompt) {
    e.card.ref = n + 1;
    for (const char* option : s.options) core::copyUtf8(e.card.options[e.card.optionCount++], sizeof(e.card.options[0]), option);
  }
  return e;
}

}  // namespace platform
