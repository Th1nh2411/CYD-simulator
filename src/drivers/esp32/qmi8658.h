#pragma once

#include <cstdint>

#include "hal/imu.h"

namespace drivers {

// QST QMI8658 6-axis IMU on I2C (ESP-IDF driver), polled: accelerometer +-4 g
// and gyroscope +-512 dps at 250 Hz, low-pass filtered at ~33 Hz. The chip's axes are
// mapped to the board's own screen frame (hal::ImuSample) with swapXY and
// the inverts, swap first.
class Qmi8658 : public hal::Imu {
 public:
  struct Config {
    int port = 0, sda = -1, scl = -1;
    uint32_t clockHz = 400000;
    bool swapXY = false, invertX = false, invertY = false, invertZ = false;
  };

  bool begin(const Config& cfg);  // false when no QMI8658 answers (at 0x6B or 0x6A)
  bool read(hal::ImuSample& s) override;

 private:
  bool write(uint8_t reg, uint8_t value);
  bool readBytes(uint8_t reg, uint8_t* out, int n);
  void toScreen(float x, float y, float z, float& sx, float& sy, float& sz) const;

  Config cfg_;
  uint8_t address_ = 0;
};

}  // namespace drivers
