#pragma once

namespace hal {

// Motion sensor reading in the screen's frame: x to the right, y down, z out
// of the screen toward the viewer. Acceleration in g as the sensor measures it
// (lying still face up: 0, 0, +1); rotation rate in degrees per second.
struct ImuSample {
  float ax = 0, ay = 0, az = 0;
  float gx = 0, gy = 0, gz = 0;
};

class Imu {
 public:
  virtual ~Imu() = default;
  // False when no reading could be taken
  virtual bool read(ImuSample& s) = 0;
};

}  // namespace hal
