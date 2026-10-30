#pragma once

#include <cstdint>

#include "app/gfx/canvas.h"

namespace gfx {

// Opaque palette image as runs of one color per row, for pictures with large
// even areas (a pre-rendered backdrop): small in flash, drawn as plain fills.
// Made offline by tools/rig/scene.py as constexpr tables.
struct RleImage {
  uint16_t w = 0, h = 0;
  const uint16_t* rows = nullptr;  // per row: its first run in runs[]
  const uint8_t* runs = nullptr;   // pairs: length 1..255, palette index
  const Color* palette = nullptr;
};

// Draws img with its top-left corner at (x, y)
void drawRleImage(Canvas& c, const RleImage& img, int x, int y);

}  // namespace gfx
