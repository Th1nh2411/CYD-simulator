#include "app/ui/blob/eye.h"

#include "app/ui/blob/blob_style.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui::blob {

namespace {

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// constexpr so the preset table is constant-initialized: CharacterConfig
// instances in other files' static constructors read it before dynamic init.
constexpr EyeShape make(float w, float h, float square = 0, float corner = 0.35f) {
  EyeShape s;
  s.w = w;
  s.h = h;
  s.square = square;
  s.corner = corner;
  return s;
}

constexpr EyeShape lids(EyeShape s, float top, float angle, float bottom = 0) {
  s.lidTop = top;
  s.lidAngle = angle;
  s.lidBottom = bottom;
  return s;
}

constexpr EyeShape arc(EyeShape s, float a) {
  s.arc = a;
  s.shine = 0;
  return s;
}

constexpr EyeShape shine(EyeShape s, float v) {
  s.shine = v;
  return s;
}

// Eyebrow pose per expression (only visible when a brow style is chosen)
constexpr EyeShape normalBrow(EyeShape s) { return s; }
constexpr EyeShape happyBrow(EyeShape s) {
  s.browRaise = 0.04f;
  s.browAngle = -0.12f;
  return s;
}
constexpr EyeShape surprisedBrow(EyeShape s) {
  s.browRaise = 0.10f;
  s.browAngle = -0.08f;
  return s;
}
constexpr EyeShape sleepyBrow(EyeShape s) {
  s.browRaise = -0.03f;
  return s;
}
constexpr EyeShape angryBrow(EyeShape s) {
  s.browRaise = -0.03f;
  s.browAngle = 0.50f;
  return s;
}
constexpr EyeShape sadBrow(EyeShape s) {
  s.browRaise = 0.03f;
  s.browAngle = -0.45f;
  return s;
}

// Order: Normal, Happy, Surprised, Sleepy, Angry, Sad
constexpr EyeSet kSets[] = {
    {"oval",
     {normalBrow(make(0.30f, 0.44f)), happyBrow(arc(make(0.34f, 0.34f), 1.0f)),
      surprisedBrow(shine(make(0.40f, 0.48f), 1.2f)), sleepyBrow(shine(lids(make(0.32f, 0.40f), 0.55f, 0.0f), 0.6f)),
      angryBrow(lids(make(0.30f, 0.40f), 0.32f, 0.45f)), sadBrow(lids(make(0.30f, 0.42f), 0.26f, -0.40f))}},
    {"robot",
     {normalBrow(make(0.34f, 0.40f, 1.0f, 0.35f)),
      happyBrow(shine(lids(make(0.36f, 0.32f, 1.0f, 0.5f), 0, 0, 0.45f), 0)),
      surprisedBrow(make(0.40f, 0.48f, 1.0f, 0.35f)), sleepyBrow(lids(make(0.34f, 0.40f, 1.0f, 0.35f), 0.55f, 0.0f)),
      angryBrow(lids(make(0.34f, 0.40f, 1.0f, 0.35f), 0.30f, 0.50f)),
      sadBrow(lids(make(0.34f, 0.40f, 1.0f, 0.35f), 0.25f, -0.45f))}},
    {"dot",
     {normalBrow(shine(make(0.17f, 0.19f), 0.8f)), happyBrow(arc(make(0.24f, 0.20f), 1.0f)),
      surprisedBrow(make(0.24f, 0.26f)), sleepyBrow(lids(make(0.19f, 0.17f), 0.50f, 0.0f)),
      angryBrow(lids(make(0.18f, 0.18f), 0.30f, 0.50f)), sadBrow(lids(make(0.18f, 0.19f), 0.26f, -0.45f))}},
    {"pill",
     {normalBrow(make(0.18f, 0.48f, 1.0f, 1.0f)), happyBrow(arc(make(0.24f, 0.34f, 1.0f, 1.0f), 1.0f)),
      surprisedBrow(make(0.24f, 0.52f, 1.0f, 1.0f)), sleepyBrow(lids(make(0.20f, 0.44f, 1.0f, 1.0f), 0.55f, 0.0f)),
      angryBrow(lids(make(0.20f, 0.44f, 1.0f, 1.0f), 0.30f, 0.50f)),
      sadBrow(lids(make(0.20f, 0.46f, 1.0f, 1.0f), 0.24f, -0.45f))}},
};

}  // namespace

