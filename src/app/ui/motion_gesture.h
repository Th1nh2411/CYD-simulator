#pragma once

#include <cstdint>

#include "hal/imu.h"

namespace ui {

// Turns motion sensor readings (screen frame) into a tilt and gestures, once
// per frame, the way TouchGesture does for touch. The tilt is measured from
// the pose the device has kept for the last few seconds, so it reads zero
// however the device stands; tilted away from that it shows, then fades as
// the new pose becomes the rest. Shake: several hard jolts within a moment.
// Hold: tilted left or right past a threshold for a moment.
class MotionGesture {
 public:
  enum class Kind : uint8_t { None, Shake, HoldLeft, HoldRight };

  Kind update(const hal::ImuSample& s, uint32_t nowMs);
  // Where gravity pulls along the screen, relative to the resting pose: x
  // right, y down, about -1..1 (sine of the tilt); exactly 0 near the rest
  float tiltX() const { return tiltX_; }
  float tiltY() const { return tiltY_; }
  // Change of the measured acceleration since the previous reading (g): what a shake is counted in
  float jolt() const { return jolt_; }

 private:
  float jolt_ = 0;
  bool started_ = false;
  uint32_t lastMs_ = 0;
  float gx_ = 0, gy_ = 0;        // gravity along the screen, smoothed
  float restX_ = 0, restY_ = 0;  // the pose kept lately
  float tiltX_ = 0, tiltY_ = 0;
  float lastAx_ = 0, lastAy_ = 0, lastAz_ = 0;
  uint32_t jolts_[4] = {};  // times of the last hard jolts, newest last
  uint32_t quietUntilMs_ = 0;  // no new shake right after one
  int holdSide_ = 0;           // -1 left, 1 right while tilted past the threshold
  uint32_t holdSinceMs_ = 0;
  bool holdUsed_ = false;  // fired; waits for the device to come back level
};

}  // namespace ui
