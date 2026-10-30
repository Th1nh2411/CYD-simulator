#include "app/ui/blob/blob_layer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui::blob {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kPolyPoints = 2 * FluidBody::kNodes;

struct PolyTables {
  float c[kPolyPoints], s[kPolyPoints];
  PolyTables() {
    for (int j = 0; j < kPolyPoints; j++) {
      float a = 2 * kPi * float(j) / kPolyPoints;
      c[j] = cosf(a);
      s[j] = sinf(a);
    }
  }
};

const PolyTables& tables() {
  static PolyTables t;
  return t;
}

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

inline uint32_t alpha32(float coverage, uint32_t opacity) {
  return uint32_t(coverage * float(opacity) + 0.5f);
}

}  // namespace

void BlobLayer::setConfig(const CharacterConfig& cfg) {
  style_ = cfg.style;
  accessories_ = cfg.accessories;
  brows_ = cfg.brows;
  dirtyStyle_ = true;  // forces a rebuild on the next update
}

void BlobLayer::update(const BlobAnimator& a) {
  const FluidBody& body = a.body();
  const float R = body.radius(), cx = a.centerX(), cy = a.centerY();
  const PolyTables& t = tables();

  // Polygon: sim nodes plus Catmull-Rom midpoints for a smoother outline
  float nx[kPoly], ny[kPoly];
  float maxMove = 0;
  for (int j = 0; j < kPoly; j++) {
    int i = j / 2;
    float d = (j & 1) ? (-body.offset(i - 1) + 9 * body.offset(i) + 9 * body.offset(i + 1) -
                         body.offset(i + 2)) / 16.0f
                      : body.offset(i);
    float r = R + d;
    nx[j] = cx + r * t.c[j];
    ny[j] = cy + r * t.s[j];
    maxMove = std::max(maxMove, std::max(fabsf(nx[j] - px_[j]), fabsf(ny[j] - py_[j])));
  }

  const Face& f = a.face();
  bool faceChanged = fabsf(f.blink - face_.blink) > 0.02f || fabsf(f.gazeX - face_.gazeX) > 0.01f ||
                     fabsf(f.gazeY - face_.gazeY) > 0.01f || !nearlyEqual(f.eye, face_.eye);
  const AccessoryMotion& am = a.accessoryMotion();
  bool accessoriesMoved = fabsf(am.flap - accMotion_.flap) > 0.004f || fabsf(am.wave - accMotion_.wave) > 0.004f;
  for (size_t i = 0; i < kSlotCount; i++) accessoriesMoved |= fabsf(am.sway[i] - accMotion_.sway[i]) > 0.004f;
  bool droplets = false;
  for (int i = 0; i < BlobAnimator::kMaxDroplets; i++) droplets |= a.droplet(i).active;
  bool dropletsChanged = droplets || hadDroplets_;  // one more rebuild erases the last one
  hadDroplets_ = droplets;
  if (built_ && !dirtyStyle_ && maxMove < 0.2f && !faceChanged && !accessoriesMoved && !dropletsChanged) {
    return;  // nothing visible moved
  }
  built_ = true;
  dirtyStyle_ = false;
  face_ = f;
  accMotion_ = am;
  memcpy(px_, nx, sizeof(px_));
  memcpy(py_, ny, sizeof(py_));

  minX_ = minY_ = 1e9f;
  maxX_ = maxY_ = -1e9f;
  for (int j = 0; j < kPoly; j++) {
    minX_ = std::min(minX_, px_[j]);
    maxX_ = std::max(maxX_, px_[j]);
    minY_ = std::min(minY_, py_[j]);
    maxY_ = std::max(maxY_, py_[j]);
  }
  buildSpans();

  // Face rides on the head: anchored to the body center and sized from the
  // head's extents (capped), so a long tail does not drag the face along
  const float cap = 1.4f * R;
  float rR = std::min(body.radiusAt(0), cap), rL = std::min(body.radiusAt(kPi), cap);
  float rU = std::min(body.radiusAt(-kPi / 2), cap), rD = std::min(body.radiusAt(kPi / 2), cap);
  float hw = std::max(0.6f * R, 0.5f * (rR + rL)), hh = std::max(0.6f * R, 0.5f * (rU + rD));
  float fcx = cx + std::max(-0.15f * R, std::min(0.15f * R, 0.25f * (rR - rL)));
  float fcy = cy + std::max(-0.15f * R, std::min(0.15f * R, 0.25f * (rD - rU)));
  float gx = f.gazeX * style_.gazeReach * R, gy = f.gazeY * style_.gazeReach * R * 0.8f;
  for (int e = 0; e < 2; e++) {
    float side = e ? 1.0f : -1.0f;
    EyeParams& eye = eyes_[e];
    eye.cx = fcx + side * style_.eyeSpacing * hw + gx;
    eye.cy = fcy + style_.eyeHeight * hh + gy;
    eye.radius = R;
    eye.squashX = 0.5f + 0.5f * hw / R;
    eye.squashY = 0.5f + 0.5f * hh / R;
    eye.mirror = e == 0;
    eye.blink = f.blink;
    eye.gazeX = f.gazeX;
    eye.gazeY = f.gazeY;
    eye.shineTravel = style_.shineTravel;
    eye.color = style_.eye;
    eye.shine = style_.shine;

    float eyeHalfH = f.eye.h * R * 0.5f * eye.squashY;
    Ellipse& ch = cheeks_[e];
    ch.x = fcx + side * (style_.eyeSpacing + 0.17f) * hw + gx * 0.5f;
    ch.y = eye.cy + eyeHalfH * 1.25f;
    ch.rx = 0.16f * R;
    ch.ry = 0.085f * R;
  }
  highlight_ = Ellipse{fcx - 0.40f * hw, fcy - 0.52f * hh, 0.28f * hw, 0.15f * hh};

  BodyFrame frame;
  frame.cx = cx;
  frame.cy = cy;
  frame.radius = R;
  frame.body = &body;
  frame.bodyColor = style_.body;
  frame.bodyOpacity = style_.opacity;
  buildAccessories(accessories_, am, frame, backPrims_, frontPrims_);
  buildBrows(brows_, f.eye, eyes_, frontPrims_);
  for (int i = 0; i < BlobAnimator::kMaxDroplets; i++) {
    const Droplet& d = a.droplet(i);
    if (d.active) addCircle(backPrims_, d.x, d.y, d.r, style_.body, style_.opacity);
  }

  int left = int(floorf(minX_)) - 1;
  bodyBox_ = gfx::Rect(left, top_, int(ceilf(maxX_)) + 1 - left, rows_);
  // Eyes are normally inside the body, but edited shapes may poke out
  gfx::Rect b = bodyBox_.unite(backPrims_.bounds).unite(frontPrims_.bounds);
  for (const EyeParams& eye : eyes_) {
    float reach = std::max(f.eye.w * eye.squashX, f.eye.h * eye.squashY) * R * 0.75f + 2;
    b = b.unite(gfx::Rect(int(eye.cx - reach), int(eye.cy - reach), int(2 * reach) + 1, int(2 * reach) + 1));
  }
  if (b == bounds()) invalidate();
  else setBounds(b);
}

