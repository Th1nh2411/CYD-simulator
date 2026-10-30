#pragma once

#include "app/ui/capri/capri_art.h"
#include "app/ui/capri/capri_layers.h"
#include "app/ui/capri/capri_lines.h"
#include "app/ui/capri/capri_style.h"
#include "app/ui/character.h"

namespace ui::capri {

// Capri, the glitchy personality construct of Project Capricorn, as a desk
// companion (after mochiiwuff/capri-desktop's look and lachydotmcg/capri-desktop's
// voice). She holds the screen by its edges in the night sky (her intro
// close-up), bobbing in held key poses, her mane swaying; she talks with her
// mouth, waves hello, squints happily when patted, swings an arm when poked,
// and now and then glitches into slices and afterimages.
// Everything a card brings shows in her cyberpunk message panel in her own
// words: a lead line by the card's tag (done, error, needs you...), the
// sender's text decoding in after it, a state badge and her face; comic
// close-ups flash beside her. Prompts get buttons, then her reaction. On her
// own she greets you by the hour, takes head pats and pokes, pops in now and
// then, sulks when ignored for hours and dozes off. A long press asks her
// brain (an AI on a PC, tools/capri) to talk; its answers come back as
// cards tagged "chat", with reply buttons. Stages cards itself.
class CapriCharacter : public CharacterWith<CapriConfig> {
 public:
  const char* name() const override { return "capri"; }

