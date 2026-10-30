#include "app/gfx/font.h"

#include <cstring>

#include "app/core/utf8.h"

namespace gfx {

const Glyph* Font::find(uint32_t code) const {
  int lo = 0, hi = int(count) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (codes[mid] == code) return &glyphs[mid];
    if (codes[mid] < code) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

int advanceOf(const Font& f, const char* utf8, size_t bytes) {
  int w = 0;
  for (const char* s = utf8, *end = utf8 + bytes; s < end;) {
    const Glyph* g = f.find(core::nextCodePoint(s));
    if (g) w += g->advance;
  }
  return w;
}

namespace {
// Bytes of the longest prefix of s (whole characters, at least one) within maxWidth and maxBytes
size_t fitPrefix(const Font& f, const char* s, size_t len, int maxWidth, size_t maxBytes) {
  size_t n = 0;
  while (n < len && n < maxBytes) {
    const char* next = s + n;
    core::nextCodePoint(next);
    size_t m = size_t(next - s);
    if (m > maxBytes || (n > 0 && advanceOf(f, s, m) > maxWidth)) break;
    n = m;
  }
  return n;
}
}  // namespace

int wrapText(const Font& f, const char* text, int maxWidth, char* lines, size_t lineBytes, int maxLines) {
  int count = 0;
  const char* p = text;
  while (*p && count < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;
    size_t len = strcspn(p, "\n");
    size_t take = fitPrefix(f, p, len, maxWidth, lineBytes - 1);
    if (take < len) {
      size_t cut = take;
      while (cut > 0 && p[cut] != ' ') cut--;
      if (cut > 0) take = cut;  // break at the last space that fits
    }
    char* line = lines + size_t(count) * lineBytes;
    memcpy(line, p, take);
    line[take] = '\0';
    count++;
    p += take;
    if (*p == '\n') p++;
  }
  while (*p == ' ' || *p == '\n') p++;
  if (*p && count == maxLines && count > 0) {  // cut off: end the last line with "..."
    char* last = lines + size_t(count - 1) * lineBytes;
    size_t keep = strlen(last);
    while (keep > 0 && (keep + 3 >= lineBytes || advanceOf(f, last, keep) + advanceOf(f, "...", 3) > maxWidth)) {
      keep--;
      while (keep > 0 && (uint8_t(last[keep]) & 0xC0) == 0x80) keep--;
    }
    strcpy(last + keep, "...");
  }
  return count;
}

}  // namespace gfx
