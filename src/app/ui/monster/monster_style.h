#pragma once

#include <cstdint>

#include "app/gfx/color.h"
#include "app/ui/monster/monster_sprites.h"

namespace ui::monster {

// Settings of the pixel monster: what the settings page edits and what gets
// saved under "monster". Plain data; the defaults are the first starter, fire
// (starterConfig gives the others).
struct MonsterConfig {
  uint8_t species = 0;        // index in kSpecies
  char name[25] = "엠버";  // UTF-8, shown in battles and messages
  gfx::Color body = gfx::rgb(244, 106, 60);
  gfx::Color belly = gfx::rgb(248, 224, 160);
  gfx::Color accent = gfx::rgb(248, 72, 24);    // flame / leaves / shell
  gfx::Color accent2 = gfx::rgb(255, 224, 72);  // its light part
  gfx::Color scarf = gfx::rgb(60, 140, 240);
  gfx::Color grass = gfx::rgb(112, 192, 80);
  uint8_t scale = 3;          // screen px per art pixel on the field
  float walkSpeed = 14.0f;    // art px per second
  uint16_t textMs = 35;       // typing speed, ms per character
  uint16_t sleepAfter = 30;   // idle seconds before it curls up and sleeps
  uint8_t level = 5;          // shown on the battle name plate
};

}  // namespace ui::monster
