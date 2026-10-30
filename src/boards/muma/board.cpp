// Spotpear ESP32-S3 AI 1.54 "muma" (sold as a DeepSeek Box; xiaozhi board
// sp-esp32-s3-1.54-muma): ESP32-S3 (16 MB flash, 8 MB octal PSRAM), 1.54"
// 240x240 ST7789 on SPI, CST816D capacitive touch on I2C (touch models only),
// battery. Pins and panel setup from the xiaozhi board files.

#include <Arduino.h>

#include "driver/gpio.h"
#include "drivers/common/rotated_touch.h"
#include "drivers/esp32/cst816_touch.h"
#include "drivers/esp32/ledc_backlight.h"
#include "drivers/esp32/mipi_dcs_spi_display.h"
#include "hal/board.h"

namespace {
// VCOM 0x38 (power-on 0x20), as the vendor firmware sets it for this panel
constexpr drivers::PanelInitCmd kPanelCmds[] = {{0xBB, 1, {0x38}, 0}};
constexpr drivers::PanelDesc kPanel{"ST7789", kPanelCmds, 1};
constexpr int kPowerHoldPin = 3;

drivers::MipiDcsSpiDisplay display;
drivers::Cst816Touch touch;
drivers::RotatedTouch rotatedTouch;
drivers::LedcBacklight backlight;
hal::Board board;
}  // namespace

hal::Board& hal::initBoard(int quarterTurns, bool mirror) {
  // The vendor firmware drives IO3 high from boot and low only to power off on
  // battery (then holds it through deep sleep): release a hold and keep it high.
  gpio_hold_dis(gpio_num_t(kPowerHoldPin));
  pinMode(kPowerHoldPin, OUTPUT);
  digitalWrite(kPowerHoldPin, HIGH);

  drivers::MipiDcsSpiDisplay::Config lcd;
  lcd.panel = &kPanel;
  lcd.spiHost = 2;  // SPI3 as in the vendor firmware, pins through the GPIO matrix
  lcd.sclk = 4;
  lcd.mosi = 2;
  lcd.cs = 5;
  lcd.dc = 47;
  lcd.rst = 38;
  lcd.pclkHz = 40000000;
  lcd.width = 240;
  lcd.height = 240;
  lcd.madctl = 0x00;  // RGB, no rotation
  lcd.quarterTurns = quarterTurns;
  lcd.mirror = mirror;
  lcd.ramHeight = 320;  // ST7789 memory is 240x320: the panel shows its first 240 rows
  lcd.invert = true;
  if (!display.begin(lcd)) Serial.println("display init failed");

  drivers::Cst816Touch::Config tp;
  tp.sda = 11;
  tp.scl = 7;
  tp.rst = 6;
  tp.width = lcd.width;
  tp.height = lcd.height;
  // Sold with and without the touch layer (this unit has none: nothing answers on its I2C)
  if (touch.begin(tp)) {
    rotatedTouch.begin(&touch, tp.width, tp.height, quarterTurns, mirror);
    board.touch = &rotatedTouch;
  } else {
    Serial.println("no touch panel");
  }

  backlight.begin(42, true);  // inverted in the vendor firmware: low = on

  board.name = "Spotpear muma 1.54 (ESP32-S3)";
  board.display = &display;
  board.backlight = &backlight;
  board.devButtonPin = 0;  // BOOT
  return board;
}
