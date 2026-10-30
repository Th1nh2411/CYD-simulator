#pragma once

#include "hal/imu.h"

namespace drivers {

// hal::Imu for the browser simulator. The page sets the reading through
// sim_motion(ax, ay, az), already in the screen frame (keys tilt and shake it);
// lying flat face up until then. One instance per page.
class SimImu : public hal::Imu {
 public:
  bool read(hal::ImuSample& s) override;
};

}  // namespace drivers
