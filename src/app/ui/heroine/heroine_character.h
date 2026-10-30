#pragma once

#include "app/ui/character.h"
#include "app/ui/heroine/heroine_layers.h"
#include "app/ui/heroine/heroine_portrait.h"
#include "app/ui/heroine/heroine_room.h"
#include "app/ui/heroine/heroine_style.h"

namespace ui::heroine {

// A visual-novel heroine: a painted portrait in her room by the window, the
// sky and the room's light following the clock. Her picture is painted
// beforehand in a few head tilts: she breathes, tilts her head and nods, in
// steps. She blinks and glances around; expressions and her mouth are frames
// of the same picture. A pat on
// the head makes her smile, a poke annoys her. Everything a card brings shows
// in the dialog box along the bottom: notifications as her lines, prompts as
// her question with the options as choices under it, then her reaction to the
// answer. Stages cards itself (Character::presentsCards).
class HeroineCharacter : public CharacterWith<HeroineConfig> {
 public:
  const char* name() const override { return "heroine"; }

  void resetSettings() override { edited_ = HeroineConfig(); }
  void mergeSettings(JsonObjectConst patch) override;
  bool applyPreset(const char*) override { return false; }  // none yet
  void settingsToJson(JsonObject out) const override;
  void describe(JsonObject out) const override;

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) override;
  void setActive(bool active) override;
  bool syncSettings() override;
  gfx::Color background() const override { return kRoomWall[period_ < 0 ? kRoomDay : period_]; }
  void setAnchor(float, float) override {}  // she stays by her window
  void setAttention(bool, float, float) override {}
  void previewExpression(Expression e, uint32_t untilMs) override;
  void setClock(int minuteOfDay) override;
  void update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs) override;
  bool hitTest(int x, int y) const override;
  void poke(float x, float y) override;
  bool busy(uint32_t nowMs) const override;

  bool presentsCards() const override { return true; }
  void present(const engine::CardState& card, uint32_t nowMs) override;
  bool handleTap(int x, int y, engine::Event& out) override;

 private:
  enum class BoxState : uint8_t { Closed, Opening, Open, Closing };
  enum class Talk : uint8_t { Message, Question, Reaction };
  enum class Reaction : uint8_t { None, Pat, Poke };
  // What her face shows, before blinks and talking
  enum class Look : uint8_t { Normal, Happy, Surprised, Sleepy, Asleep, Annoyed, Sad };

  void applySettings();
  void applyLight();
  void onCard(uint32_t now);
  void onCardGone(uint32_t now);
  void react(int answer, uint32_t now);
  void openBox(uint32_t now);
  void closeBox(uint32_t now);
  void say(const char* title, const char* body, bool withChoices, uint32_t now);
  void updateDialog(uint32_t now);
  void updateFace(uint32_t now);
  void updatePose(float dt, uint32_t now);
  Look look(uint32_t now) const;
  int typed(uint32_t now) const;
  bool typing(uint32_t now) const;
  void wake(uint32_t now);
  gfx::Rect boxRect() const;
  float randf();

  // Layers, back to front: room, portrait, dialog
  BackdropLayer room_;
  PortraitLayer portrait_;
  DialogLayer dialog_;
  gfx::Color palette_[4 * 256] = {};  // her palette, adjusted for the panel and dithered (gfx::SpanPalette)

  int width_ = 0, height_ = 0, baseX_ = 0, baseY_ = 0;
  uint32_t rng_ = 1, lastMs_ = 0;
  bool active_ = false, started_ = false;
  int minute_ = -1, period_ = -1;  // clock, and the time of day the room is lit for

  // Face and motion
  uint32_t blinkMs_ = 0, nextBlinkMs_ = 0;
  bool doubleBlink_ = false;
  int glance_ = -1;  // look frame, -1 = straight
  uint32_t glanceUntilMs_ = 0, nextGlanceMs_ = 0;
  Reaction reaction_ = Reaction::None;
  uint32_t reactionMs_ = 0, hopMs_ = 0, surprisedUntilMs_ = 0;
  float patSide_ = 0;  // -1 patted from her right (the viewer's left), 1 from her left
  // Head, on springs, and the painted tilt shown for it
  float roll_ = 0, rollV_ = 0, headY_ = 0, headYV_ = 0;
  int tiltLevel_ = 0;
  float tilt_ = 0;
  uint32_t tiltUntilMs_ = 0, nextTiltMs_ = 0, poseMs_ = 0;
  int preview_ = -1;
  uint32_t previewUntilMs_ = 0;
  engine::Mood mood_ = engine::Mood::Idle;
  bool asleep_ = false;
  uint32_t lastActiveMs_ = 0;

  // The card on screen
  uint32_t cardId_ = 0;
  bool cardUp_ = false;
  int answer_ = -1;
  engine::CardContent card_;

  // Dialog box
  BoxState boxState_ = BoxState::Closed;
  Talk talk_ = Talk::Message;
  uint32_t boxMs_ = 0, openAtMs_ = 0, textMs_ = 0, typedMs_ = 0, pressedMs_ = 0;
  bool choicesUp_ = false, reacted_ = false;
  Look reactionLook_ = Look::Normal;
};

}  // namespace ui::heroine
