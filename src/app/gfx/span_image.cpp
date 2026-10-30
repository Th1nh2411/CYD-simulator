#include "app/gfx/span_image.h"

#include <algorithm>

namespace gfx {

void ditherColor(float r, float g, float b, Color* out) {
  // Thresholds of the 2x2 Bayer matrix, as fractions of one 565 step
  static constexpr float kBayer[4] = {0.125f, 0.625f, 0.875f, 0.375f};
  auto level = [](float v, float step, float t) { return uint8_t(std::min(255.0f, std::max(0.0f, v + t * step))); };
  for (int k = 0; k < 4; k++) {
    out[k * 256] = rgb(level(r, 8, kBayer[k]), level(g, 4, kBayer[k]), level(b, 8, kBayer[k]));
  }
}

void drawSpanRow(Canvas& c, const SpanImage& img, int row, int x, int y, const SpanPalette& pal, int dx, int dy) {
  if (unsigned(row) >= img.h) return;
  const Rect& area = c.area();
  const uint8_t* p = img.data + img.rows[row];
  Color* out = c.rowPtr(y) - area.x;
  const Color* colors = pal.colors + ((row + dy) & 1) * 512;
  const int phase = x - dx;  // screen x of the picture's column 0
  for (int n = *p++; n > 0; n--) {
    x += p[0];
    const int len = p[1];
    const uint8_t* texels = p + 2;
    p += 2 + len;
    const int a = std::max<int>(x, area.x), b = std::min<int>(x + len, area.right());
    for (int i = a; i < b; i++) {
      const uint8_t t = texels[i - x];
      const uint32_t o = pal.opacity[t];
      const Color color = colors[((i - phase) & 1) * 256 + t];
      if (o >= 32) out[i] = color;
      else if (o) out[i] = mix(out[i], color, o);
    }
    x += len;
  }
}

bool spanOpaqueAt(const SpanImage& img, int x, int y, const SpanPalette& pal) {
  if (unsigned(y) >= img.h) return false;
  const uint8_t* p = img.data + img.rows[y];
  int at = 0;
  for (int n = *p++; n > 0; n--) {
    at += p[0];
    const int len = p[1];
    if (x >= at && x < at + len) return pal.opacity[p[2 + x - at]] != 0;
    at += len;
    p += 2 + len;
  }
  return false;
}

}  // namespace gfx
