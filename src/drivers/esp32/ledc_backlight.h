#pragma once

#include <cstdint>

#include "hal/backlight.h"

namespace drivers {

// PWM backlight on an ESP32 LEDC channel. activeLow: the pin low lights it
class LedcBacklight : public hal::Backlight {
 public:
  void begin(int pin, bool activeLow = false, uint8_t channel = 0, uint32_t freqHz = 5000);
  void set(uint8_t level) override;
  void holdOff() override;

 private:
  int pin_ = -1;
  uint8_t channel_ = 0;
  bool activeLow_ = false;
};

}  // namespace drivers
