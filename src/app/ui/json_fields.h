#pragma once

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app/gfx/color.h"

// Field helpers shared by the characters' settings codecs: colors as
// "#rrggbb" (stored as RGB565), floats clamped on the way in and rounded to
// three decimals on the way out.
namespace ui::json {

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float round3(float v) { return float(int(v * 1000.0f + (v < 0 ? -0.5f : 0.5f))) / 1000.0f; }

inline void hexOf(gfx::Color c, char out[8]) {
  uint16_t v = gfx::toRgb565(c);
  unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  snprintf(out, 8, "#%02x%02x%02x", (r * 255 + 15) / 31, (g * 255 + 31) / 63, (b * 255 + 15) / 31);
}

inline bool parseHex(const char* s, gfx::Color& out) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  unsigned rgb = 0;
  if (sscanf(s + 1, "%06x", &rgb) != 1) return false;
  out = gfx::rgb(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));
  return true;
}

inline void readFloat(JsonVariantConst v, float& dst, float lo, float hi) {
  if (v.is<float>()) dst = clampf(v.as<float>(), lo, hi);
}

inline void readColor(JsonVariantConst v, gfx::Color& dst) {
  gfx::Color c;
  if (v.is<const char*>() && parseHex(v.as<const char*>(), c)) dst = c;
}

}  // namespace ui::json
