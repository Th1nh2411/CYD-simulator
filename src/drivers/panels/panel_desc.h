#pragma once

#include <cstdint>

namespace drivers {

// Vendor-specific init commands of a MIPI-DCS style panel (ILI9341, ST7789,
// ST7796, GC9A01, ...). The generic part of bring-up (reset, pixel format,
// orientation, inversion, sleep out, display on) is done by the driver, so a
// new panel only needs a table like this.
struct PanelInitCmd {
  uint8_t cmd;
  uint8_t len;
  uint8_t data[15];
  uint16_t delayMs;
};

struct PanelDesc {
  const char* name;
  const PanelInitCmd* cmds;
  uint8_t count;
};

}  // namespace drivers
