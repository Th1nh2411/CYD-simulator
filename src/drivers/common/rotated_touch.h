#pragma once

#include <utility>

#include "hal/touch.h"

namespace drivers {

// Another touch driver's points turned to match a display shown turned a
// quarter clockwise quarterTurns times: each turn takes (x, y) to
// (height-1-y, x), width x height being the frame the driver reports in.
// mirror then flips the turned points left to right, as the display does.
class RotatedTouch : public hal::Touch {
 public:
  void begin(hal::Touch* inner, int width, int height, int quarterTurns, bool mirror = false) {
    inner_ = inner;
    width_ = width;
    height_ = height;
    turns_ = quarterTurns & 3;
    mirror_ = mirror;
  }

  bool read(hal::TouchPoint& p) override {
    if (!inner_->read(p)) return false;
    int x = p.x, y = p.y, h = height_, w = width_;
    for (int i = 0; i < turns_; i++) {
      const int turnedX = h - 1 - y;
      y = x;
      x = turnedX;
      std::swap(w, h);
    }
    if (mirror_) x = w - 1 - x;
    p.x = int16_t(x);
    p.y = int16_t(y);
    return true;
  }

 private:
  hal::Touch* inner_ = nullptr;
  int width_ = 0, height_ = 0, turns_ = 0;
  bool mirror_ = false;
};

}  // namespace drivers
