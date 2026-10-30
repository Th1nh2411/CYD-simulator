#pragma once

#include <cstdint>

#include "hal/touch.h"

namespace drivers {

// XPT2046 resistive touch controller over bit-banged SPI (any Arduino core).
// Bit-banging keeps both hardware SPI hosts free for display and SD card.
//
// Resistive panels report noisy coordinates at light pressure (start and end
// of a tap), so a sample is accepted only with enough pressure, after the
// first samples of a press, and when it does not jump from the previous one
// (or carries on the finger's last step: a fast drag moves further than
// maxJump between samples).
class Xpt2046Bitbang : public hal::Touch {
 public:
  struct Config {
    int clk = -1, mosi = -1, miso = -1, cs = -1;
    int irq = -1;  // PENIRQ; skips SPI entirely while untouched
    int16_t width = 0, height = 0;  // screen size in display orientation
    bool swapXY = false, invertX = false, invertY = false;
    int16_t rawXMin = 200, rawXMax = 3700, rawYMin = 240, rawYMax = 3800;
    uint16_t zMin = 600;
    uint8_t settleSamples = 2;
    uint16_t maxJump = 150;  // raw units from the last sample (or its step carried on)
  };

  void begin(const Config& cfg);
  bool read(hal::TouchPoint& p) override;

 private:
  uint16_t transfer(uint8_t cmd);

  Config cfg_;
  uint8_t pressedSamples_ = 0;
  int prevA_ = 0, prevB_ = 0;
  int stepA_ = 0, stepB_ = 0;  // the last sample's move
};

}  // namespace drivers
