#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "app/ui/blob/slime_character.h"
#include "app/ui/character.h"
#include "app/ui/monster/monster_character.h"

// The anime heroine's pictures take about 440 KB of flash: a board short of it
// builds without her (WITH_HEROINE=0, the CYD's default env); nothing then
// refers to her code or tables and the linker leaves them out.
#ifndef WITH_HEROINE
#define WITH_HEROINE 1
#endif
#if WITH_HEROINE
#include "app/ui/heroine/heroine_character.h"
#endif
// Capri (pictures and voice clips, about 480 KB of flash) the same way: WITH_CAPRI=0 leaves her out
#ifndef WITH_CAPRI
#define WITH_CAPRI 1
#endif
#if WITH_CAPRI
#include "app/ui/capri/capri_character.h"
#endif

namespace ui {

// Every character the firmware can show: the one place to register a new one.
// Index 0 is the default, and owns settings saved before characters could be
// switched.
//
// The settings document, as the settings page, NVS and serial use it:
//   {"character":"slime","slime":{...},"<name>":{...}}
// In older documents the default character's keys sit at the top level; those
// still merge into it. Settings calls are writer side (see Character).
class CharacterSet {
 public:
  int count() const { return kCount; }
  Character& operator[](int i) { return *list_[i]; }
  const Character& operator[](int i) const { return *list_[i]; }
  int find(const char* name) const;  // -1 if unknown

  // Merges a whole or partial document. Sets `active` if it picks a
  // character and `touched` bit i for each character whose settings it
  // merged into. False, with nothing changed, if it names an unknown character.
  bool merge(JsonObjectConst doc, int& active, uint32_t& touched);
  // The active character's part: {"character":<name>,"<name>":{...}}
  void toJson(int active, JsonObject out) const;
  // Expressions, then each character's name and option lists, for the settings page
  void describe(JsonObject out) const;

 private:
  static constexpr int kCount = 2 + WITH_HEROINE + WITH_CAPRI;
  blob::SlimeCharacter slime_;
  monster::MonsterCharacter monster_;
#if WITH_HEROINE
  heroine::HeroineCharacter heroine_;
#endif
#if WITH_CAPRI
  capri::CapriCharacter capri_;
#endif
  Character* const list_[kCount] = {
      &slime_,
      &monster_,
#if WITH_HEROINE
      &heroine_,
#endif
#if WITH_CAPRI
      &capri_,
#endif
  };
};

}  // namespace ui
