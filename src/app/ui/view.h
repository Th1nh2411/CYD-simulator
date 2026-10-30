#pragma once

#include <cstdint>

#include "app/engine/events.h"
#include "app/engine/world.h"
#include "app/gfx/compositor.h"
#include "app/ui/characters.h"
#include "app/ui/expression.h"
#include "app/ui/layers.h"
#include "app/ui/motion_gesture.h"
#include "app/ui/touch_gesture.h"

namespace ui {

// Presentation of the engine state. Owns the layers and the layout: the
// character floats centered, and moves aside (up for a notification, left for
// a prompt) to make room for the card. Turns taps into semantic events.
// Layout derives from the display size and orientation, nothing is hardcoded.
// Hosts every character of the set; only the active one is shown and animated.
class View {
 public:
  void begin(gfx::Compositor& comp, int width, int height, CharacterSet& characters, int active,
             uint32_t seed);
  // Each frame: switches to character `index` if needed, applies newly published settings
  void syncCharacter(int index);
  void previewExpression(Expression e, uint32_t untilMs);
  void update(const engine::WorldState& w, uint32_t nowMs);
  // Produces the engine event for a tap and plays the immediate visual reaction
  bool handleTap(int x, int y, engine::Event& out);
  // Any touch gesture at (x, y): a tap as above; a long press on the character
  // gets its own reaction or shows its next expression for a while; a drag
  // that starts on the character
  // moves it (Character::grab, told whether a long press came first), and let
  // go it goes back. True if it made an engine event.
  bool handleGesture(TouchGesture::Kind kind, int x, int y, uint32_t nowMs, engine::Event& out);
  // Motion sensor: the tilt goes to the character. A shake closes a
  // notification; held tilted left or right, the device answers a prompt with
  // its first or last option. True if it made an engine event.
  void setTilt(float x, float y) { character_->setTilt(x, y); }
  bool handleMotion(MotionGesture::Kind kind, engine::Event& out);
  // A card is up, whoever draws it
  bool showsCard() const { return shownKind_ != engine::CardKind::None; }
  void setDebugText(const char* text);
  // Local time of day for the clock and the character; -1 while unknown (clock hidden)
  void setClock(int minuteOfDay);
  // Whether the next frames need the full frame rate (motion, card slides)
  bool wantsFullFrameRate(uint32_t nowMs) const;
  // What the active character sends out after update (Character::takeAsk, takeSound)
  bool takeAsk(char* out, size_t size) { return character_->takeAsk(out, size); }
  audio::Sound takeSound() { return character_->takeSound(); }
  bool takeSpeech(char* out, size_t size) { return character_->takeSpeech(out, size); }
  void setVoiceReady(bool ready) { character_->setVoiceReady(ready); }

 private:
  struct Layout {
    float charX = 0, charY = 0;  // where the character floats
    gfx::Rect card;              // where the card rests when shown
    int hideDx = 0, hideDy = 0;  // offset of the card when slid out
    bool narrowCard = false;     // use the small body font
  };
  Layout layoutFor(engine::CardKind kind) const;
  bool onCharacter(int x, int y) const;  // the character itself, not a card over it
  void endDrag();

  void applyBackground();

  BackgroundLayer background_;
  TextLayer hud_;
  ClockLayer clock_;
  CardLayer card_;
  CharacterSet* characters_ = nullptr;
  Character* character_ = nullptr;  // the active one
  int active_ = 0;

  int width_ = 0, height_ = 0, margin_ = 0, textScale_ = 1;
  int minute_ = -1;
  uint32_t cardId_ = 0;
  engine::CardKind cardKind_ = engine::CardKind::None;
  engine::CardKind shownKind_ = engine::CardKind::None;  // card up now, whoever draws it
  int shownOptions_ = 0;
  Layout cardLayout_;
  int pressed_ = -1;
  uint32_t cardChangedMs_ = 0;
  uint8_t heldFaces_ = 0;  // expressions shown by long presses so far
  bool carrying_ = false;  // the character is being dragged
};

}  // namespace ui
