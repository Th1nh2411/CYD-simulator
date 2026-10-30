#pragma once

#include <cstdint>

#include "hal/touch.h"

namespace drivers {

// Capacitive touch built into the AXS15231B, on I2C (ESP-IDF driver), polled.
// The controller reports panel (portrait) coordinates; they come out turned to
// Axs15231bQspiDisplay's landscape (quarterTurns 0). Other turns: RotatedTouch.
class Axs15231bTouch : public hal::Touch {
 public:
  struct Config {
    int port = 0, sda = -1, scl = -1;
    uint32_t clockHz = 400000;
    int16_t panelWidth = 320, panelHeight = 480;
  };

  bool begin(const Config& cfg);
  bool read(hal::TouchPoint& p) override;

 private:
  Config cfg_;
  bool ready_ = false;
};

}  // namespace drivers
