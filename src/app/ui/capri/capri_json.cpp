#include "app/ui/capri/capri_json.h"

#include <cstring>

#include "app/core/utf8.h"
#include "app/ui/json_fields.h"

namespace ui::capri {

namespace {

using namespace ui::json;

const char* const kLangNames[size_t(Lang::Count)] = {"ko", "en"};

void readInt(JsonVariantConst v, uint16_t& dst, float lo, float hi) {
  if (v.is<float>()) dst = uint16_t(clampf(v.as<float>(), lo, hi) + 0.5f);
}

void readByte(JsonVariantConst v, uint8_t& dst, float lo, float hi) {
  if (v.is<float>()) dst = uint8_t(clampf(v.as<float>(), lo, hi) + 0.5f);
}

void readBool(JsonVariantConst v, bool& dst) {
  if (v.is<bool>()) dst = v.as<bool>();
}

}  // namespace

const char* langName(Lang l) { return kLangNames[size_t(l) % size_t(Lang::Count)]; }

void toJson(const CapriConfig& c, JsonObject out) {
  char hex[8];
  out["call"] = c.call;
  out["lang"] = langName(c.lang);
  hexOf(c.accent, hex);
  out["accent"] = hex;
  out["textMs"] = c.textMs;
  out["sleepAfter"] = c.sleepAfter;
  out["chatter"] = c.chatter;
  out["glitch"] = c.glitch;
  out["cutins"] = c.cutins;
  out["sound"] = c.sound;
  out["voice"] = c.voice;
  out["volume"] = c.volume;
  out["brain"] = c.brain;
}

void mergeJson(JsonObjectConst o, CapriConfig& c) {
  if (o["call"].is<const char*>()) core::copyUtf8(c.call, sizeof(c.call), o["call"].as<const char*>());
  if (o["lang"].is<const char*>()) {
    for (size_t i = 0; i < size_t(Lang::Count); i++) {
      if (strcmp(o["lang"].as<const char*>(), kLangNames[i]) == 0) c.lang = Lang(i);
    }
  }
  readColor(o["accent"], c.accent);
  readInt(o["textMs"], c.textMs, 5, 150);
  readInt(o["sleepAfter"], c.sleepAfter, 10, 900);
  readInt(o["chatter"], c.chatter, 0, 240);
  readByte(o["glitch"], c.glitch, 0, 100);
  readBool(o["cutins"], c.cutins);
  readBool(o["sound"], c.sound);
  readBool(o["voice"], c.voice);
  readByte(o["volume"], c.volume, 0, 100);
  readBool(o["brain"], c.brain);
}

}  // namespace ui::capri
