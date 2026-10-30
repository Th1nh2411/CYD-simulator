#pragma once

#include <ArduinoJson.h>

#include "app/ui/capri/capri_style.h"

namespace ui::capri {

// Settings codec, keys as in CapriConfig:
//   {"call","lang":"ko"|"en","accent":"#rrggbb","textMs","sleepAfter","chatter","glitch",
//    "cutins","sound","voice","volume","brain"}
void toJson(const CapriConfig& c, JsonObject out);
void mergeJson(JsonObjectConst o, CapriConfig& c);  // keys present only, values clamped
const char* langName(Lang l);

}  // namespace ui::capri
