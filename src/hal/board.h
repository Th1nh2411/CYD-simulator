#pragma once

#include "hal/backlight.h"
#include "hal/display.h"
#include "hal/imu.h"
#include "hal/speaker.h"
#include "hal/touch.h"

namespace hal {

// Everything board specific, assembled by the board definition.
// Optional parts are nullptr / -1 when the board does not have them.
struct Board {
  const char* name = "";
  Display* display = nullptr;
  Touch* touch = nullptr;
  TouchCalibration touchCalibration;  // default until the device saves its own
  Backlight* backlight = nullptr;
  Imu* imu = nullptr;     // turned with the screen like touch
  Speaker* speaker = nullptr;  // at audio::kSampleRate
  int devButtonPin = -1;  // active-low button used for test actions
  // Screen lines per compositor strip (two strips of width x lines pixels, internal RAM). Fewer
  // lines free RAM and cost draw time.
  int stripLines = 24;
  // Battery voltage through a divider on an ADC pin: volts = pin volts * batteryScale.
  int batteryPin = -1;
  float batteryScale = 1.0f;
};

// Implemented by exactly one src/boards/<name>/board.cpp, chosen by the
// PlatformIO environment's build_src_filter. quarterTurns turns the screen
// (and touch) a quarter clockwise that many times from the board's own
// orientation: screen (x, y) -> (h-1-y, x) a turn. mirror then flips it left
// to right, (x, y) -> (w-1-x, y), for a screen seen through a prism.
// touchCalibration stays in the board's own frame (ui::rotateTouchCalibration
// and ui::mirrorTouchCalibration turn it).
Board& initBoard(int quarterTurns = 0, bool mirror = false);

}  // namespace hal
