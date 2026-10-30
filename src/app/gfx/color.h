#pragma once

#include <cstdint>

namespace gfx {

// RGB565 pixels are kept in the byte order the panel expects on the wire, so
// rendered buffers go to the display without conversion. SPI panels take
// big-endian pixels, which on little-endian CPUs means swapped bytes.
// A board whose transport wants native order builds with -DGFX_COLOR_SWAP=0.
#ifndef GFX_COLOR_SWAP
#define GFX_COLOR_SWAP 1
#endif

using Color = uint16_t;

constexpr uint16_t swap16(uint16_t v) { return uint16_t((v >> 8) | (v << 8)); }
constexpr Color fromRgb565(uint16_t c) { return GFX_COLOR_SWAP ? swap16(c) : c; }
constexpr uint16_t toRgb565(Color c) { return GFX_COLOR_SWAP ? swap16(c) : c; }

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
  return fromRgb565(uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)));
}

// Blends a toward b; t = 0 gives a, t = 32 gives b.
// Spreads the three channels into separate lanes so one multiply blends all.
inline Color mix(Color a, Color b, uint32_t t) {
  uint32_t ca = toRgb565(a), cb = toRgb565(b);
  uint32_t la = (ca | (ca << 16)) & 0x07E0F81Fu;
  uint32_t lb = (cb | (cb << 16)) & 0x07E0F81Fu;
  uint32_t l = ((la * (32 - t) + lb * t) >> 5) & 0x07E0F81Fu;
  return fromRgb565(uint16_t(l | (l >> 16)));
}

// mix() toward one fixed color at one fixed t, with the color expanded once
struct MixTo {
  uint32_t lane, keep;
  MixTo(Color c, uint32_t t) : keep(32 - t) {
    uint32_t v = toRgb565(c);
    lane = ((v | (v << 16)) & 0x07E0F81Fu) * t;
  }
  Color operator()(Color dst) const {
    uint32_t d = toRgb565(dst);
    uint32_t l = ((((d | (d << 16)) & 0x07E0F81Fu) * keep + lane) >> 5) & 0x07E0F81Fu;
    return fromRgb565(uint16_t(l | (l >> 16)));
  }
};

}  // namespace gfx
