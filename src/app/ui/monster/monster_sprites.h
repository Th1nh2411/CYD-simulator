#pragma once

#include <cstdint>

#include "app/gfx/canvas.h"

// Pixel art of the monsters and their world. Drawn in the PNG sheets of
// art/monster/ (palette slots and the starters: sprites.json); the build converts
// them into monster_art.h (tools/sprites/sprites.py). Slot order there is index order here.
namespace ui::monster {

// Palette slots of the monster art; index 0 is transparent. Accent is the
// species' feature: flame, leaves, shell.
enum MonColor : uint8_t {
  kOutline = 1, kBody, kShade, kBelly, kEye, kShine, kAccent, kAccent2, kMouth, kScarf, kScarfShade,
  kMonColors
};

// A starter's frames, in the order of the cells of sprites.json's character
// sheets. Field ones are 16x16: 0 standing, 1 mid-step, side ones face right;
// held ones struggle while carried, in turn. The battle one is 32x32, from behind.
enum Pose : uint8_t { kDown0, kDown1, kUp0, kUp1, kSide0, kSide1, kHeld0, kHeld1, kSleep, kBack, kPoseCount };

// A starter: one entry of sprites.json "characters", so adding one is art and data only
struct Species {
  const char* id;                   // "fire": its name in the settings
  const gfx::IndexedImage* frames;  // kPoseCount, by Pose
  const char* json;                 // the rest of its entry: label, preset, move, accent names, defaults
};
extern const Species* const kSpecies;
extern const int kSpeciesCount;

// How a starter's move looks in battle (its "move"); a flame also flickers
enum class Move : uint8_t { Flame, Leaf, Bubble };

extern const gfx::IndexedImage kFoeEnvelope;  // 24x24, the request in battle
constexpr int kFoeColors = 7;  // outline, paper shade, paper, shadow, seal, wing

enum Tile : uint8_t { kTileGrass, kTileGrass2, kTileFlowers, kTileTall, kTileCount };
extern const gfx::IndexedImage kTiles[kTileCount];  // 16x16
constexpr int kTileColors = 9;  // grass, dark, light, red, white, yellow, tall, tall dark

enum Emote : uint8_t { kEmoteBang, kEmoteHeart, kEmoteZzz, kEmoteAnger, kEmoteDrop, kEmoteDots, kEmoteCount };
extern const gfx::IndexedImage kEmotes[kEmoteCount];  // 12x12 speech bubbles
constexpr int kEmoteColors = 5;  // outline, bubble, red, blue

}  // namespace ui::monster
