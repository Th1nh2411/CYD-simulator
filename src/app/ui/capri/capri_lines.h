#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::capri {

// What she says on her own, in her voice (lachydotmcg/capri-desktop's lines
// and persona, and the event lines of mochiiwuff/capri-desktop). Each set in
// Korean and English; "{c}" is what she calls the user.
enum class Lines : uint8_t {
  HelloMorning,
  HelloLate,
  HelloMiss,
  Pet,            // a tap on her head
  Poke,           // a tap on her body
  CheckIn,        // she pops in now and then
  Nudge,          // nobody touched her for hours
  NudgeAnswered,  // tapped soon after a nudge
  NudgeIgnored,
  Sleep,
  Wake,
  Thinking,       // asked her brain, waiting
  Offline,        // her brain did not answer
  // A card's first line, by its tag
  LeadAlert,
  LeadDone,
  LeadError,
  LeadWork,
  LeadRemind,
  LeadIdle,
  LeadInfo,
  LeadStart,
  LeadEnd,
  // After a prompt: "%s" is the option picked
  AnswerYes,
  AnswerNo,
  AnswerNone,
  Count
};

enum class Lang : uint8_t { Ko, En, Count };

int lineCount(Lines set, Lang lang);
const char* line(Lines set, Lang lang, int i);

// Picks a line of a set, never the one it picked last time, with {c} replaced
// by who (or the default name for the user when who is empty). english, when
// given, gets the same line in English: what her voice says, whatever she shows.
class LinePicker {
 public:
  void pick(Lines set, Lang lang, const char* who, uint32_t random, char* out, size_t size, char* english = nullptr,
            size_t englishSize = 0);

 private:
  int8_t last_[size_t(Lines::Count)] = {};
};

}  // namespace ui::capri
