#pragma once

#include "drivers/panels/panel_desc.h"

namespace drivers::panels {

// ST7789 runs on its power-on defaults; the generic bring-up is enough.
// Most ST7789 IPS panels need invert = true in the board config.
inline constexpr PanelDesc kSt7789{"ST7789", nullptr, 0};

}  // namespace drivers::panels
