#include "app/ui/blob/fluid_body.h"

#include <algorithm>
#include <cmath>

namespace ui::blob {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kMaxSubstep = 1.0f / 240.0f;
constexpr int kMaxSubsteps = 12;
constexpr float kTailTaper = 1.6f;  // > 1 gives slightly concave tail sides
// Share of the tail's area taken from the head. Low, so the head stays round
// and full-sized while the tail streams out behind it.
constexpr float kTailFromHead = 0.25f;
// How far round the body a drag reaches: weight ((1 + cos) / 2)^this of the
// angle from the dragged spot (about half at 50 degrees, an eighth at 90)
constexpr float kDragBreadth = 3.0f;
}  // namespace

float FluidBody::nodeAngle(int i) { return 2 * kPi * float(i) / kNodes; }

void FluidBody::reset(float radius) {
  radius_ = radius;
  for (int i = 0; i < kNodes; i++) {
    float a = nodeAngle(i);
    cos_[i] = cosf(a);
    sin_[i] = sinf(a);
    cos2_[i] = cosf(2 * a);
    d_[i] = rest_[i];
    v_[i] = 0;
  }
  setRestShape(rest_);
}

void FluidBody::setRestShape(const float* offsets) {
  float sumR2 = 0;
  for (int i = 0; i < kNodes; i++) {
    rest_[i] = offsets ? offsets[i] : 0.0f;
    float r = radius_ + rest_[i];
    sumR2 += r * r;
  }
  restArea_ = 0.5f * sumR2 * (2 * kPi / kNodes);
}

void FluidBody::step(float dt, const Input& in) {
  if (dt <= 0) return;
  float ax = std::max(-p_.maxAccel, std::min(p_.maxAccel, in.ax));
  float ay = std::max(-p_.maxAccel, std::min(p_.maxAccel, in.ay));

  // Target outline for this frame: rest silhouette + breathing + tail (+ a
  // finger's drag). The tail tapers to a point over an angular half width
  // that narrows as it gets longer (tangent-like geometry).
  float target[kNodes];
  bool head[kNodes];
  const bool apart = in.tailApart && !in.keepArea;
  float tailW = 0;
  if (in.tail > 0.5f) tailW = std::max(0.35f, std::min(1.2f, acosf(radius_ / (radius_ + in.tail))));
  const float tc = cosf(in.tailAngle), ts = sinf(in.tailAngle);
  for (int i = 0; i < kNodes; i++) {
    target[i] = rest_[i] + in.breathe * cos2_[i];
    head[i] = true;
    if (tailW > 0) {
      float a = fabsf(atan2f(sin_[i] * tc - cos_[i] * ts, cos_[i] * tc + sin_[i] * ts));
      if (a < tailW) {
        target[i] += in.tail * powf(1.0f - a / tailW, kTailTaper);
        head[i] = false;
      }
    }
  }
  if (in.dragX * in.dragX + in.dragY * in.dragY > 0.25f) dragOutline(in, target);
  float sumR2 = 0;
  for (int i = 0; i < kNodes; i++) {
    float r = radius_ + target[i];
    if (head[i] || !apart) sumR2 += r * r;
  }
  float outlineArea = 0.5f * sumR2 * (2 * kPi / kNodes);
  float targetArea = in.keepArea ? restArea_
                     : apart     ? outlineArea
                                 : outlineArea - kTailFromHead * std::max(0.0f, outlineArea - restArea_);

  int n = std::min(kMaxSubsteps, std::max(1, int(ceilf(dt / kMaxSubstep))));
  float h = dt / float(n);
  for (int s = 0; s < n; s++) substep(h, in, target, targetArea, apart ? head : nullptr, ax, ay);
}

