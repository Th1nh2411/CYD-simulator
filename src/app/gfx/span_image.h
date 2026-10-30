#pragma once

#include <cstdint>

#include "app/gfx/canvas.h"

namespace gfx {

// Palette picture with holes, row by row as spans of texels between skipped
// (transparent) gaps: for painted art, whose one-color runs are too short for
// RLE but which leaves much of its box empty. Images may share rows (the same
// bytes for rows that do not change between poses). Made offline by
// tools/rig/portrait.py as constexpr tables.
struct SpanImage {
  uint16_t w = 0, h = 0;
  const uint32_t* rows = nullptr;  // per row: where it starts in data
  const uint8_t* data = nullptr;   // per row: span count, then per span: gap 0..255, length 0..255, texels
};

// Colors for a SpanImage's texels and their opacity, 0 .. 32. The colors come
// as four tables of 256, one per cell of a 2x2 ordered dither (y & 1) * 2 +
// (x & 1): soft gradients (skin) would band into contour lines at 16 bits.
struct SpanPalette {
  const Color* colors = nullptr;
  const uint8_t* opacity = nullptr;
};

// Makes the four dithered colors of a palette entry from its 8-bit channels,
// into out[0], out[256], out[512] and out[768]
void ditherColor(float r, float g, float b, Color* out);

// Draws row `row` of img with its left end at (x, y); y must lie in c.area().
// (dx, dy): where the image lies in the picture it belongs to, so that the
// dither stays put on the picture.
void drawSpanRow(Canvas& c, const SpanImage& img, int row, int x, int y, const SpanPalette& pal, int dx = 0,
                 int dy = 0);

// Whether texel (x, y) of img is drawn at all
bool spanOpaqueAt(const SpanImage& img, int x, int y, const SpanPalette& pal);

}  // namespace gfx
