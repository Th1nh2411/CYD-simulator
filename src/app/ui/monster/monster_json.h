#pragma once

#include <ArduinoJson.h>

#include "app/ui/monster/monster_style.h"

// JSON form of MonsterConfig. Colors are "#rrggbb"; values are clamped.
namespace ui::monster {

const char* speciesName(int s);      // its id: "fire", "grass", "water" ...
int findSpecies(const char* name);   // -1 if unknown
MonsterConfig starterConfig(int s);  // that starter's own name and colors (its "defaults")
Move speciesMove(int s);

void toJson(const MonsterConfig& c, JsonObject out);
// Missing keys keep their values. "species" switches the art only; the
// starter presets (ember, leaf, bubble) bring the starter's colors along.
void mergeJson(JsonObjectConst o, MonsterConfig& c);

// For the settings page: "species" (id, label, name, preset, accent names) and "presets"
void describe(JsonObject out);
bool applyPreset(const char* name, MonsterConfig& c);  // starter defaults + preset; false if unknown

}  // namespace ui::monster