EyeShape lerp(const EyeShape& a, const EyeShape& b, float t) {
  auto l = [t](float x, float y) { return x + (y - x) * t; };
  EyeShape r;
  r.w = l(a.w, b.w);
  r.h = l(a.h, b.h);
  r.square = l(a.square, b.square);
  r.corner = l(a.corner, b.corner);
  r.tilt = l(a.tilt, b.tilt);
  r.lidTop = l(a.lidTop, b.lidTop);
  r.lidAngle = l(a.lidAngle, b.lidAngle);
  r.lidBottom = l(a.lidBottom, b.lidBottom);
  r.arc = l(a.arc, b.arc);
  r.shine = l(a.shine, b.shine);
  r.browRaise = l(a.browRaise, b.browRaise);
  r.browAngle = l(a.browAngle, b.browAngle);
  return r;
}

bool nearlyEqual(const EyeShape& a, const EyeShape& b) {
  const float* x = &a.w;
  const float* y = &b.w;
  for (size_t i = 0; i < sizeof(EyeShape) / sizeof(float); i++) {
    if (fabsf(x[i] - y[i]) > 0.002f) return false;
  }
  return true;
}

int eyeSetCount() { return int(sizeof(kSets) / sizeof(kSets[0])); }

void CharacterConfig::useEyeSet(int index) {
  const EyeSet& set = eyeSet(index);
  strncpy(eyeSetName, set.name, sizeof(eyeSetName) - 1);
  eyeSetName[sizeof(eyeSetName) - 1] = '\0';
  for (size_t i = 0; i < kExpressionCount; i++) eyes[i] = set.shapes[i];
}

const EyeSet& eyeSet(int index) {
  return kSets[(index >= 0 && index < eyeSetCount()) ? index : 0];
}

int findEyeSet(const char* name) {
  for (int i = 0; i < eyeSetCount(); i++) {
    if (strcmp(kSets[i].name, name) == 0) return i;
  }
  return -1;
}

