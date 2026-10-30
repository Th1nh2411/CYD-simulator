#include "app/ui/monster/monster_sprites.h"

#include "app/ui/monster/monster_art.h"

namespace ui::monster {

constexpr bool same(art::Role role, Pose pose) { return int(role) == int(pose); }
static_assert(same(art::kRoleCount, kPoseCount) && same(art::kRoleDown0, kDown0) && same(art::kRoleDown1, kDown1) &&
                  same(art::kRoleUp0, kUp0) && same(art::kRoleUp1, kUp1) && same(art::kRoleSide0, kSide0) &&
                  same(art::kRoleSide1, kSide1) && same(art::kRoleHeld0, kHeld0) && same(art::kRoleHeld1, kHeld1) &&
                  same(art::kRoleSleep, kSleep) && same(art::kRoleBack, kBack),
              "art/monster/sprites.json: the character sheets' cells are not in Pose order");

const Species* const kSpecies = art::kCharacters;
const int kSpeciesCount = art::kCharacterCount;

const gfx::IndexedImage kFoeEnvelope = art::kEnvelope.image();
const gfx::IndexedImage kTiles[kTileCount] = {art::kTileGrass.image(), art::kTileGrass2.image(),
                                              art::kTileFlowers.image(), art::kTileTall.image()};
const gfx::IndexedImage kEmotes[kEmoteCount] = {art::kEmoteBang.image(), art::kEmoteHeart.image(),
                                                art::kEmoteZzz.image(), art::kEmoteAnger.image(),
                                                art::kEmoteDrop.image(), art::kEmoteDots.image()};

}  // namespace ui::monster
