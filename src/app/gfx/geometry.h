#pragma once

#include <algorithm>
#include <cstdint>

namespace gfx {

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;

  constexpr Rect() = default;
  constexpr Rect(int x_, int y_, int w_, int h_)
      : x(int16_t(x_)), y(int16_t(y_)), w(int16_t(w_)), h(int16_t(h_)) {}

  bool empty() const { return w <= 0 || h <= 0; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  int area() const { return empty() ? 0 : int(w) * h; }
  bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }

  Rect intersect(const Rect& o) const {
    int l = std::max<int>(x, o.x), t = std::max<int>(y, o.y);
    int r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
    return (r > l && b > t) ? Rect(l, t, r - l, b - t) : Rect();
  }
  bool intersects(const Rect& o) const { return !intersect(o).empty(); }

  Rect unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int l = std::min<int>(x, o.x), t = std::min<int>(y, o.y);
    int r = std::max(right(), o.right()), b = std::max(bottom(), o.bottom());
    return Rect(l, t, r - l, b - t);
  }

  Rect inflate(int d) const { return Rect(x - d, y - d, w + 2 * d, h + 2 * d); }

  bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
  bool operator!=(const Rect& o) const { return !(*this == o); }
};

}  // namespace gfx
