#pragma once

// ESP32-2432S028R (Cheap Yellow Display) pin map
// Display pins (HSPI 14/13/12, CS 15, DC 2, BL 21) are set via build_flags in platformio.ini

// Touch controller XPT2046 (on its own SPI bus, VSPI)
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// RGB LED on the back (active LOW)
#define CYD_LED_RED   4
#define CYD_LED_GREEN 16
#define CYD_LED_BLUE  17

// Light sensor (LDR), analog input
#define CYD_LDR 34

// Speaker amplifier output (DAC)
#define CYD_SPEAKER 26

// MicroSD card slot (VSPI default pins)
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5

// BOOT button
#define CYD_BOOT_BTN 0
