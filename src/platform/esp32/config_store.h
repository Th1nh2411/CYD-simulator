#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "app/ui/characters.h"
#include "hal/touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace platform {

// Owns every character's settings and which character is shown. Writers (web
// server, serial) take a mutex; each character hands its settings to the UI
// task lock-free (Character::publishSettings), the selection is an atomic.
// Persisted in NVS: one JSON string per character plus the selection.
class ConfigStore {
 public:
  void begin(ui::CharacterSet& characters);  // loads the saved settings, or defaults
  // Settings document, whole or partial (see CharacterSet); may switch characters
  bool merge(const char* json, size_t len);
  bool applyPreset(const char* name);  // active character: defaults + preset, not saved until save()
  void reset();                        // active character back to defaults, not saved until save()
  size_t toJson(char* out, size_t size);  // {"character":<active>,"<active>":{...}}
  bool save();
  // Touch calibration saved on this device; false leaves cal (the board default) as is
  bool loadTouchCalibration(hal::TouchCalibration& cal);
  bool saveTouchCalibration(const hal::TouchCalibration& cal);
  // Screen rotation in quarter turns and mirror (hal::initBoard), read before anything else starts
  static void loadScreen(int& quarterTurns, bool& mirror);
  static bool saveScreen(int quarterTurns, bool mirror);

  int active() const { return active_.load(std::memory_order_acquire); }  // any thread

 private:
  void publish(uint32_t touched);

  ui::CharacterSet* characters_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  std::atomic<int> active_{0};
};

}  // namespace platform
