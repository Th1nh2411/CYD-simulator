#pragma once

#include <cstdint>

#include "app/gfx/color.h"
#include "app/gfx/font.h"
#include "app/gfx/geometry.h"

namespace gfx {

// Engine-native sprite format: one palette index per pixel, row-major,
// index 0 = transparent. Produced offline by the asset converter.
struct IndexedImage {
  uint16_t w = 0, h = 0;
  const uint8_t* pixels = nullptr;
};

// Drawing surface over one strip of the screen. All coordinates are absolute
// screen coordinates; everything is clipped to area().
class Canvas {
 public:
  void reset(Color* buf, const Rect& area) {
    buf_ = buf;
    area_ = area;
  }
  const Rect& area() const { return area_; }

  void fillRect(const Rect& r, Color c);
  void fillRoundRect(const Rect& r, int radius, Color c);
  // Vertical gradient spanning r (top row = top, bottom row = bottom)
  void fillGradientV(const Rect& r, Color top, Color bottom);
  // Darkens pixels inside the ellipse bounded by r; amount 0..32
  void darkenEllipse(const Rect& r, uint32_t amount);
  void drawImage(const IndexedImage& img, const Color* palette, int x, int y, int scale, bool flipX);
  // 5x7 font, transparent background
  void drawText(const char* text, int x, int y, Color c, int scale);
  // Proportional font, UTF-8, transparent background; y is the top of the line.
  // Draws at most maxChars characters (typewriter effects); returns the pen x after them.
  // Characters the font lacks show as a box.
  int drawText(const Font& f, const char* utf8, int x, int y, Color c, int scale = 1,
               int maxChars = 1 << 30);

  static int textWidth(const char* text, int scale);
  static int textHeight(int scale);
  static int textWidth(const Font& f, const char* utf8, int scale = 1);

  // Raw access for custom per-pixel drawing: pixel (x, y) is rowPtr(y)[x - area().x].
  // Caller keeps x and y inside area().
  Color* rowPtr(int y) { return buf_ + (y - area_.y) * area_.w; }

 private:
  Color* at(int x, int y) { return buf_ + (y - area_.y) * area_.w + (x - area_.x); }
  void span(int y, int x0, int x1, Color c);  // fills [x0, x1) on row y, clipped

  Color* buf_ = nullptr;
  Rect area_;
};

}  // namespace gfx