  void resetSettings() override { edited_ = CapriConfig(); }
  void mergeSettings(JsonObjectConst patch) override;
  bool applyPreset(const char*) override { return false; }
  void settingsToJson(JsonObject out) const override;
  void describe(JsonObject out) const override;

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) override;
  void setActive(bool active) override;
  bool syncSettings() override;
  gfx::Color background() const override { return gfx::rgb(8, 7, 16); }
  void setAnchor(float, float) override {}  // she keeps her own place
  void setAttention(bool, float, float) override {}
  void previewExpression(Expression e, uint32_t untilMs) override;
  void setClock(int minuteOfDay) override { minute_ = minuteOfDay; }
  void update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs) override;
  bool hitTest(int x, int y) const override;
  void poke(float x, float y) override;
  bool longPress(float x, float y) override;
  bool busy(uint32_t nowMs) const override;

  bool presentsCards() const override { return true; }
  void present(const engine::CardState& card, uint32_t nowMs) override;
  bool handleTap(int x, int y, engine::Event& out) override;

  bool takeAsk(char* out, size_t size) override;
  audio::Sound takeSound() override;
  bool takeSpeech(char* out, size_t size) override;
  void setVoiceReady(bool ready) override { voiceReady_ = ready; }

 private:
  enum class Box : uint8_t { Closed, Opening, Open, Closing };
  enum class Talk : uint8_t { Self, Message, Question, Reaction };
  // What kind of news a card is: its tag, or a guess from its kind
  enum class Tag : uint8_t { Info, Alert, Done, Error, Work, Remind, Idle, Start, End, Chat, Count };

  struct Style {
    const char* name;
    const char* badge;
    uint32_t accent;  // 0xRRGGBB, 0 = her accent setting
    Face face;
    int8_t cutin;     // -1 = none
    audio::Cue cue;
    Lines lead;
  };
  static const Style kStyles[int(Tag::Count)];

  void applySettings();
  void applyPalettes();
  void layout();
  Tag tagOf(const engine::CardContent& c) const;
  Face faceNamed(const char* name, Face fallback) const;

  void onCard(uint32_t now);
  void onCardGone(uint32_t now);
  void react(int answer, uint32_t now);
  void sayLine(Lines set, Face face, uint32_t now);  // on her own, when nothing else is up
  void open(Talk talk, const char* sys, const char* text, bool withChoices, gfx::Color accent, const char* badge,
            Face face, uint32_t now);
  void closeBox(uint32_t now);
  void flash(int cutin, uint32_t now);
  void glitch(uint32_t ms, uint32_t now);
  void sound(audio::Cue cue, const audio::Clip* clip = nullptr);
  void wake(uint32_t now);
  bool touched(uint32_t now);  // true if it answers her nudge
  void ask(const char* type, uint32_t now);
  void speak(const char* english);  // out loud, when her voice is on
  void speakRef(uint32_t ref);      // her brain's card: it knows its words in English
  void idleAction(uint32_t now);    // something small she does on her own

  void updateBox(uint32_t now);
  void updateLife(uint32_t now);
  void updatePose(uint32_t now);
  void updateSky(uint32_t now);
  int typed(uint32_t now) const;
  bool typing(uint32_t now) const;
  bool glitching(uint32_t now) const;
  float randf();
  uint32_t rand32();

  // Layers, back to front
  VoidLayer sky_;
  FigureLayer figure_;
  PanelLayer panel_;
  CutinLayer cutin_;
  gfx::Color main_[4 * 256] = {}, ghost_[4 * 256] = {};  // her palette, and the afterimage's tint
  uint8_t ghostOpacity_[256] = {};
  bool dimmed_ = false, ghostRed_ = false;

  LinePicker picker_;
  int width_ = 0, height_ = 0, size_ = 0;
  int figX_ = 0, figY_ = 0;
  int cutinX_ = 0, cutinY_ = 0;
  uint32_t rng_ = 1, lastMs_ = 0;
  bool active_ = false, started_ = false, greeted_ = false;
  uint32_t introMs_ = 0;  // her entrance started; 0 = over
  uint32_t startMs_ = 0;
  int minute_ = -1;

  // Pose
  uint32_t poseMs_ = 0;
  int bobStep_ = 0, bobHeld_ = 0;
  uint32_t glitchUntilMs_ = 0, nextGlitchMs_ = 0, hopMs_ = 0;
  uint32_t waveMs_ = 0, happyUntilMs_ = 0, swingUntilMs_ = 0;  // cels played for a while
  FigureLayer::Pose pose_;
  int cel(uint32_t now) const;
  // Her entrance (mochiiwuff/capri-desktop's intro): the edges crackle, her hands slam onto them,
  // she hauls herself up to the glass and hits it, says hi, waves, grabs on again and beams.
  // Takes over her pose while it runs; false once it is over.
  bool intro(uint32_t now, FigureLayer::Pose& p, int& cel);
  void greet(uint32_t now);

  // Cut-in
  int cutinShown_ = -1, pendingCutin_ = -1;  // pending: flashes as the panel opens
  uint32_t cutinMs_ = 0;

  // Panel
  Box box_ = Box::Closed;
  Talk talk_ = Talk::Self;
  Tag tag_ = Tag::Info;
  uint32_t boxMs_ = 0, openAtMs_ = 0, textMs_ = 0, typedMs_ = 0, pressedMs_ = 0, lastBlipMs_ = 0;
  int lastTyped_ = 0;
  char sys_[64] = "", text_[260] = "";
  gfx::Color accent_ = 0;
  char badge_[12] = "";
  Face face_ = kFaceCalm;
  bool withChoices_ = false, choicesUp_ = false, reacted_ = false;

  // The card on screen
  uint32_t cardId_ = 0;
  bool cardUp_ = false;
  int answer_ = -1;
  engine::CardContent card_;

  // Her own life
  bool asleep_ = false;
  uint32_t lastActiveMs_ = 0, lastTouchMs_ = 0, nextCheckInMs_ = 0, nudgedMs_ = 0;
  bool nudgeOpen_ = false;
  uint32_t waitBrainUntilMs_ = 0;  // asked her brain, its answer not in yet
  bool waitCheckIn_ = false;       // that ask was a pop-in: no "offline" if it stays quiet
  bool voiceReady_ = false;        // her voice answered last time: no blips over it
  uint32_t nextIdleMs_ = 0, mumbleUntilMs_ = 0;
  uint32_t nextSatMs_ = 0, satMs_ = 0;

  // Outbox, taken by the platform after update
  char ask_[96] = "", speech_[200] = "";
  audio::Sound sounds_[4];
  int soundCount_ = 0;
};

}  // namespace ui::capri
