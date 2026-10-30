#include "app/ui/monster/monster_json.h"

#include <cstring>

#include "app/core/utf8.h"
#include "app/ui/json_fields.h"

namespace ui::monster {

namespace {

using namespace ui::json;

struct ColorField {
  const char* key;
  gfx::Color MonsterConfig::*ptr;
};
const ColorField kColors[] = {
    {"body", &MonsterConfig::body},       {"belly", &MonsterConfig::belly}, {"accent", &MonsterConfig::accent},
    {"accent2", &MonsterConfig::accent2}, {"scarf", &MonsterConfig::scarf}, {"grass", &MonsterConfig::grass},
};

struct IntField {
  const char* key;
  int lo, hi;
  int (*get)(const MonsterConfig&);
  void (*set)(MonsterConfig&, int);
};
const IntField kInts[] = {
    {"scale", 2, 4, [](const MonsterConfig& c) { return int(c.scale); }, [](MonsterConfig& c, int v) { c.scale = uint8_t(v); }},
    {"textMs", 5, 150, [](const MonsterConfig& c) { return int(c.textMs); }, [](MonsterConfig& c, int v) { c.textMs = uint16_t(v); }},
    {"sleepAfter", 5, 600, [](const MonsterConfig& c) { return int(c.sleepAfter); },
     [](MonsterConfig& c, int v) { c.sleepAfter = uint16_t(v); }},
    {"level", 1, 100, [](const MonsterConfig& c) { return int(c.level); }, [](MonsterConfig& c, int v) { c.level = uint8_t(v); }},
};

struct Preset {
  const char* name;
  const char* json;
};
// After the starters' own presets (sprites.json "preset", the settings page's starter
// choice): variants of a starter, the first one unless they name another
const Preset kVariants[] = {
    {"frost", R"({"name":"서리","body":"#78b8f0","belly":"#e8f4ff","accent":"#58c8f8","accent2":"#ffffff",
      "scarf":"#f05878","grass":"#9cd0b8"})"},
    {"shiny", R"({"name":"반짝이","body":"#f8c030","belly":"#fff0b8","accent":"#f86830","accent2":"#ffe8a0",
      "scarf":"#48b848"})"},
    {"night", R"({"name":"밤불","body":"#c05048","belly":"#f0d0a0","accent":"#a050f8","accent2":"#f0c8ff",
      "scarf":"#f0c838","grass":"#3c7048"})"},
};

// A starter's sprites.json entry (the generator wrote it, so it parses)
JsonDocument speciesEntry(int s) {
  JsonDocument doc;
  deserializeJson(doc, kSpecies[s % kSpeciesCount].json);
  return doc;
}

}  // namespace

const char* speciesName(int s) { return kSpecies[s % kSpeciesCount].id; }

int findSpecies(const char* name) {
  for (int i = 0; i < kSpeciesCount; i++) {
    if (strcmp(kSpecies[i].id, name) == 0) return i;
  }
  return -1;
}

MonsterConfig starterConfig(int s) {
  MonsterConfig c;
  c.species = uint8_t(s % kSpeciesCount);
  mergeJson(speciesEntry(s)["defaults"].as<JsonObjectConst>(), c);
  return c;
}

Move speciesMove(int s) {
  JsonDocument e = speciesEntry(s);
  const char* move = e["move"] | "flame";
  return strcmp(move, "leaf") == 0 ? Move::Leaf : strcmp(move, "bubble") == 0 ? Move::Bubble : Move::Flame;
}

void toJson(const MonsterConfig& c, JsonObject out) {
  char hex[8];
  out["species"] = speciesName(c.species);
  out["name"] = c.name;
  for (const ColorField& f : kColors) {
    hexOf(c.*f.ptr, hex);
    out[f.key] = hex;
  }
  for (const IntField& f : kInts) out[f.key] = f.get(c);
  out["walkSpeed"] = round3(c.walkSpeed);
}

void mergeJson(JsonObjectConst o, MonsterConfig& c) {
  if (o["species"].is<const char*>()) {
    int s = findSpecies(o["species"].as<const char*>());
    if (s >= 0) c.species = uint8_t(s);
  }
  if (o["name"].is<const char*>()) {
    const char* name = o["name"].as<const char*>();
    if (*name) core::copyUtf8(c.name, sizeof(c.name), name);
  }
  for (const ColorField& f : kColors) readColor(o[f.key], c.*f.ptr);
  for (const IntField& f : kInts) {
    if (o[f.key].is<float>()) f.set(c, int(clampf(o[f.key].as<float>(), float(f.lo), float(f.hi)) + 0.5f));
  }
  readFloat(o["walkSpeed"], c.walkSpeed, 2, 60);
}

void describe(JsonObject out) {
  JsonArray species = out["species"].to<JsonArray>();
  JsonArray presets = out["presets"].to<JsonArray>();
  for (int s = 0; s < kSpeciesCount; s++) {
    JsonDocument e = speciesEntry(s);
    JsonObject o = species.add<JsonObject>();
    o["id"] = kSpecies[s].id;
    o["label"] = e["label"];
    o["name"] = e["defaults"]["name"];
    o["preset"] = e["preset"];
    o["accentNames"] = e["accentNames"];
    presets.add(e["preset"]);
  }
  for (const Preset& p : kVariants) presets.add(p.name);
}

bool applyPreset(const char* name, MonsterConfig& c) {
  for (int s = 0; s < kSpeciesCount; s++) {
    if (strcmp(speciesEntry(s)["preset"] | "", name) == 0) {
      c = starterConfig(s);
      return true;
    }
  }
  for (const Preset& p : kVariants) {
    if (strcmp(p.name, name) != 0) continue;
    JsonDocument doc;
    if (deserializeJson(doc, p.json)) return false;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    int s = o["species"].is<const char*>() ? findSpecies(o["species"].as<const char*>()) : 0;
    c = starterConfig(s < 0 ? 0 : s);
    mergeJson(o, c);
    return true;
  }
  return false;
}

}  // namespace ui::monster
