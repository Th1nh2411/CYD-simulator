#include "app/ui/blob/character_presets.h"

#include <cstring>

#include "app/ui/blob/character_json.h"

namespace ui::blob {

namespace {

struct Preset {
  const char* name;
  const char* json;
};

const Preset kPresets[] = {
    {"aqua", R"({})"},  // the default droplet
    {"sprout", R"({"bodyShape":"mochi","aspect":1.05,"body":"#9be38a","eye":"#1e2a1a","cheek":"#ff8fa8",
      "accessories":{"head":{"type":"sprout","color":"#5fd16a","color2":"#3b8f46"}}})"},
    {"kitty", R"({"bodyShape":"cat","aspect":1.08,"body":"#ffb86b","eye":"#2a1c10","cheek":"#ff7a9a",
      "cheekAlpha":16,"accessories":{"head":{"type":"none"},"right":{"type":"heart","color":"#ff5a78","color2":"#c8cde1"}}})"},
    {"robo", R"({"bodyShape":"squircle","aspect":1.1,"body":"#9fb4c8","opacity":32,"highlight":10,
      "eye":"#5ef0ff","cheekAlpha":0,"eyeSet":"robot",
      "accessories":{"head":{"type":"antenna","color":"#ff5a5a","color2":"#5a6078"}},
      "motion":{"thud":0.9,"drop":0.8,"splash":0}})"},
    {"angel", R"({"bodyShape":"egg","body":"#f4f1ff","opacity":26,"eye":"#3a3560","cheek":"#ffb0c8",
      "eyeSet":"dot","brows":{"style":"arch","color":"#6a6496"},
      "accessories":{"head":{"type":"none"},"back":{"type":"wings","color":"#ffffff","color2":"#dfe6ff"}}})"},
    {"imp", R"({"bodyShape":"drop","body":"#b05cff","eye":"#1a0b26","cheek":"#ff5c8a",
      "brows":{"style":"thick","color":"#2a0d3a"},"eyes":{"normal":{"browAngle":0.3}},
      "accessories":{"head":{"type":"horns","color":"#ffe08a","color2":"#c8a050"},
      "back":{"type":"spikes","color":"#7a2bd6","color2":"#c0a0ff"}}})"},
    {"royal", R"({"bodyShape":"bean","body":"#ff8fb1","eye":"#2b1020","eyeSet":"pill",
      "accessories":{"head":{"type":"crown","color":"#ffd24a","color2":"#5ad1ff"},
      "left":{"type":"flag","color":"#5ad1ff","color2":"#c8cde1"}}})"},
    {"party", R"({"body":"#7ee0d0","eye":"#14302c","brows":{"style":"line","color":"#14302c"},
      "accessories":{"head":{"type":"bow","color":"#ff6fa0","color2":"#ffd1e0"},
      "left":{"type":"balloon","color":"#ff7ab0","color2":"#c8cde1"},
      "right":{"type":"balloon","color":"#ffd24a","color2":"#c8cde1"}}})"},
    {"mochi", R"({"bodyShape":"mochi","aspect":1.15,"body":"#fff4f0","opacity":30,"eye":"#2a2020",
      "cheek":"#ff9fb0","cheekAlpha":18,"eyeSet":"dot","accessories":{"head":{"type":"none"}},
      "physics":{"stiffness":120,"damping":4},"motion":{"drop":2.2}})"},
};

}  // namespace

int presetCount() { return int(sizeof(kPresets) / sizeof(kPresets[0])); }

const char* presetName(int index) {
  return (index >= 0 && index < presetCount()) ? kPresets[index].name : "";
}

bool applyPreset(const char* name, CharacterConfig& c) {
  for (const Preset& p : kPresets) {
    if (strcmp(p.name, name) != 0) continue;
    c = CharacterConfig();
    return mergeJson(p.json, strlen(p.json), c);
  }
  return false;
}

}  // namespace ui::blob
