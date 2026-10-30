#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {

// Mono 16-bit samples out to an amplifier, at the rate given to the driver
class Speaker {
 public:
  virtual ~Speaker() = default;
  // Blocks until the samples are queued for output
  virtual void write(const int16_t* samples, size_t count) = 0;
  // Nothing to play for a while: let the output fall silent
  virtual void idle() {}
};

}  // namespace hal
