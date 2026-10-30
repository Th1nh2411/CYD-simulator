// ESP32-2432S028R "CYD2USB" (micro-USB + USB-C): ESP32-D0WD-V3, 4MB flash,
// 2.8" 320x240 ILI9341-compatible panel, XPT2046 resistive touch.

#include <Arduino.h>

#include "drivers/common/rotated_touch.h"
#include "drivers/common/xpt2046_bitbang.h"
#include "drivers/esp32/ledc_backlight.h"
#include "drivers/esp32/mipi_dcs_spi_display.h"
#include "drivers/panels/ili9341.h"
#include "hal/board.h"

namespace {
drivers::MipiDcsSpiDisplay display;
drivers::Xpt2046Bitbang touch;
drivers::RotatedTouch rotatedTouch;
drivers::LedcBacklight backlight;
hal::Board board;
}  // namespace

hal::Board& hal::initBoard(int quarterTurns, bool mirror) {
  drivers::MipiDcsSpiDisplay::Config lcd;
  lcd.panel = &drivers::panels::kIli9341;
  lcd.spiHost = 1;  // SPI2/HSPI on its native IO_MUX pins (capable of 80 MHz)
  lcd.sclk = 14;
  lcd.mosi = 13;
  lcd.cs = 15;
  lcd.dc = 2;
  lcd.rst = -1;  // tied to EN
  lcd.pclkHz = 40000000;
  lcd.width = 320;
  lcd.height = 240;
  lcd.madctl = 0x28;  // MV (landscape) | BGR
  lcd.quarterTurns = quarterTurns;
  lcd.mirror = mirror;
  lcd.invert = true;  // this panel batch shows inverted colors otherwise
  if (!display.begin(lcd)) Serial.println("display init failed");

  drivers::Xpt2046Bitbang::Config tp;
  tp.clk = 25;
  tp.mosi = 32;
  tp.miso = 39;
  tp.cs = 33;
  tp.irq = 36;
  tp.width = lcd.width;  // landscape; turned with the screen below
  tp.height = lcd.height;
  touch.begin(tp);
  rotatedTouch.begin(&touch, lcd.width, lcd.height, quarterTurns, mirror);

  backlight.begin(21);

  board.name = "CYD2USB (ESP32-2432S028R)";
  board.display = &display;
  board.touch = &rotatedTouch;
  board.backlight = &backlight;
  board.devButtonPin = 0;  // BOOT
  return board;
}