// Each target point moves with the drag, weighted by how near it is to the
// dragged spot; the moved outline is read back along the nodes' rays (the
// farthest crossing, should it fold over)
void FluidBody::dragOutline(const Input& in, float* target) const {
  float qx[kNodes], qy[kNodes];
  for (int i = 0; i < kNodes; i++) {
    float r = radius_ + target[i];
    float w = powf(0.5f * (1 + cosf(nodeAngle(i) - in.dragAngle)), kDragBreadth);
    qx[i] = r * cos_[i] + w * in.dragX;
    qy[i] = r * sin_[i] + w * in.dragY;
  }
  for (int j = 0; j < kNodes; j++) {
    float best = 0;
    for (int k = 0; k < kNodes; k++) {
      int k1 = (k + 1) % kNodes;
      float ex = qx[k1] - qx[k], ey = qy[k1] - qy[k];
      float den = cos_[j] * ey - sin_[j] * ex;
      if (fabsf(den) < 1e-6f) continue;
      float t = (qx[k] * ey - qy[k] * ex) / den;      // along the ray
      float s = (qx[k] * sin_[j] - qy[k] * cos_[j]) / den;  // along the segment
      if (t > 0 && s >= 0 && s < 1) best = std::max(best, t);
    }
    if (best > 0) target[j] = best - radius_;
  }
}

void FluidBody::substep(float h, const Input& in, const float* target, float targetArea, const bool* head,
                        float ax, float ay) {
  // Gravity minus the center's acceleration, as felt by the surface
  float gx = -ax, gy = in.gravity - ay;

  float acc[kNodes];
  for (int i = 0; i < kNodes; i++) {
    int prev = (i + kNodes - 1) % kNodes, next = (i + 1) % kNodes;
    float lap = d_[prev] + d_[next] - 2 * d_[i];
    float felt = (gx * cos_[i] + gy * sin_[i]) * p_.inertia;
    acc[i] = -p_.stiffness * (d_[i] - target[i]) - p_.damping * v_[i] + p_.tension * lap + felt;
  }

  const float lo = -radius_ * 0.6f, hi = radius_ * 3.2f;
  for (int i = 0; i < kNodes; i++) {
    v_[i] += acc[i] * h;
    d_[i] += v_[i] * h;
    if (d_[i] > hi || d_[i] < lo) {
      d_[i] = std::max(lo, std::min(hi, d_[i]));
      v_[i] = 0;
    }
  }

  // Keep the enclosed area: polygon area ~ 0.5 * sum(r^2) * dTheta
  float sumR2 = 0, sumR = 0;
  for (int i = 0; i < kNodes; i++) {
    if (head && !head[i]) continue;
    float r = radius_ + d_[i];
    sumR2 += r * r;
    sumR += r;
  }
  float dTheta = 2 * kPi / kNodes;
  float area = 0.5f * sumR2 * dTheta;
  float grow = (targetArea - area) / (sumR * dTheta) * p_.volumeKeep;
  for (int i = 0; i < kNodes; i++) {
    if (!head || head[i]) d_[i] += grow;
  }

  // Floor: nodes cannot cross it and lose their outward speed
  float below = in.groundY - in.cy;
  for (int i = 0; i < kNodes; i++) {
    if (sin_[i] <= 0.05f) continue;
    float maxR = below / sin_[i];
    if (radius_ + d_[i] > maxR) {
      d_[i] = maxR - radius_;
      if (v_[i] > 0) v_[i] = 0;
    }
  }
}

float FluidBody::radiusAt(float angle) const {
  float u = angle / (2 * kPi) * kNodes;
  u -= floorf(u / kNodes) * kNodes;  // wrap to [0, kNodes)
  int i = int(u) % kNodes;
  float f = u - floorf(u);
  return radius_ + d_[i] * (1 - f) + d_[(i + 1) % kNodes] * f;
}

float FluidBody::restRadiusAt(float angle) const {
  float u = angle / (2 * kPi) * kNodes;
  u -= floorf(u / kNodes) * kNodes;
  int i = int(u) % kNodes;
  float f = u - floorf(u);
  return radius_ + rest_[i] * (1 - f) + rest_[(i + 1) % kNodes] * f;
}

void FluidBody::squash(float angle, float speed) {
  float ac = cosf(angle), as = sinf(angle);
  for (int i = 0; i < kNodes; i++) {
    float c = cos_[i] * ac + sin_[i] * as;
    v_[i] += speed * (1.0f - 2.0f * c * c);  // sin^2 - cos^2
  }
}

void FluidBody::poke(float angle, float speed) {
  for (int i = 0; i < kNodes; i++) {
    float d = nodeAngle(i) - angle;
    d = atan2f(sinf(d), cosf(d));  // wrap to [-pi, pi]
    float w = expf(-(d * d) / (0.35f * 0.35f));
    v_[i] += speed * w;
  }
}

}  // namespace ui::blob
