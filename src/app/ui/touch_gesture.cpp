#include "app/ui/touch_gesture.h"

#include <cstdlib>

namespace ui {

TouchGesture::Event TouchGesture::update(bool touching, int x, int y, uint32_t nowMs) {
  Event e;
  if (!touching) {
    if (state_ != State::Up && nowMs - contactMs_ >= releaseMs_) {
      if (state_ == State::Down) e = {Kind::Tap, x0_, y0_};
      if (state_ == State::Dragging) e = {Kind::DragEnd, x_, y_};
      state_ = State::Up;
    }
    return e;
  }
  contactMs_ = nowMs;
  x_ = x;
  y_ = y;
  if (state_ == State::Up) {
    state_ = State::Down;
    x0_ = x;
    y0_ = y;
    downMs_ = nowMs;
  } else if (state_ == State::Dragging) {
    e = {Kind::Drag, x, y};
  } else if (std::abs(x - x0_) > slop_ || std::abs(y - y0_) > slop_) {
    e = {state_ == State::Held ? Kind::HeldDragStart : Kind::DragStart, x0_, y0_};
    state_ = State::Dragging;
  } else if (state_ == State::Down && nowMs - downMs_ >= longPressMs_) {
    state_ = State::Held;
    e = {Kind::LongPress, x0_, y0_};
  }
  return e;
}

}  // namespace ui
