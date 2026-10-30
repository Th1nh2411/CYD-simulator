#pragma once

#include <cstddef>
#include <cstdint>

namespace gfx {

// One glyph of a proportional 1-bit font
struct Glyph {
  uint8_t advance;  // pen movement, px
  uint8_t w, h;     // bitmap size
  int8_t x, y;      // bitmap offset from the pen: x right, y down from the line top
  uint32_t offset;  // first bit in the font's bit stream (rows top down, MSB first)
};

// Bitmap font over a sorted set of code points, made by tools/font/bdf2font.py
struct Font {
  const uint16_t* codes;  // sorted
  const Glyph* glyphs;    // parallel to codes
  const uint8_t* bits;
  uint16_t count;
  uint8_t height;    // line height, px
  uint8_t baseline;  // from the line top, px
  uint8_t above;     // px some glyphs (Vietnamese stacked marks) reach above the line top

  const Glyph* find(uint32_t code) const;  // nullptr if missing
};

// Korean 11 px pixel font: ASCII, KS X 1001 Hangul (2,350 syllables), jamo, symbols and Vietnamese.
// Derived from Galmuri11 by Lee Minseo, SIL Open Font License 1.1 (fonts/LICENSE-Galmuri.txt).
extern const Font kFontKo11;

// Pen advance of the first `bytes` bytes of a UTF-8 string (missing glyphs count as 0)
int advanceOf(const Font& f, const char* utf8, size_t bytes);

// Word-wraps UTF-8 text into lines (maxLines rows of lineBytes each) no wider
// than maxWidth px: breaks at spaces and '\n', inside words only when a word is
// too long. A cut-off last line ends in "...". Returns the number of lines.
int wrapText(const Font& f, const char* text, int maxWidth, char* lines, size_t lineBytes, int maxLines);

}  // namespace gfx
