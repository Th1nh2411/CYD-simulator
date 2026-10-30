#pragma once

#include <cstdint>

#include "app/gfx/color.h"
#include "app/ui/capri/capri_lines.h"

namespace ui::capri {

// Settings of Capri: what the settings page edits and what gets saved under
// "capri". Plain data. Her look is the one picture (capri_art).
struct CapriConfig {
  char call[25] = "";  // what she calls the user, UTF-8; "" = 촘 / choom
  Lang lang = Lang::Ko;
  gfx::Color accent = gfx::rgb(58, 214, 235);  // panel border when a card has no tag of its own
  uint16_t textMs = 36;      // ms per character as her text decodes in
  uint16_t sleepAfter = 120;  // idle seconds before she dozes off
  uint16_t chatter = 5;      // minutes between her pop-ins (her brain's words when it answers), 0 = never
  uint8_t glitch = 50;       // how much she glitches on her own, 0 .. 100
  bool cutins = true;        // close-ups flash beside her on news
  bool sound = true;         // her blips and greeting clips (boards with a speaker)
  bool voice = true;         // she says her words out loud in English (her brain's /tts), instead of blips
  uint8_t volume = 60;       // 0 .. 100
  bool brain = true;         // a long press asks her brain (an AI on a PC, over MQTT) to talk
};

}  // namespace ui::capri
