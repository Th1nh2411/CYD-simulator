#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "app/audio/synth.h"
#include "app/core/triple_buffer.h"
#include "app/engine/events.h"
#include "app/engine/world.h"
#include "app/gfx/compositor.h"
#include "app/ui/expression.h"

namespace ui {

// A character the view can host. The view owns the layout, the cards and the
// background layer; a character owns its look, its motion and its settings.
//
// Two sides, used from different threads:
//  - settings (writer side): edited as JSON by whoever owns the settings
//    (web server, serial), who serializes these calls, then published
//  - presentation (UI task): takes up published settings and draws
class Character {
 public:
  virtual ~Character() = default;
  // Settings and NVS key (at most 15 chars); never one of another character's settings keys
  virtual const char* name() const = 0;

  // ---- Settings (writer side) ----
  virtual void resetSettings() = 0;  // defaults
  // Merges the keys present; missing keys keep their values, values are clamped
  virtual void mergeSettings(JsonObjectConst patch) = 0;
  virtual bool applyPreset(const char* name) = 0;  // defaults + preset; false if unknown
  virtual void settingsToJson(JsonObject out) const = 0;
  virtual void publishSettings() = 0;  // hands the edited settings to the UI side
  // Option lists and presets for the settings page. Constant: any thread.
  virtual void describe(JsonObject out) const = 0;

  // ---- Presentation (UI task) ----
  // Takes the published settings and adds its layers at depth z (up to z + 99)
  virtual void attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) = 0;
  // Shown and animated, or hidden; an inactive character costs nothing
  virtual void setActive(bool active) = 0;
  // Applies newly published settings; true if there were any
  virtual bool syncSettings() = 0;
  virtual gfx::Color background() const = 0;
  // Where the layout wants the character; moving it is the character's business
  virtual void setAnchor(float x, float y) = 0;
  // Look at a point while active (e.g. the card that just appeared)
  virtual void setAttention(bool active, float x, float y) = 0;
  virtual void previewExpression(Expression e, uint32_t untilMs) = 0;
  // Local time of day in minutes, -1 while unknown (e.g. for a scene that follows the clock)
  virtual void setClock(int /*minuteOfDay*/) {}
  // Where gravity pulls along the screen, relative to the device's resting
  // pose (MotionGesture): x right, y down, about -1..1. Boards with a motion
  // sensor only; 0, 0 otherwise
  virtual void setTilt(float /*x*/, float /*y*/) {}
  virtual void update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs) = 0;
  // The character itself is at this point (not a box it shows)
  virtual bool hitTest(int x, int y) const = 0;
  virtual void poke(float x, float y) = 0;  // react to a tap at this point
  // A long press at this point: true if the character reacts itself, else it
  // shows its next expression for a while
  virtual bool longPress(float /*x*/, float /*y*/) { return false; }
  // Grabbed by a finger at a point on it (false if it cannot be), then moved by
  // the finger until let go, when it goes back. held: a long press came first
  // (a character may carry it then, and do something else on a plain drag).
  virtual bool grab(float /*x*/, float /*y*/, bool /*held*/) { return false; }
  virtual void drag(float /*x*/, float /*y*/) {}
  virtual void release() {}
  // Fast motion under way: the next frames want the full frame rate
  virtual bool busy(uint32_t nowMs) const = 0;

  // ---- Characters that stage cards themselves (UI task) ----
  // E.g. a dialog box or a battle scene instead of the shared card. The view
  // then hides its card, leaves positioning to the character, and hands it the
  // card state every frame (before update) and every tap.
  virtual bool presentsCards() const { return false; }
  virtual void present(const engine::CardState&, uint32_t /*nowMs*/) {}
  // Turns a tap into an engine event; false if it means nothing to the engine
  virtual bool handleTap(int /*x*/, int /*y*/, engine::Event& /*out*/) { return false; }

  // ---- What a character sends out (UI task, after update) ----
  // A request to whoever answers for the device (an AI on a PC: MQTT <topic>/ask),
  // as JSON written into out; false if there is none
  virtual bool takeAsk(char* /*out*/, size_t /*size*/) { return false; }
  // A sound to play on a board with a speaker; Cue::None if there is none
  virtual audio::Sound takeSound() { return audio::Sound{}; }
  // Words to say out loud, for whoever has a voice for them (Capri's brain: tools/capri
  // /tts), as JSON {"text"} or {"ref"} (the card whose words they are) into out; false if none
  virtual bool takeSpeech(char* /*out*/, size_t /*size*/) { return false; }
  // Whether the last words sent out were spoken (a voice answered), or not: no voice to lean on then
  virtual void setVoiceReady(bool /*ready*/) {}
};

// Settings plumbing for a character whose settings are one plain struct: the
// writer edits its own copy, the UI side gets published copies lock-free.
template <typename Settings>
class CharacterWith : public Character {
 public:
  void publishSettings() override {
    mailbox_.back() = edited_;
    mailbox_.publish();
  }

 protected:
  // UI side: copies the newest published settings into settings_
  bool takeSettings() {
    if (!mailbox_.update()) return false;
    settings_ = mailbox_.front();
    return true;
  }

  Settings edited_;    // writer side
  Settings settings_;  // UI side: what is on screen

 private:
  core::TripleBuffer<Settings> mailbox_;
};

}  // namespace ui
