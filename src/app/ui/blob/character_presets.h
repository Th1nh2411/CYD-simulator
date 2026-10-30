#pragma once

#include "app/ui/blob/blob_style.h"

// Ready-made characters. Each is a JSON patch over the defaults, in the same
// format as the settings page and saved configs, so presets can just as well
// come from the character converter or a server later.
namespace ui::blob {

int presetCount();
const char* presetName(int index);
// Resets c to defaults and applies the preset; false if the name is unknown
bool applyPreset(const char* name, CharacterConfig& c);

}  // namespace ui::blob
