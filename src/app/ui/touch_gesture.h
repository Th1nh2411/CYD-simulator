#pragma once

#include <cstdint>

#include "hal/touch.h"

namespace ui {

// Turns touch samples (one per UI frame) into gestures. A finger lifted before
// the long-press time is a tap (so taps act on release); one held still that
// long is a long press, and no tap. A finger that moves further than the slop
// from where it landed drags until it lifts; after a long press that drag
// starts as HeldDragStart. Touch panels drop a sample now and then mid-press,
// so the finger counts as lifted only after `releaseMs` without contact.
class TouchGesture {
 public:
  enum class Kind : uint8_t { None, Tap, LongPress, DragStart, HeldDragStart, Drag, DragEnd };
  struct Event {
    Kind kind = Kind::None;
    int x = 0, y = 0;  // where the finger landed (tap, long press, drag start) or is (drag, drag end)
  };

  explicit TouchGesture(uint32_t releaseMs) : releaseMs_(releaseMs) {}
  // Slop and long-press time as calibrated for this device
  void tune(const hal::TouchCalibration& cal) {
    slop_ = cal.slopPx;
    longPressMs_ = uint32_t(cal.longPressMs);
  }
  // One sample: touching at (x, y) or not
  Event update(bool touching, int x, int y, uint32_t nowMs);
  bool pressed() const { return state_ != State::Up; }

 private:
  enum class State : uint8_t { Up, Down, Held, Dragging };  // Down: still a tap or a long press

  uint32_t releaseMs_;
  int slop_ = hal::TouchCalibration{}.slopPx;
  uint32_t longPressMs_ = uint32_t(hal::TouchCalibration{}.longPressMs);
  State state_ = State::Up;
  int x0_ = 0, y0_ = 0, x_ = 0, y_ = 0;
  uint32_t downMs_ = 0, contactMs_ = 0;
};

}  // namespace ui
