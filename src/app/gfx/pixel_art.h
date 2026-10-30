#pragma once

#include <cstdint>

#include "app/gfx/canvas.h"

namespace gfx {

// Palette-indexed pixel art written as text, converted at compile time:
// each character's index in `legend` becomes the pixel's palette index
// (legend[0] = transparent). Rows are concatenated string literals.
template <int W, int H>
struct PixelArt {
  uint8_t px[W * H] = {};

  constexpr PixelArt(const char* legend, const char (&rows)[W * H + 1]) {
    for (int i = 0; i < W * H; i++) {
      int k = 0;
      while (legend[k] && legend[k] != rows[i]) k++;
      px[i] = uint8_t(legend[k] ? k : 0);
    }
  }
  constexpr IndexedImage image() const { return IndexedImage{W, H, px}; }
};

}  // namespace gfx
