#include "app/ui/blob/character_json.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "app/ui/json_fields.h"

namespace ui::blob {

namespace {

struct EyeField {
  const char* key;
  float EyeShape::*ptr;
  float lo, hi;
};
const EyeField kEyeFields[] = {
    {"w", &EyeShape::w, 0.02f, 0.9f},           {"h", &EyeShape::h, 0.02f, 0.9f},
    {"square", &EyeShape::square, 0, 1},        {"corner", &EyeShape::corner, 0, 1},
    {"tilt", &EyeShape::tilt, -1.2f, 1.2f},     {"lidTop", &EyeShape::lidTop, 0, 1},
    {"lidAngle", &EyeShape::lidAngle, -1.0f, 1.0f}, {"lidBottom", &EyeShape::lidBottom, 0, 1},
    {"arc", &EyeShape::arc, 0, 1},              {"shine", &EyeShape::shine, 0, 2},
    {"browRaise", &EyeShape::browRaise, -0.3f, 0.4f}, {"browAngle", &EyeShape::browAngle, -1.0f, 1.0f},
};

struct PhysicsField {
  const char* key;
  float FluidBody::Params::*ptr;
  float lo, hi;
};
const PhysicsField kPhysicsFields[] = {
    {"stiffness", &FluidBody::Params::stiffness, 10, 1000},
    {"damping", &FluidBody::Params::damping, 0, 40},
    {"tension", &FluidBody::Params::tension, 0, 4000},
    {"volumeKeep", &FluidBody::Params::volumeKeep, 0, 1},
    {"inertia", &FluidBody::Params::inertia, 0, 3},
    {"maxAccel", &FluidBody::Params::maxAccel, 0, 30000},
};

struct MotionField {
  const char* key;
  float MotionParams::*ptr;
  float lo, hi;
};
const MotionField kMotionFields[] = {
    {"glideMs", &MotionParams::glideMs, 120, 1500},
    {"thud", &MotionParams::thud, 0, 1},
    {"drop", &MotionParams::drop, 0, 3},
    {"splash", &MotionParams::splash, 0, 1},
    {"bounce", &MotionParams::bounce, 0, 12},
};

struct BrowField {
  const char* key;
  float BrowConfig::*ptr;
  float lo, hi;
};
const BrowField kBrowFields[] = {
    {"thickness", &BrowConfig::thickness, 0.01f, 0.12f},
    {"length", &BrowConfig::length, 0.3f, 2.0f},
    {"lift", &BrowConfig::lift, -0.1f, 0.4f},
};

struct StyleFloat {
  const char* key;
  float BlobStyle::*ptr;
  float lo, hi;
};
const StyleFloat kStyleFloats[] = {
    {"eyeSpacing", &BlobStyle::eyeSpacing, 0, 0.9f},
    {"eyeHeight", &BlobStyle::eyeHeight, -0.8f, 0.8f},
    {"gazeReach", &BlobStyle::gazeReach, 0, 0.4f},
    {"shineTravel", &BlobStyle::shineTravel, 0, 0.8f},
};

struct StyleAlpha {
  const char* key;
  uint8_t BlobStyle::*ptr;
};
const StyleAlpha kStyleAlphas[] = {
    {"opacity", &BlobStyle::opacity},
    {"highlight", &BlobStyle::highlightAlpha},
    {"cheekAlpha", &BlobStyle::cheekAlpha},
};

struct StyleColor {
  const char* key;
  gfx::Color BlobStyle::*ptr;
};
const StyleColor kStyleColors[] = {
    {"body", &BlobStyle::body},
    {"eye", &BlobStyle::eye},
    {"shine", &BlobStyle::shine},
    {"cheek", &BlobStyle::cheek},
};

}  // namespace

using namespace ui::json;

void toJson(const CharacterConfig& c, JsonObject doc) {
  char hex[8];
  doc["size"] = round3(c.size);
  doc["bodyShape"] = bodyShapeName(BodyShape(c.bodyShape));
  doc["aspect"] = round3(c.aspect);
  hexOf(c.background, hex);
  doc["background"] = hex;
  for (const StyleColor& f : kStyleColors) {
    hexOf(c.style.*f.ptr, hex);
    doc[f.key] = hex;
  }
  for (const StyleAlpha& f : kStyleAlphas) doc[f.key] = c.style.*f.ptr;
  for (const StyleFloat& f : kStyleFloats) doc[f.key] = round3(c.style.*f.ptr);
  doc["eyeSet"] = c.eyeSetName;

  JsonObject eyes = doc["eyes"].to<JsonObject>();
  for (size_t e = 0; e < kExpressionCount; e++) {
    JsonObject shape = eyes[expressionName(Expression(e))].to<JsonObject>();
    for (const EyeField& f : kEyeFields) shape[f.key] = round3(c.eyes[e].*f.ptr);
  }
  JsonObject brows = doc["brows"].to<JsonObject>();
  brows["style"] = browStyleName(BrowStyle(c.brows.style));
  hexOf(c.brows.color, hex);
  brows["color"] = hex;
  for (const BrowField& f : kBrowFields) brows[f.key] = round3(c.brows.*f.ptr);

  JsonObject acc = doc["accessories"].to<JsonObject>();
  for (size_t i = 0; i < kSlotCount; i++) {
    const Slot slot = Slot(i);
    const AccessorySlot& a = c.accessories.slots[i];
    JsonObject o = acc[slotName(slot)].to<JsonObject>();
    o["type"] = itemName(slot, a.type);
    hexOf(a.color, hex);
    o["color"] = hex;
    hexOf(a.color2, hex);
    o["color2"] = hex;
    o["size"] = round3(a.size);
    o["angle"] = round3(a.angle);
  }
  JsonObject physics = doc["physics"].to<JsonObject>();
  for (const PhysicsField& f : kPhysicsFields) physics[f.key] = round3(c.physics.*f.ptr);
  JsonObject motion = doc["motion"].to<JsonObject>();
  for (const MotionField& f : kMotionFields) motion[f.key] = round3(c.motion.*f.ptr);
}

bool mergeJson(const char* json, size_t len, CharacterConfig& c) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  JsonObjectConst o = doc.as<JsonObjectConst>();
  if (o.isNull()) return false;
  mergeJson(o, c);
  return true;
}

