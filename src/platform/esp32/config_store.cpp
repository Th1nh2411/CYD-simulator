#include "platform/esp32/config_store.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "app/ui/touch_calibration.h"

#include <algorithm>
#include <memory>

namespace platform {

namespace {
constexpr const char* kNamespace = "cyd";
constexpr const char* kTouchKey = "touchcal";
constexpr const char* kRotationKey = "rotation";  // quarter turns
constexpr const char* kMirrorKey = "mirror";
constexpr const char* kActiveKey = "active";
// The slime tag saved the slime's settings, flat, under this key
constexpr const char* kLegacyKey = "character";
constexpr size_t kJsonMax = 4096;

void loadInto(Preferences& prefs, const char* key, ui::Character& c) {
  if (!prefs.isKey(key)) return;
  String json = prefs.getString(key);
  JsonDocument doc;
  if (!deserializeJson(doc, json.c_str(), json.length()) && doc.is<JsonObject>()) {
    c.mergeSettings(doc.as<JsonObjectConst>());
  }
}
}  // namespace

void ConfigStore::begin(ui::CharacterSet& characters) {
  characters_ = &characters;
  mutex_ = xSemaphoreCreateMutex();
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {  // read-write so a fresh device creates the namespace quietly
    loadInto(prefs, kLegacyKey, characters[0]);
    for (int i = 0; i < characters.count(); i++) loadInto(prefs, characters[i].name(), characters[i]);
    if (prefs.isKey(kActiveKey)) active_ = std::max(0, characters.find(prefs.getString(kActiveKey).c_str()));
    prefs.end();
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  publish(~0u);
  xSemaphoreGive(mutex_);
}

void ConfigStore::publish(uint32_t touched) {
  for (int i = 0; i < characters_->count(); i++) {
    if (touched & (1u << i)) (*characters_)[i].publishSettings();
  }
}

bool ConfigStore::merge(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  int active = active_;
  uint32_t touched = 0;
  bool ok = characters_->merge(doc.as<JsonObjectConst>(), active, touched);
  if (ok) {
    publish(touched);  // settings first, so a switch shows them right away
    active_ = active;
  }
  xSemaphoreGive(mutex_);
  return ok;
}

bool ConfigStore::applyPreset(const char* name) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  int a = active_;
  bool ok = (*characters_)[a].applyPreset(name);
  if (ok) publish(1u << a);
  xSemaphoreGive(mutex_);
  return ok;
}

void ConfigStore::reset() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  int a = active_;
  (*characters_)[a].resetSettings();
  publish(1u << a);
  xSemaphoreGive(mutex_);
}

size_t ConfigStore::toJson(char* out, size_t size) {
  JsonDocument doc;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  characters_->toJson(active_, doc.to<JsonObject>());
  xSemaphoreGive(mutex_);
  size_t n = serializeJson(doc, out, size);
  return n < size ? n : 0;
}

bool ConfigStore::loadTouchCalibration(hal::TouchCalibration& cal) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  String json = prefs.getString(kTouchKey, "");
  prefs.end();
  return json.length() && ui::touchCalibrationFromJson(json.c_str(), cal);
}

bool ConfigStore::saveTouchCalibration(const hal::TouchCalibration& cal) {
  char json[128];
  if (!ui::touchCalibrationToJson(cal, json, sizeof(json))) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.putString(kTouchKey, json) > 0;
  prefs.end();
  return ok;
}

void ConfigStore::loadScreen(int& quarterTurns, bool& mirror) {
  quarterTurns = 0;
  mirror = false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  if (prefs.isKey(kRotationKey)) quarterTurns = prefs.getUChar(kRotationKey) & 3;
  if (prefs.isKey(kMirrorKey)) mirror = prefs.getBool(kMirrorKey);
  prefs.end();
}

bool ConfigStore::saveScreen(int quarterTurns, bool mirror) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.putUChar(kRotationKey, uint8_t(quarterTurns & 3)) == 1 && prefs.putBool(kMirrorKey, mirror) == 1;
  prefs.end();
  return ok;
}

bool ConfigStore::save() {
  std::unique_ptr<char[]> json(new char[kJsonMax]);
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = true;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (int i = 0; i < characters_->count(); i++) {
    const ui::Character& c = (*characters_)[i];
    JsonDocument doc;
    c.settingsToJson(doc.to<JsonObject>());
    size_t n = serializeJson(doc, json.get(), kJsonMax);
    ok = ok && n < kJsonMax && prefs.putString(c.name(), json.get()) == n;
  }
  ok = ok && prefs.putString(kActiveKey, (*characters_)[active_].name()) > 0;
  xSemaphoreGive(mutex_);
  if (ok && prefs.isKey(kLegacyKey)) prefs.remove(kLegacyKey);  // now saved under the slime's own key
  prefs.end();
  return ok;
}

}  // namespace platform
