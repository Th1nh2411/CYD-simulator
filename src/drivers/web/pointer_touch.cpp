#include "drivers/web/pointer_touch.h"

#include <emscripten/emscripten.h>

#include <cstdint>

namespace {

// Written by page events between frames (single-threaded)
int16_t gX = 0, gY = 0;
bool gDown = false;
bool gUnread = false;  // press not seen by read() yet: a quick tap can begin and end between frames

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void sim_pointer(int x, int y, int down) {
  gX = int16_t(x);
  gY = int16_t(y);
  if (down && !gDown) gUnread = true;
  gDown = down != 0;
}

namespace drivers {

bool PointerTouch::read(hal::TouchPoint& p) {
  if (!gDown && !gUnread) return false;
  gUnread = false;
  p.x = gX;
  p.y = gY;
  p.pressure = 1000;
  return true;
}

}  // namespace drivers
