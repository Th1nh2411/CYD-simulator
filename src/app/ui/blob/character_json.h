#pragma once

#include <ArduinoJson.h>

#include <cstddef>

#include "app/ui/blob/blob_style.h"

// JSON form of CharacterConfig: what the settings page edits, what gets saved,
// and what the character converter will emit. Colors are "#rrggbb".
namespace ui::blob {

// Writes the whole config into out
void toJson(const CharacterConfig& c, JsonObject out);

// Merges the keys present in o into c; missing keys keep their values.
// "eyeSet" (a preset name) loads that preset before any "eyes" overrides.
// Values are clamped to sane ranges.
void mergeJson(JsonObjectConst o, CharacterConfig& c);
// Same from text; false on a parse error or if it is not an object
bool mergeJson(const char* json, size_t len, CharacterConfig& c);

}  // namespace ui::blob
