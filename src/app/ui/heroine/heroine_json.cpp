#include "app/ui/heroine/heroine_json.h"

#include <cstring>

#include "app/core/utf8.h"
#include "app/ui/json_fields.h"

namespace ui::heroine {

namespace {

using namespace ui::json;

const char* const kTimeNames[size_t(TimeOfDay::Count)] = {"auto", "morning", "day", "evening", "night"};

}  // namespace

const char* timeName(TimeOfDay t) { return kTimeNames[size_t(t) % size_t(TimeOfDay::Count)]; }

void toJson(const HeroineConfig& c, JsonObject out) {
  char hex[8];
  out["name"] = c.name;
  hexOf(c.accent, hex);
  out["accent"] = hex;
  out["time"] = timeName(c.time);
  out["textMs"] = c.textMs;
  out["sleepAfter"] = c.sleepAfter;
  out["saturation"] = round3(c.saturation);
  out["gamma"] = round3(c.gamma);
}

void mergeJson(JsonObjectConst o, HeroineConfig& c) {
  if (o["name"].is<const char*>()) {
    const char* name = o["name"].as<const char*>();
    if (*name) core::copyUtf8(c.name, sizeof(c.name), name);
  }
  readColor(o["accent"], c.accent);
  if (o["time"].is<const char*>()) {
    for (size_t i = 0; i < size_t(TimeOfDay::Count); i++) {
      if (strcmp(o["time"].as<const char*>(), kTimeNames[i]) == 0) c.time = TimeOfDay(i);
    }
  }
  if (o["textMs"].is<float>()) c.textMs = uint16_t(clampf(o["textMs"].as<float>(), 5, 150) + 0.5f);
  if (o["sleepAfter"].is<float>()) c.sleepAfter = uint16_t(clampf(o["sleepAfter"].as<float>(), 10, 900) + 0.5f);
  readFloat(o["saturation"], c.saturation, 0.5f, 2.5f);
  readFloat(o["gamma"], c.gamma, 0.6f, 1.8f);
}

}  // namespace ui::heroine
