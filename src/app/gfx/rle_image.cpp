#include "app/gfx/rle_image.h"

#include <algorithm>

namespace gfx {

void drawRleImage(Canvas& c, const RleImage& img, int x, int y) {
  const Rect& area = c.area();
  const int y0 = std::max<int>(y, area.y), y1 = std::min<int>(y + img.h, area.bottom());
  const int left = area.x, right = area.right();
  for (int sy = y0; sy < y1; sy++) {
    const uint8_t* run = img.runs + img.rows[sy - y];
    Color* row = c.rowPtr(sy);
    for (int px = x; px < right && px < x + img.w; run += 2) {
      const int a = std::max(px, left), b = std::min(px + run[0], right);
      px += run[0];
      if (b <= a) continue;
      const Color color = img.palette[run[1]];
      Color* p = row + (a - left);
      for (int i = b - a; i > 0; i--) *p++ = color;
    }
  }
}

}  // namespace gfx
