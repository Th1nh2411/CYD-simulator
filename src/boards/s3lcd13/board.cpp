// SpotPear ESP32-S3-LCD-1.3 (sold as a prism "mini TV"): ESP32-S3R8 (16 MB
// flash, 8 MB octal PSRAM), 1.3" 240x240 ST7789 on SPI, no touch, QMI8658 IMU
// (I2C SDA 47 / SCL 48), micro SD, WS2812 LED, CH343 USB-UART.
// LCD pins from the SpotPear wiki, the rest from its schematic (ESP32S3_1.3inch).

#include <Arduino.h>

#include "drivers/common/rotated_imu.h"
#include "drivers/esp32/ledc_backlight.h"
#include "drivers/esp32/mipi_dcs_spi_display.h"
#include "drivers/esp32/qmi8658.h"
#include "drivers/panels/st7789.h"
#include "hal/board.h"

namespace {
drivers::MipiDcsSpiDisplay display;
drivers::LedcBacklight backlight;
drivers::Qmi8658 imu;
drivers::RotatedImu rotatedImu;
hal::Board board;
}  // namespace

hal::Board& hal::initBoard(int quarterTurns, bool mirror) {
  drivers::MipiDcsSpiDisplay::Config lcd;
  lcd.panel = &drivers::panels::kSt7789;
  lcd.spiHost = 1;  // SPI2, pins through the GPIO matrix
  lcd.sclk = 40;
  lcd.mosi = 41;
  lcd.cs = 39;
  lcd.dc = 38;
  lcd.rst = 42;
  lcd.pclkHz = 40000000;
  lcd.width = 240;
  lcd.height = 240;
  lcd.madctl = 0x00;  // RGB, no rotation
  lcd.quarterTurns = quarterTurns;
  lcd.mirror = mirror;  // seen through the prism: "좌우 반전" in the settings
  // ST7789 memory is 240x320; the panel shows its first 240 rows (checked: an
  // offset of 80, as in the vendor demo's own setup, left the top third stale)
  lcd.ramHeight = 320;
  lcd.invert = true;
  if (!display.begin(lcd)) Serial.println("display init failed");

  // BL_PWM drives the backlight through an NPN: high = on. GPIO20 is the
  // S3's USB D+, free here since USB goes through the CH343.
  backlight.begin(20);

  drivers::Qmi8658::Config motion;
  motion.sda = 47;
  motion.scl = 48;
  // Measured on the device (seen through the prism, turned 180 and mirrored):
  // the chip's x and y run against the screen's
  motion.invertX = true;
  motion.invertY = true;
  if (imu.begin(motion)) {
    rotatedImu.begin(&imu, quarterTurns, mirror);
    board.imu = &rotatedImu;
  } else {
    Serial.println("no IMU");
  }

  board.name = "SpotPear ESP32-S3-LCD-1.3";
  board.display = &display;
  board.backlight = &backlight;
  board.devButtonPin = 0;  // BOOT
  return board;
}