void mergeJson(JsonObjectConst o, CharacterConfig& c) {
  readFloat(o["size"], c.size, 0.05f, 0.45f);
  if (o["bodyShape"].is<const char*>()) {
    int shape = findBodyShape(o["bodyShape"].as<const char*>());
    if (shape >= 0) c.bodyShape = uint8_t(shape);
  }
  readFloat(o["aspect"], c.aspect, 0.6f, 1.6f);
  readColor(o["background"], c.background);
  for (const StyleColor& f : kStyleColors) readColor(o[f.key], c.style.*f.ptr);
  for (const StyleAlpha& f : kStyleAlphas) {
    if (o[f.key].is<int>()) c.style.*f.ptr = uint8_t(std::max(0, std::min(32, o[f.key].as<int>())));
  }
  for (const StyleFloat& f : kStyleFloats) readFloat(o[f.key], c.style.*f.ptr, f.lo, f.hi);

  if (o["eyeSet"].is<const char*>()) {
    int set = findEyeSet(o["eyeSet"].as<const char*>());
    if (set >= 0) c.useEyeSet(set);
  }
  JsonObjectConst eyes = o["eyes"];
  if (!eyes.isNull()) {
    for (size_t e = 0; e < kExpressionCount; e++) {
      JsonObjectConst shape = eyes[expressionName(Expression(e))];
      if (shape.isNull()) continue;
      for (const EyeField& f : kEyeFields) readFloat(shape[f.key], c.eyes[e].*f.ptr, f.lo, f.hi);
    }
  }
  JsonObjectConst brows = o["brows"];
  if (!brows.isNull()) {
    if (brows["style"].is<const char*>()) {
      int style = findBrowStyle(brows["style"].as<const char*>());
      if (style >= 0) c.brows.style = uint8_t(style);
    }
    readColor(brows["color"], c.brows.color);
    for (const BrowField& f : kBrowFields) readFloat(brows[f.key], c.brows.*f.ptr, f.lo, f.hi);
  }

  JsonObjectConst acc = o["accessories"];
  if (!acc.isNull()) {
    for (size_t i = 0; i < kSlotCount; i++) {
      const Slot slot = Slot(i);
      JsonObjectConst s = acc[slotName(slot)];
      if (s.isNull()) continue;
      AccessorySlot& a = c.accessories.slots[i];
      if (s["type"].is<const char*>()) {
        int type = findItem(slot, s["type"].as<const char*>());
        if (type >= 0) a.type = uint8_t(type);
      }
      readColor(s["color"], a.color);
      readColor(s["color2"], a.color2);
      readFloat(s["size"], a.size, 0.3f, 2.5f);
      readFloat(s["angle"], a.angle, -90, 90);
    }
  }
  JsonObjectConst physics = o["physics"];
  for (const PhysicsField& f : kPhysicsFields) readFloat(physics[f.key], c.physics.*f.ptr, f.lo, f.hi);
  JsonObjectConst motion = o["motion"];
  for (const MotionField& f : kMotionFields) readFloat(motion[f.key], c.motion.*f.ptr, f.lo, f.hi);
}

}  // namespace ui::blob
