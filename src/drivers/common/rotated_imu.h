#pragma once

#include "hal/imu.h"

namespace drivers {

// Another IMU's readings turned to match a display shown turned a quarter
// clockwise quarterTurns times, then mirrored, as RotatedTouch does for
// points: a turn takes the vector (x, y) to (-y, x); a mirror flips x. The
// rotation rate is an axial vector: a mirror flips its y and z instead.
class RotatedImu : public hal::Imu {
 public:
  void begin(hal::Imu* inner, int quarterTurns, bool mirror) {
    inner_ = inner;
    turns_ = quarterTurns & 3;
    mirror_ = mirror;
  }

  bool read(hal::ImuSample& s) override {
    if (!inner_->read(s)) return false;
    for (int i = 0; i < turns_; i++) {
      turn(s.ax, s.ay);
      turn(s.gx, s.gy);
    }
    if (mirror_) {
      s.ax = -s.ax;
      s.gy = -s.gy;
      s.gz = -s.gz;
    }
    return true;
  }

 private:
  static void turn(float& x, float& y) {
    const float turnedX = -y;
    y = x;
    x = turnedX;
  }

  hal::Imu* inner_ = nullptr;
  int turns_ = 0;
  bool mirror_ = false;
};

}  // namespace drivers
