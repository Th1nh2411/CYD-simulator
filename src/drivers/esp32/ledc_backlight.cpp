#include "drivers/esp32/ledc_backlight.h"

#include <Arduino.h>

#include "driver/gpio.h"

namespace drivers {

void LedcBacklight::begin(int pin, bool activeLow, uint8_t channel, uint32_t freqHz) {
  pin_ = pin;
  channel_ = channel;
  activeLow_ = activeLow;
  gpio_hold_dis(gpio_num_t(pin));  // in case holdOff() outlived a reset
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, freqHz, 8);
#else
  ledcSetup(channel, freqHz, 8);
  ledcAttachPin(pin, channel);
#endif
  set(0);
}

void LedcBacklight::set(uint8_t level) {
  // 255 is a steady high (the core writes it as the full period), 0 a steady low
  if (activeLow_) level = uint8_t(255 - level);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin_, level);
#else
  ledcWrite(channel_, level);
#endif
}

void LedcBacklight::holdOff() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(pin_);
#else
  ledcDetachPin(pin_);
#endif
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, activeLow_ ? HIGH : LOW);
  gpio_hold_en(gpio_num_t(pin_));
  gpio_deep_sleep_hold_en();
}

}  // namespace drivers
