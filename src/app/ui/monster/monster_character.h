#pragma once

#include "app/ui/character.h"
#include "app/ui/monster/monster_layers.h"
#include "app/ui/monster/monster_sprites.h"
#include "app/ui/monster/monster_style.h"

namespace ui::monster {

// A pixel-art monster (original art, in the spirit of the handheld monster
// games): one of three starters, fire, grass or water. It wanders a grass
// field. A notification stops it ("!") and opens a dialog box at the bottom
// that types the message out. A prompt starts a battle against the request:
// the options are its moves, and picking one plays the move before it
// returns to the field. Stages cards itself (Character::presentsCards).
class MonsterCharacter : public CharacterWith<MonsterConfig> {
 public:
  const char* name() const override { return "monster"; }

  void resetSettings() override;
  void mergeSettings(JsonObjectConst patch) override;
  bool applyPreset(const char* name) override;
  void settingsToJson(JsonObject out) const override;
  void describe(JsonObject out) const override;

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) override;
  void setActive(bool active) override;
  bool syncSettings() override;
  gfx::Color background() const override { return settings_.grass; }
  void setAnchor(float, float) override {}  // it goes where it likes
  void setAttention(bool, float, float) override {}
  void previewExpression(Expression e, uint32_t untilMs) override;
  void update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs) override;
  bool hitTest(int x, int y) const override;
  void poke(float x, float y) override;
  bool grab(float x, float y, bool held) override;  // any drag picks it up
  void drag(float x, float y) override;
  void release() override;
  bool busy(uint32_t nowMs) const override;

  bool presentsCards() const override { return true; }
  void present(const engine::CardState& card, uint32_t nowMs) override;
  bool handleTap(int x, int y, engine::Event& out) override;

 private:
  enum class Dir : uint8_t { Down, Up, Left, Right };
  enum class Stage : uint8_t { Field, Encounter, Battle, Attack, Leave };
  enum class BoxState : uint8_t { Closed, Opening, Open, Closing };
  enum class Carry : uint8_t { None, Held, Dropping };

  void applySettings();
  void onCard(uint32_t now);
  void onCardGone(uint32_t now);
  void toField(uint32_t now);
  void toBattleScene();
  void updateField(float dt, uint32_t now);
  void updateWander(float dt, uint32_t now);
  void updateDialog(uint32_t now);
  void updateEncounter(uint32_t now);
  void updateBattle(uint32_t now);
  void updateLeave(uint32_t now);
  void showFieldMonster(uint32_t now);
  void followFinger(uint32_t now);
  float carryHeight(uint32_t now) const;  // 0 on the ground .. 1 held up
  int airPx(uint32_t now) const;          // carried: px over its shadow
  void showBattleSprites(uint32_t now);
  void spawnMove();
  const gfx::Color* palette(uint32_t now) const;
  const gfx::IndexedImage* frames() const;  // the species' frames, by Pose
  void say(const char* title, const char* body, uint32_t now);
  int typed(uint32_t now) const;
  bool saidAll(uint32_t now) const { return typed(now) >= box_.length(); }
  void wake(uint32_t now);
  void hop(uint32_t now);
  void emote(Emote e, uint32_t now, uint32_t ms);
  void walkTo(float x, float y, uint32_t now);
  gfx::Rect fieldBoxRect() const;
  gfx::Rect monsterRect() const;
  float randf();

  // Layers, back to front
  SceneLayer backdrop_;
  SpriteLayer foe_;
  PlateLayer foePlate_;
  SpriteLayer monster_;
  PlateLayer minePlate_;
  FxLayer fx_;
  SpriteLayer emote_;
  BoxLayer box_;
  WipeLayer wipe_;

  Move move_ = Move::Flame;                 // the species' move, from its sprites.json entry
  gfx::Color monPal_[2][kMonColors] = {};  // [1]: a flame flickers up
  gfx::Color tilePal_[kTileColors] = {}, foePal_[kFoeColors] = {}, emotePal_[kEmoteColors] = {};

  int width_ = 0, height_ = 0, scale_ = 3, battleScale_ = 3;
  uint32_t seed_ = 1, rng_ = 1, lastMs_ = 0;
  bool active_ = false, started_ = false;

  // The card on screen
  uint32_t cardId_ = 0;
  bool cardUp_ = false;
  int answer_ = -1;
  engine::CardContent card_;

  // On the field
  Stage stage_ = Stage::Field;
  uint32_t stageMs_ = 0, battleAt_ = 0, noticeAt_ = 0;
  float x_ = 0, y_ = 0, targetX_ = 0, targetY_ = 0;
  Dir dir_ = Dir::Down;
  bool walking_ = false, asleep_ = false;
  uint32_t walkMs_ = 0, nextIdeaMs_ = 0, lastActiveMs_ = 0, hopMs_ = 0;
  bool hopping_ = false;
  // Picked up by a finger: hangs just below it (not hidden by the hand) in the
  // air over its shadow, struggling; let go, it drops and walks back home
  Carry carry_ = Carry::None;
  uint32_t carryMs_ = 0;                // when picked up or let go
  float fingerX_ = 0, fingerY_ = 0;
  float grabDx_ = 0, grabDy_ = 0;       // sprite from the finger when picked up
  float homeX_ = 0, homeY_ = 0, dropFrom_ = 0;  // dropFrom_: height when let go
  Emote emoteKind_ = kEmoteBang;
  uint32_t emoteUntilMs_ = 0;
  int preview_ = -1;
  uint32_t previewUntilMs_ = 0;
  engine::Mood mood_ = engine::Mood::Idle;
  uint32_t moodSince_ = 0;

  // Message box
  BoxState boxState_ = BoxState::Closed;
  uint32_t boxMs_ = 0, textMs_ = 0;

  // Battle
  bool menu_ = false, resultShown_ = false, foeGone_ = false;
  uint32_t pressedMs_ = 0;  // when the picked move lit up
  float foeHp_ = 1;
};

}  // namespace ui::monster
