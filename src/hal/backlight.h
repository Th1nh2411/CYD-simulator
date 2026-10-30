#pragma once

#include <cstdint>

namespace hal {

class Backlight {
 public:
  virtual ~Backlight() = default;
  virtual void set(uint8_t level) = 0;  // 0 = off, 255 = full
  // Off, and kept off through deep sleep, where an undriven pin may float on
  virtual void holdOff() { set(0); }
};

}  // namespace hal
