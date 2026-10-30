#include "platform/common/api_json.h"

#include <ArduinoJson.h>

#include <cstring>

#include "app/core/utf8.h"

namespace platform::api {

size_t metaJson(const ui::CharacterSet& characters, char* out, size_t size) {
  JsonDocument doc;
  characters.describe(doc.to<JsonObject>());
  size_t n = serializeJson(doc, out, size);
  return n < size ? n : 0;
}

bool cardFromJson(const char* json, size_t len, engine::Event& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  out = engine::Event{};
  out.type = engine::EventType::ShowCard;
  engine::CardContent& c = out.card;
  c.kind = strcmp(doc["kind"] | "", "prompt") == 0 ? engine::CardKind::Prompt : engine::CardKind::Notification;
  c.sticky = doc["sticky"] | false;
  c.ref = doc["ref"] | 0u;
  core::copyUtf8(c.tag, sizeof(c.tag), doc["tag"] | "");
  core::copyUtf8(c.face, sizeof(c.face), doc["face"] | "");
  core::copyUtf8(c.title, sizeof(c.title), doc["title"] | "");
  core::copyUtf8(c.body, sizeof(c.body), doc["body"] | "");
  if (c.kind == engine::CardKind::Prompt) {
    for (JsonVariantConst opt : doc["options"].as<JsonArrayConst>()) {
      if (c.optionCount == engine::kMaxOptions) break;
      core::copyUtf8(c.options[c.optionCount++], sizeof(c.options[0]), opt | "");
    }
    if (c.optionCount == 0) return false;
  }
  return true;
}

bool isAsk(const char* json, size_t len) {
  JsonDocument doc;
  return len <= 600 && !deserializeJson(doc, json, len) && doc["type"].is<const char*>();
}

bool localTimeFromJson(const char* json, size_t len, int64_t& localSeconds) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc["epoch"].is<double>()) return false;
  localSeconds = int64_t(doc["epoch"].as<double>()) + int64_t(doc["offset"] | 0) * 60;
  return true;
}

bool screenFromJson(const char* json, size_t len, int& degrees, bool& mirror) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  JsonVariantConst rotation = doc["rotation"], mirrored = doc["mirror"];
  if ((!rotation.isNull() && !rotation.is<int>()) || (!mirrored.isNull() && !mirrored.is<bool>())) return false;
  if (!rotation.isNull()) degrees = rotation.as<int>();
  if (!mirrored.isNull()) mirror = mirrored.as<bool>();
  return true;
}

}  // namespace platform::api