void drawEye(gfx::Canvas& c, const EyeShape& s, const EyeParams& p) {
  const float hw = std::max(0.5f, s.w * p.radius * 0.5f * p.squashX);
  const float hhOpen = std::max(0.5f, s.h * p.radius * 0.5f * p.squashY);
  const float hh = hhOpen + (std::max(1.0f, hhOpen * 0.12f) - hhOpen) * clamp01(p.blink);

  // Eye-local frame: +x points away from the face center
  const float dir = p.mirror ? -1.0f : 1.0f;
  const float ct = cosf(s.tilt), st = sinf(s.tilt);
  const float rr = s.corner * std::min(hw, hh);  // rounded-rect corner radius
  const float irx = 1.0f / hw, iry = 1.0f / hh, minR = std::min(hw, hh);
  const float lidSlope = tanf(s.lidAngle), lidNorm = 1.0f / sqrtf(1.0f + lidSlope * lidSlope);
  const float lidY = -hh + clamp01(s.lidTop) * 2 * hh;
  const float lowY = hh - clamp01(s.lidBottom) * 2 * hh;
  const bool useLids = s.lidTop > 0.001f || fabsf(s.lidAngle) > 0.001f;
  // Carve ellipse sits just below the eye at arc = 0 and rises to leave a thin band at 1
  const float carveDy = 2 * hh * (1.0f - 0.65f * clamp01(s.arc));
  const float carveIrx = 1.0f / (hw * 1.25f), carveMin = std::min(hw * 1.25f, hh);

  // Signed distance (px) from a pixel center to the eye outline, negative inside
  auto eyeDist = [&](float px, float py) {
    float lx = (px - p.cx) * dir, ly = py - p.cy;
    float qx = lx * ct + ly * st, qy = -lx * st + ly * ct;  // undo tilt
    float ex = qx * irx, ey = qy * iry;
    float d = (sqrtf(ex * ex + ey * ey) - 1.0f) * minR;
    if (s.square > 0.001f) {
      float bx = fabsf(qx) - (hw - rr), by = fabsf(qy) - (hh - rr);
      float ox = std::max(bx, 0.0f), oy = std::max(by, 0.0f);
      float dRect = sqrtf(ox * ox + oy * oy) + std::min(std::max(bx, by), 0.0f) - rr;
      d += (dRect - d) * s.square;
    }
    if (useLids) d = std::max(d, (lidY - lidSlope * lx - ly) * lidNorm);
    if (s.lidBottom > 0.001f) d = std::max(d, ly - lowY);
    if (s.arc > 0.001f) {
      float cx = qx * carveIrx, cy = (qy - carveDy) * iry;
      d = std::max(d, -(sqrtf(cx * cx + cy * cy) - 1.0f) * carveMin);
    }
    return d;
  };
  auto blend = [](gfx::Color& px, gfx::Color color, float coverage) {
    if (coverage > 0.004f) px = gfx::mix(px, color, uint32_t(coverage * 32.0f + 0.5f));
  };

  const float reach = (s.tilt != 0 ? sqrtf(hw * hw + hh * hh) : std::max(hw, hh)) + 1.5f;
  const gfx::Rect& a = c.area();
  int x0 = std::max<int>(a.x, int(floorf(p.cx - reach))), x1 = std::min<int>(a.right(), int(ceilf(p.cx + reach)));
  int y0 = std::max<int>(a.y, int(floorf(p.cy - reach))), y1 = std::min<int>(a.bottom(), int(ceilf(p.cy + reach)));
  for (int y = y0; y < y1; y++) {
    gfx::Color* row = c.rowPtr(y) + (x0 - a.x);
    for (int x = x0; x < x1; x++, row++) blend(*row, p.color, clamp01(0.5f - eyeDist(x + 0.5f, y + 0.5f)));
  }

  // Catchlight: same side on both eyes as if lit from one direction, and it
  // slides inside the eye toward the gaze so looking around reads clearly.
  // Clipped to the eye outline so it never leaks past the lids.
  // It sits in the visible part of the eye, so it comes down with the upper lid.
  if (s.shine < 0.05f || p.blink > 0.4f || s.arc > 0.5f) return;
  float offX = (0.30f + p.gazeX * p.shineTravel) * hw;
  float top = useLids ? lidY - lidSlope * (offX * dir) : -hh;  // lid edge right above the catchlight
  float bottom = s.lidBottom > 0.001f ? lowY : hh;
  float visible = bottom - top;
  if (visible < 2.0f) return;
  float sr = std::min(0.30f * std::min(hw, hhOpen) * s.shine, 0.32f * visible);
  float offY = top + (0.31f + 0.5f * p.gazeY * p.shineTravel) * visible;
  offY = std::max(top + sr, std::min(bottom - sr, offY));
  float sx = p.cx + offX;
  float sy = p.cy + offY;
  int sx0 = std::max<int>(a.x, int(floorf(sx - sr - 1))), sx1 = std::min<int>(a.right(), int(ceilf(sx + sr + 1)));
  int sy0 = std::max<int>(a.y, int(floorf(sy - sr - 1))), sy1 = std::min<int>(a.bottom(), int(ceilf(sy + sr + 1)));
  for (int y = sy0; y < sy1; y++) {
    gfx::Color* row = c.rowPtr(y) + (sx0 - a.x);
    for (int x = sx0; x < sx1; x++, row++) {
      float dx = x + 0.5f - sx, dy = y + 0.5f - sy;
      float d = std::max(sqrtf(dx * dx + dy * dy) - sr, eyeDist(x + 0.5f, y + 0.5f) + 0.8f);
      blend(*row, p.shine, clamp01(0.5f - d));
    }
  }
}

}  // namespace ui::blob