// Rasterizes the polygon into per-subrow [left, right) spans
void BlobLayer::buildSpans() {
  top_ = int(floorf(minY_));
  rows_ = std::min(kMaxRows, int(ceilf(maxY_)) - top_ + 1);
  int subs = rows_ * kSub;
  for (int s = 0; s < subs; s++) {
    spanL_[s] = 1e9f;
    spanR_[s] = -1e9f;
  }
  for (int j = 0; j < kPoly; j++) {
    float x0 = px_[j], y0 = py_[j];
    float x1 = px_[(j + 1) % kPoly], y1 = py_[(j + 1) % kPoly];
    if (y0 == y1) continue;
    float ylo = std::min(y0, y1), yhi = std::max(y0, y1);
    int s0 = std::max(0, int(ceilf((ylo - top_) * kSub - 0.5f)));
    int s1 = std::min(subs, int(ceilf((yhi - top_) * kSub - 0.5f)));
    float slope = (x1 - x0) / (y1 - y0);
    for (int s = s0; s < s1; s++) {
      float ys = top_ + (s + 0.5f) / kSub;
      float x = x0 + (ys - y0) * slope;
      spanL_[s] = std::min(spanL_[s], x);
      spanR_[s] = std::max(spanR_[s], x);
    }
  }
}

bool BlobLayer::rowInterior(int y, float& left, float& right) const {
  int r = y - top_;
  if (r < 0 || r >= rows_) return false;
  left = -1e9f;
  right = 1e9f;
  for (int k = 0; k < kSub; k++) {
    float l = spanL_[r * kSub + k], rr = spanR_[r * kSub + k];
    if (l >= rr) return false;
    left = std::max(left, l);
    right = std::min(right, rr);
  }
  return right > left;
}

