#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "app/core/utf8.h"

namespace ui {

// Whether the word's last syllable has a final consonant (받침): 1 yes, 0 no,
// -1 unknown (it ends in something other than Hangul). Trailing punctuation is skipped.
inline int finalConsonant(const char* word) {
  int result = -1;
  for (const char* s = word; *s;) {
    uint32_t c = core::nextCodePoint(s);
    if (c >= 0xAC00 && c <= 0xD7A3) result = (c - 0xAC00) % 28 ? 1 : 0;
    else if (c != ' ' && c != '?' && c != '!' && c != '.' && c != ',') result = -1;
  }
  return result;
}

// Word followed by the particle that fits it: ("엠버", "은", "는") -> "엠버는";
// "Deploy?" -> "Deploy?은(는)" when the ending cannot be told
inline void withParticle(char* out, size_t size, const char* word, const char* withFinal, const char* without) {
  int f = finalConsonant(word);
  if (f < 0) snprintf(out, size, "%s%s(%s)", word, withFinal, without);
  else snprintf(out, size, "%s%s", word, f ? withFinal : without);
}

}  // namespace ui
