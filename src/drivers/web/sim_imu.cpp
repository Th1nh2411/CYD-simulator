#include "drivers/web/sim_imu.h"

#include <emscripten/emscripten.h>

namespace {

// Written by page events between frames (single-threaded)
float gAx = 0, gAy = 0, gAz = 1;

}  // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void sim_motion(float ax, float ay, float az) {
  gAx = ax;
  gAy = ay;
  gAz = az;
}

namespace drivers {

bool SimImu::read(hal::ImuSample& s) {
  s = hal::ImuSample{};
  s.ax = gAx;
  s.ay = gAy;
  s.az = gAz;
  return true;
}

}  // namespace drivers
