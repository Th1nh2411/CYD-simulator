#pragma once

#include <cstdint>

namespace hal {

struct TouchPoint {
  int16_t x = 0, y = 0;  // screen coordinates, same orientation as the display
  uint16_t pressure = 0;
};

// Per-axis linear correction of what the touch driver reports:
// screen x = ax * x + bx, screen y = ay * y + by. Panels of one model differ a
// little, so boards ship a measured default and the device can save its own.
// Gestures are calibrated the same way: a finger moving further than slopPx
// (driver px) drags, so it must stay above the panel's jitter under a still
// finger; one held longPressMs is a long press, so it must outlast the taps.
struct TouchCalibration {
  float ax = 1, bx = 0, ay = 1, by = 0;
  int slopPx = 16;
  int longPressMs = 500;  // taps measured 60..150 ms
};

class Touch {
 public:
  virtual ~Touch() = default;
  // Returns true and fills p while a stable press is detected
  virtual bool read(TouchPoint& p) = 0;
};

}  // namespace hal
