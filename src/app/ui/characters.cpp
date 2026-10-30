#include "app/ui/characters.h"

#include <cstring>

namespace ui {

namespace {
constexpr const char* kActiveKey = "character";
}  // namespace

int CharacterSet::find(const char* name) const {
  for (int i = 0; i < kCount; i++) {
    if (strcmp(list_[i]->name(), name) == 0) return i;
  }
  return -1;
}

bool CharacterSet::merge(JsonObjectConst doc, int& active, uint32_t& touched) {
  touched = 0;
  int pick = active;
  if (doc[kActiveKey].is<const char*>()) {
    pick = find(doc[kActiveKey].as<const char*>());
    if (pick < 0) return false;
  }

  // Keys that are neither the selection nor a character: an older, flat document
  bool flat = false;
  for (JsonPairConst kv : doc) {
    if (strcmp(kv.key().c_str(), kActiveKey) != 0 && find(kv.key().c_str()) < 0) flat = true;
  }
  if (flat) {
    list_[0]->mergeSettings(doc);
    touched |= 1u;
  }
  for (int i = 0; i < kCount; i++) {
    JsonObjectConst part = doc[list_[i]->name()];
    if (part.isNull()) continue;
    list_[i]->mergeSettings(part);
    touched |= 1u << i;
  }
  active = pick;
  return true;
}

void CharacterSet::toJson(int active, JsonObject out) const {
  const Character& c = *list_[active];
  out[kActiveKey] = c.name();
  c.settingsToJson(out[c.name()].to<JsonObject>());
}

void CharacterSet::describe(JsonObject out) const {
  JsonArray exprs = out["expressions"].to<JsonArray>();
  for (size_t i = 0; i < kExpressionCount; i++) exprs.add(expressionName(Expression(i)));
  JsonArray chars = out["characters"].to<JsonArray>();
  for (const Character* c : list_) {
    JsonObject o = chars.add<JsonObject>();
    o["name"] = c->name();
    c->describe(o);
  }
}

}  // namespace ui
