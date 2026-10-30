#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

// Decodes one code point and advances s; malformed bytes come out as U+FFFD
inline uint32_t nextCodePoint(const char*& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
  uint32_t c = p[0];
  int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
  if (extra < 0) {
    s++;
    return 0xFFFD;
  }
  c &= extra ? (0x3F >> extra) : 0x7F;
  for (int i = 1; i <= extra; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      s += i;
      return 0xFFFD;
    }
    c = (c << 6) | (p[i] & 0x3F);
  }
  s += 1 + extra;
  return c;
}

// Length of the longest prefix of s that fits in max bytes without splitting a character
inline size_t utf8Fit(const char* s, size_t max) {
  size_t n = 0;
  while (s[n] && n < max) n++;
  if (s[n] && n > 0) {
    while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) n--;  // s[n] continues a character
  }
  return n;
}

// strncpy for UTF-8: always terminated, never cuts a character in half
inline void copyUtf8(char* dst, size_t size, const char* src) {
  size_t n = utf8Fit(src, size - 1);
  for (size_t i = 0; i < n; i++) dst[i] = src[i];
  dst[n] = '\0';
}

}  // namespace core
