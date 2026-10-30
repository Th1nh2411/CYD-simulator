#pragma once

#include <algorithm>
#include <cmath>

#include "app/gfx/canvas.h"

// Signed distance functions (pixels, negative inside) for anti-aliased
// procedural shapes, plus a painter that fills one over its bounding box.
// Screen space, y down. Formulas after Inigo Quilez's 2D distance functions.
namespace gfx::sdf {

struct V2 {
  float x = 0, y = 0;
};

inline float len(float x, float y) { return sqrtf(x * x + y * y); }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

inline float circle(float px, float py, float cx, float cy, float r) { return len(px - cx, py - cy) - r; }

// Ellipse rotated by an angle given as its cosine/sine; takes inverse radii and
// the smaller radius. Approximate, but never overestimates (safe for skipping).
inline float ellipse(float px, float py, float cx, float cy, float irx, float iry, float ca, float sa,
                     float minR) {
  float dx = px - cx, dy = py - cy;
  float qx = (dx * ca + dy * sa) * irx, qy = (-dx * sa + dy * ca) * iry;
  return (sqrtf(qx * qx + qy * qy) - 1.0f) * minR;
}

// Segment a-b with radius r
inline float capsule(float px, float py, V2 a, V2 b, float r) {
  float pax = px - a.x, pay = py - a.y, bax = b.x - a.x, bay = b.y - a.y;
  float dd = bax * bax + bay * bay;
  float h = dd > 0 ? clamp01((pax * bax + pay * bay) / dd) : 0;
  return len(pax - bax * h, pay - bay * h) - r;
}

inline float triangle(float px, float py, V2 p0, V2 p1, V2 p2) {
  float e0x = p1.x - p0.x, e0y = p1.y - p0.y, e1x = p2.x - p1.x, e1y = p2.y - p1.y;
  float e2x = p0.x - p2.x, e2y = p0.y - p2.y;
  float v0x = px - p0.x, v0y = py - p0.y, v1x = px - p1.x, v1y = py - p1.y;
  float v2x = px - p2.x, v2y = py - p2.y;
  auto proj = [](float vx, float vy, float ex, float ey, float& qx, float& qy) {
    float h = clamp01((vx * ex + vy * ey) / (ex * ex + ey * ey));
    qx = vx - ex * h;
    qy = vy - ey * h;
  };
  float q0x, q0y, q1x, q1y, q2x, q2y;
  proj(v0x, v0y, e0x, e0y, q0x, q0y);
  proj(v1x, v1y, e1x, e1y, q1x, q1y);
  proj(v2x, v2y, e2x, e2y, q2x, q2y);
  float s = (e0x * e2y - e0y * e2x) > 0 ? 1.0f : -1.0f;
  float dx = std::min({q0x * q0x + q0y * q0y, q1x * q1x + q1y * q1y, q2x * q2x + q2y * q2y});
  float dy = std::min({s * (v0x * e0y - v0y * e0x), s * (v1x * e1y - v1y * e1x), s * (v2x * e2y - v2y * e2x)});
  return -sqrtf(dx) * (dy > 0 ? 1.0f : -1.0f);
}

// Upright heart, (cx, cy) = middle, size = full height
inline float heart(float px, float py, float cx, float cy, float size) {
  // Unit heart spans y 0 (tip) .. ~1 (top lobes), y up
  float x = fabsf(px - cx) / size, y = (cy + size * 0.5f - py) / size;
  float d;
  if (y + x > 1.0f) {
    d = len(x - 0.25f, y - 0.75f) - 0.35355339f;
  } else {
    float m = 0.5f * std::max(x + y, 0.0f);
    float a = (x) * (x) + (y - 1.0f) * (y - 1.0f), b = (x - m) * (x - m) + (y - m) * (y - m);
    d = sqrtf(std::min(a, b)) * (x - y > 0 ? 1.0f : -1.0f);
  }
  return d * size;
}

// Upright five-point star: outer radius r, inner/outer ratio rf
inline float star5(float px, float py, float cx, float cy, float r, float rf) {
  const float k1x = 0.809016994f, k1y = -0.587785252f, k2x = -k1x, k2y = k1y;
  float x = fabsf(px - cx), y = cy - py;  // y up
  float d1 = std::max(k1x * x + k1y * y, 0.0f);
  x -= 2 * d1 * k1x;
  y -= 2 * d1 * k1y;
  float d2 = std::max(k2x * x + k2y * y, 0.0f);
  x -= 2 * d2 * k2x;
  y -= 2 * d2 * k2y;
  x = fabsf(x);
  y -= r;
  float bax = rf * -k1y, bay = rf * k1x - 1.0f;
  float h = std::max(0.0f, std::min(r, (x * bax + y * bay) / (bax * bax + bay * bay)));
  return len(x - bax * h, y - bay * h) * ((y * bax - x * bay) > 0 ? 1.0f : -1.0f);
}

// Fills dist(x, y) <= 0 inside box with anti-aliased edges; alpha 0..32.
// dist must not overestimate: a pixel at distance d guarantees the next
// floor(d - 0.5) pixels share its side of the edge, so runs far outside are
// skipped and runs deep inside are filled without evaluating dist. Cost then
// scales with the outline length instead of the box area.
template <typename F>
void paint(Canvas& c, const Rect& box, Color color, uint32_t alpha, F dist) {
  Rect v = box.intersect(c.area());
  if (v.empty() || !alpha) return;
  const float a = float(alpha);
  const MixTo blend(color, alpha);
  const int ox = c.area().x;
  for (int y = v.y; y < v.bottom(); y++) {
    Color* const base = c.rowPtr(y);
    auto row = [base, ox](int x) -> Color& { return base[x - ox]; };
    const float py = y + 0.5f;
    int x = v.x;
    while (x < v.right()) {
      float d = dist(x + 0.5f, py);
      if (d >= 1.0f) {  // outside: skip what is certainly outside too
        x += int(d - 0.5f) + 1;
      } else if (d <= -1.0f) {  // inside: fill what is certainly inside too
        int end = std::min<int>(v.right(), x + int(-d - 0.5f) + 1);
        if (alpha >= 32) {
          for (; x < end; x++) row(x) = color;
        } else {
          for (; x < end; x++) row(x) = blend(row(x));
        }
      } else {
        uint32_t t = uint32_t(clamp01(0.5f - d) * a + 0.5f);
        if (t) row(x) = t >= 32 ? color : mix(row(x), color, t);
        x++;
      }
    }
  }
}

}  // namespace gfx::sdf
