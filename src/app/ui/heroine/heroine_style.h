#pragma once

#include <cstdint>

#include "app/gfx/color.h"

namespace ui::heroine {

// Time of day of her room: follow the clock, or keep one
enum class TimeOfDay : uint8_t { Auto, Morning, Day, Evening, Night, Count };

// Settings of the visual-novel heroine: what the settings page edits and what
// gets saved under "heroine". Plain data. Her look is a painted portrait.
struct HeroineConfig {
  char name[25] = "하루";                        // UTF-8, on the dialog's name plate
  gfx::Color accent = gfx::rgb(255, 122, 162);  // name plate, choices, "next" arrow
  TimeOfDay time = TimeOfDay::Auto;
  uint16_t textMs = 40;      // typing speed, ms per character
  uint16_t sleepAfter = 90;  // idle seconds before she dozes off
  // Her picture's colors for the panel: a TN panel washes out light tones, so
  // pale skin reads gray unless saturated a little more than on a monitor
  float saturation = 1.3f;
  float gamma = 1.1f;  // above 1 darkens the light tones
};

}  // namespace ui::heroine
