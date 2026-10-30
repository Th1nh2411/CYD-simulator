#pragma once

#include "app/ui/blob/blob_animator.h"
#include "app/ui/blob/blob_layer.h"
#include "app/ui/blob/blob_style.h"
#include "app/ui/character.h"

namespace ui::blob {

// The fluid slime (git tag `slime`): BlobAnimator + BlobLayer behind the
// Character interface. Its settings are CharacterConfig, saved as before.
class SlimeCharacter : public CharacterWith<CharacterConfig> {
 public:
  const char* name() const override { return "slime"; }

  void resetSettings() override;
  void mergeSettings(JsonObjectConst patch) override;
  bool applyPreset(const char* name) override;
  void settingsToJson(JsonObject out) const override;
  void describe(JsonObject out) const override;

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) override;
  void setActive(bool active) override { layer_.setVisible(active); }
  bool syncSettings() override;
  gfx::Color background() const override { return settings_.background; }
  void setAnchor(float x, float y) override { anim_.setAnchor(x, y); }
  void setAttention(bool active, float x, float y) override { anim_.setAttention(active, x, y); }
  void setTilt(float x, float y) override { anim_.setTilt(x, y); }
  void previewExpression(Expression e, uint32_t untilMs) override;
  void update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs) override;
  bool hitTest(int x, int y) const override { return layer_.bodyRect().inflate(6).contains(x, y); }
  void poke(float x, float y) override { anim_.poke(x, y); }
  // A long press makes it flinch (ready to be carried) rather than change face
  bool longPress(float, float) override {
    anim_.flinch();
    return true;
  }
  // Long press then drag: carried along; a plain drag: stretched or pushed in
  bool grab(float x, float y, bool held) override {
    anim_.grab(x, y, held);
    return true;
  }
  void drag(float x, float y) override { anim_.drag(x, y); }
  void release() override { anim_.letGo(); }
  bool busy(uint32_t nowMs) const override { return anim_.busy(nowMs); }

 private:
  float radius() const { return settings_.size * minSide_; }

  BlobAnimator anim_;  // keeps a pointer into settings_ (eye shapes)
  BlobLayer layer_;
  float minSide_ = 1;  // smaller screen side, px
};

}  // namespace ui::blob
