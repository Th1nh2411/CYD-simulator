#include "app/ui/blob/slime_character.h"

#include <algorithm>

#include "app/ui/blob/body_shape.h"
#include "app/ui/blob/character_json.h"
#include "app/ui/blob/character_presets.h"

namespace ui::blob {

void SlimeCharacter::resetSettings() { edited_ = CharacterConfig(); }

void SlimeCharacter::mergeSettings(JsonObjectConst patch) { mergeJson(patch, edited_); }

bool SlimeCharacter::applyPreset(const char* name) { return blob::applyPreset(name, edited_); }

void SlimeCharacter::settingsToJson(JsonObject out) const { toJson(edited_, out); }

void SlimeCharacter::describe(JsonObject out) const {
  JsonArray sets = out["eyeSets"].to<JsonArray>();
  for (int i = 0; i < eyeSetCount(); i++) sets.add(eyeSet(i).name);
  JsonObject items = out["items"].to<JsonObject>();
  for (size_t s = 0; s < kSlotCount; s++) {
    JsonArray list = items[slotName(Slot(s))].to<JsonArray>();
    for (int i = 0; i < itemCount(Slot(s)); i++) list.add(itemName(Slot(s), i));
  }
  JsonArray shapes = out["bodyShapes"].to<JsonArray>();
  for (int i = 0; i < bodyShapeCount(); i++) shapes.add(bodyShapeName(BodyShape(i)));
  JsonArray brows = out["browStyles"].to<JsonArray>();
  for (int i = 0; i < browStyleCount(); i++) brows.add(browStyleName(BrowStyle(i)));
  JsonArray presets = out["presets"].to<JsonArray>();
  for (int i = 0; i < presetCount(); i++) presets.add(presetName(i));
}

void SlimeCharacter::attach(gfx::Compositor& comp, int z, int screenW, int screenH, uint32_t seed) {
  minSide_ = float(std::min(screenW, screenH));
  takeSettings();
  anim_.begin(settings_, radius(), seed);
  layer_.setConfig(settings_);
  comp.add(&layer_, z);
}

bool SlimeCharacter::syncSettings() {
  if (!takeSettings()) return false;
  anim_.configure(settings_, radius());
  layer_.setConfig(settings_);
  return true;
}

void SlimeCharacter::previewExpression(Expression e, uint32_t untilMs) {
  anim_.overrideExpression(int(e), untilMs);
}

void SlimeCharacter::update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs) {
  anim_.update(mood, moodSinceMs, nowMs);
  layer_.update(anim_);
}

}  // namespace ui::blob
