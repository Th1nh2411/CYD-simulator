#pragma once

#include "hal/touch.h"

namespace drivers {

// hal::Touch for the browser simulator. The page forwards mouse / finger
// events on the screen canvas through sim_pointer(x, y, down), already in
// screen coordinates. One instance per page.
class PointerTouch : public hal::Touch {
 public:
  bool read(hal::TouchPoint& p) override;
};

}  // namespace drivers
