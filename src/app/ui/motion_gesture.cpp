#include "app/ui/motion_gesture.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {
constexpr float kSmoothS = 0.08f;     // against jitter and the jolts of a shake
constexpr float kRestS = 4.0f;        // the rest pose follows a kept tilt this slowly
constexpr float kDeadZone = 0.03f;    // tilt under ~2 degrees reads zero
constexpr float kJoltG = 0.4f;        // change between two frames that counts as a jolt
constexpr uint32_t kShakeMs = 700;    // that many jolts (all of jolts_) within this
constexpr uint32_t kShakeQuietMs = 1200;
constexpr float kHoldTilt = 0.4f;     // ~24 degrees
constexpr float kHoldRelease = 0.15f;
constexpr uint32_t kHoldMs = 700;

float deadZone(float t) {
  const float m = std::fabs(t) - kDeadZone;
  return m <= 0 ? 0.0f : std::copysign(std::min(m, 1.0f), t);
}
}  // namespace

MotionGesture::Kind MotionGesture::update(const hal::ImuSample& s, uint32_t now) {
  // The sensor measures the push that holds it up: gravity pulls the other way
  const float gx = -s.ax, gy = -s.ay;
  if (!started_) {
    started_ = true;
    lastMs_ = now;
    gx_ = restX_ = gx;
    gy_ = restY_ = gy;
    lastAx_ = s.ax;
    lastAy_ = s.ay;
    lastAz_ = s.az;
    return Kind::None;
  }
  const float dt = std::min(0.1f, float(now - lastMs_) / 1000.0f);
  lastMs_ = now;
  const float k = 1 - std::exp(-dt / kSmoothS), kRest = 1 - std::exp(-dt / kRestS);
  gx_ += (gx - gx_) * k;
  gy_ += (gy - gy_) * k;
  restX_ += (gx_ - restX_) * kRest;
  restY_ += (gy_ - restY_) * kRest;
  tiltX_ = deadZone(gx_ - restX_);
  tiltY_ = deadZone(gy_ - restY_);

  // Shake: hard jolts, frame to frame, close together
  const float jx = s.ax - lastAx_, jy = s.ay - lastAy_, jz = s.az - lastAz_;
  lastAx_ = s.ax;
  lastAy_ = s.ay;
  lastAz_ = s.az;
  jolt_ = std::sqrt(jx * jx + jy * jy + jz * jz);
  if (jolt_ > kJoltG) {
    std::rotate(jolts_, jolts_ + 1, jolts_ + 4);
    jolts_[3] = now;
    if (jolts_[0] && now - jolts_[0] <= kShakeMs && int32_t(now - quietUntilMs_) >= 0) {
      quietUntilMs_ = now + kShakeQuietMs;
      std::fill(jolts_, jolts_ + 4, 0u);
      holdSide_ = 0;
      holdUsed_ = true;  // a shake swings the device: no choice from it
      return Kind::Shake;
    }
  }

  // Hold: tilted left or right long enough, once until it comes back level
  if (std::fabs(tiltX_) < kHoldRelease) holdUsed_ = false;
  const int side = tiltX_ < -kHoldTilt ? -1 : tiltX_ > kHoldTilt ? 1 : 0;
  if (side != holdSide_) {
    holdSide_ = side;
    holdSinceMs_ = now;
  } else if (side && !holdUsed_ && now - holdSinceMs_ >= kHoldMs) {
    holdUsed_ = true;
    return side < 0 ? Kind::HoldLeft : Kind::HoldRight;
  }
  return Kind::None;
}

}  // namespace ui
