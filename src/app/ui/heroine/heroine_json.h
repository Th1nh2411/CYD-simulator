#pragma once

#include <ArduinoJson.h>

#include "app/ui/heroine/heroine_style.h"

// Heroine settings <-> JSON: {"name":..,"accent":"#rrggbb","time":"auto",..}.
// Partial documents merge; values are clamped.
namespace ui::heroine {

void toJson(const HeroineConfig& c, JsonObject out);
void mergeJson(JsonObjectConst o, HeroineConfig& c);
const char* timeName(TimeOfDay t);

}  // namespace ui::heroine