void BlobLayer::draw(gfx::Canvas& c) const {
  if (!built_) return;
  paintPrims(c, backPrims_);  // wings, fins, arms: behind the translucent body
  drawBody(c);
  if (style_.highlightAlpha) drawSoftEllipse(c, highlight_, style_.highlight, style_.highlightAlpha);
  if (style_.cheekAlpha) {
    for (const Ellipse& ch : cheeks_) drawSoftEllipse(c, ch, style_.cheek, style_.cheekAlpha);
  }
  for (const EyeParams& eye : eyes_) drawEye(c, face_.eye, eye);
  paintPrims(c, frontPrims_);  // head items and held items
}

void BlobLayer::drawBody(gfx::Canvas& c) const {
  const gfx::Rect& a = c.area();
  int y0 = std::max<int>(a.y, top_), y1 = std::min<int>(a.bottom(), top_ + rows_);
  const uint32_t opacity = style_.opacity;
  const gfx::Color color = style_.body;
  const gfx::MixTo blend(color, opacity);

  for (int y = y0; y < y1; y++) {
    const float* L = &spanL_[(y - top_) * kSub];
    const float* R = &spanR_[(y - top_) * kSub];
    float lmin = 1e9f, lmax = -1e9f, rmin = 1e9f, rmax = -1e9f;
    int valid = 0;
    for (int k = 0; k < kSub; k++) {
      if (L[k] >= R[k]) continue;
      valid++;
      lmin = std::min(lmin, L[k]);
      lmax = std::max(lmax, L[k]);
      rmin = std::min(rmin, R[k]);
      rmax = std::max(rmax, R[k]);
    }
    if (!valid) continue;

    int x0 = std::max<int>(a.x, int(floorf(lmin))), x1 = std::min<int>(a.right(), int(ceilf(rmax)));
    if (x1 <= x0) continue;
    // Fully covered interior; partial rows (top/bottom) are all edge
    int in0 = x1, in1 = x1;
    if (valid == kSub) {
      in0 = int(ceilf(lmax));
      in1 = int(floorf(rmin));
    }
    int ia = std::max(x0, std::min(in0, x1)), ib = std::max(ia, std::min(in1, x1));

    gfx::Color* row = c.rowPtr(y);
    const int ox = a.x;
    auto edge = [&](int x) {
      float cov = 0;
      for (int k = 0; k < kSub; k++) {
        float l = std::max(float(x), L[k]), r = std::min(float(x + 1), R[k]);
        if (r > l) cov += r - l;
      }
      uint32_t al = alpha32(cov / kSub, opacity);
      if (al) row[x - ox] = gfx::mix(row[x - ox], color, al);
    };
    for (int x = x0; x < ia; x++) edge(x);
    gfx::Color* p = row + (ia - ox);
    gfx::Color* end = row + (ib - ox);
    if (opacity >= 32) {
      for (; p < end; p++) *p = color;
    } else {
      for (; p < end; p++) *p = blend(*p);
    }
    for (int x = ib; x < x1; x++) edge(x);
  }
}

// Radial falloff, clipped to the body interior
void BlobLayer::drawSoftEllipse(gfx::Canvas& c, const Ellipse& e, gfx::Color color,
                                uint32_t alpha) const {
  const gfx::Rect& a = c.area();
  int x0 = std::max<int>(a.x, int(floorf(e.x - e.rx))), x1 = std::min<int>(a.right(), int(ceilf(e.x + e.rx)));
  int y0 = std::max<int>(a.y, int(floorf(e.y - e.ry))), y1 = std::min<int>(a.bottom(), int(ceilf(e.y + e.ry)));
  const float irx = 1.0f / e.rx, iry = 1.0f / e.ry;
  for (int y = y0; y < y1; y++) {
    float left, right;
    if (!rowInterior(y, left, right)) continue;
    int xa = std::max(x0, int(ceilf(left))), xb = std::min(x1, int(floorf(right)));
    if (xb <= xa) continue;
    gfx::Color* row = c.rowPtr(y) + (xa - a.x);
    float dy = (y + 0.5f - e.y) * iry;
    for (int x = xa; x < xb; x++, row++) {
      float dx = (x + 0.5f - e.x) * irx;
      float w = clamp01((1.0f - sqrtf(dx * dx + dy * dy)) * 2.0f);
      uint32_t al = alpha32(w, alpha);
      if (al) *row = gfx::mix(*row, color, al);
    }
  }
}

}  // namespace ui::blob
