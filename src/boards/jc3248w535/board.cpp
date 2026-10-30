// Guition JC3248W535 (JC3248W535EN): ESP32-S3 (16 MB flash, 8 MB octal PSRAM),
// 3.5" 320x480 AXS15231B panel on QSPI, shown landscape (480x320) or turned,
// with the controller's capacitive touch on I2C. Pins from the vendor BSP
// (refs/JC3248W535EN/src/esp_bsp.h).

#include <Arduino.h>

#include "drivers/common/rotated_touch.h"
#include "drivers/esp32/axs15231b_touch.h"
#include "drivers/esp32/i2s_speaker.h"
#include "drivers/esp32/ledc_backlight.h"
#include "drivers/esp32s3/axs15231b_qspi_display.h"
#include "drivers/panels/axs15231b.h"
#include "hal/board.h"

namespace {
drivers::Axs15231bQspiDisplay display;
drivers::Axs15231bTouch touch;
drivers::RotatedTouch rotatedTouch;
drivers::LedcBacklight backlight;
drivers::I2sSpeaker speaker;
hal::Board board;
}  // namespace

hal::Board& hal::initBoard(int quarterTurns, bool mirror) {
  drivers::Axs15231bQspiDisplay::Config lcd;
  lcd.init = drivers::panels::kAxs15231bJc3248w535;
  lcd.initCount = uint8_t(sizeof(drivers::panels::kAxs15231bJc3248w535) /
                          sizeof(drivers::panels::kAxs15231bJc3248w535[0]));
  lcd.spiHost = 1;  // SPI2
  lcd.sclk = 47;
  lcd.cs = 45;
  lcd.data[0] = 21;
  lcd.data[1] = 48;
  lcd.data[2] = 40;
  lcd.data[3] = 39;
  lcd.te = 38;
  lcd.pclkHz = 40000000;
  lcd.quarterTurns = quarterTurns;
  lcd.mirror = mirror;
  if (!display.begin(lcd)) Serial.println("display init failed");

  drivers::Axs15231bTouch::Config tp;  // reports landscape; turned with the screen below
  tp.sda = 4;
  tp.scl = 8;
  if (!touch.begin(tp)) Serial.println("touch init failed");
  rotatedTouch.begin(&touch, tp.panelHeight, tp.panelWidth, quarterTurns, mirror);

  backlight.begin(1);

  board.name = "JC3248W535 (ESP32-S3)";
  board.display = &display;
  board.touch = &rotatedTouch;
  // Measured on this unit (72 taps, rms 10.9 px), landscape: the touch spreads about 8% wider than
  // the picture horizontally. Recalibrate on screen with "touchcal" or the settings page.
  board.touchCalibration = {0.9239f, 18.95f, 0.9600f, -0.45f};
  board.backlight = &backlight;
  // NS4168 amplifier on the speaker header (schematic; its channel select is wired to the right
  // channel, the driver sends both). Not heard on this unit yet
  drivers::I2sSpeaker::Config amp;
  amp.bclk = 42;
  amp.lrclk = 2;
  amp.dout = 41;
  if (speaker.begin(amp)) board.speaker = &speaker;
  else Serial.println("speaker init failed");
  board.devButtonPin = 0;  // BOOT
  // 480 px wide strips of 24 lines are 2 x 23 KB of the ~100 KB of internal RAM. 12 lines return
  // 23 KB for ~1.7 ms more draw time and ~5 points of UI core (measured; 8 DMA rows as well
  // tripled the SPI wait of a card transition).
  board.stripLines = 12;
  // IP5306 charger. IO5 senses the battery through R26/R27; the schematic says 33k/100k but this
  // unit's R26 is marked 683 (68k), so (68k + 100k) / 100k. Not checked with a multimeter.
  board.batteryPin = 5;
  board.batteryScale = 1.68f;
  return board;
}
