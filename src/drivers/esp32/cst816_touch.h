#pragma once

#include <cstdint>

#include "hal/touch.h"

namespace drivers {

// Hynitron CST816 (S/D) capacitive touch on I2C (ESP-IDF driver), polled.
// One point in the controller's own coordinates, turned to the display's with
// swapXY / mirrorX / mirrorY (swap first).
class Cst816Touch : public hal::Touch {
 public:
  struct Config {
    int port = 0, sda = -1, scl = -1, rst = -1;
    uint32_t clockHz = 400000;
    int16_t width = 240, height = 240;  // display size
    bool swapXY = false, mirrorX = false, mirrorY = false;
  };

  bool begin(const Config& cfg);
  bool read(hal::TouchPoint& p) override;

 private:
  Config cfg_;
  bool ready_ = false;
};

}  // namespace drivers
