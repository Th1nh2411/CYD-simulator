#pragma once

#include <cstdint>

#include "drivers/panels/panel_desc.h"
#include "esp_lcd_panel_io.h"
#include "hal/display.h"

namespace drivers {

// SPI display driver for any MIPI-DCS panel on ESP32 (ESP-IDF esp_lcd).
// Pixel transfers are queued to DMA: flush() returns as soon as the transfer
// starts and only waits when the previous one is still running.
class MipiDcsSpiDisplay : public hal::Display {
 public:
  struct Config {
    const PanelDesc* panel = nullptr;
    int spiHost = 1;  // 1 = SPI2_HOST (HSPI), 2 = SPI3_HOST (VSPI)
    int sclk = -1, mosi = -1, cs = -1, dc = -1, rst = -1;
    uint32_t pclkHz = 40000000;
    int16_t width = 0, height = 0;  // as madctl shows the panel
    uint8_t madctl = 0;  // rotation / mirroring / BGR bits
    // The picture turned a quarter clockwise this many more times (MADCTL does
    // it; width and height swap on odd turns): screen (x, y) -> (h-1-y, x) a turn
    int quarterTurns = 0;
    bool mirror = false;  // then left to right (a prism shows the picture mirrored)
    // Panels smaller than the controller's memory: where the panel starts in it
    // and how big it is, in the controller's own orientation (MADCTL 0); a size
    // of 0 means the panel's own
    int16_t memX = 0, memY = 0, ramWidth = 0, ramHeight = 0;
    bool invert = false;
    int maxStripLines = 40;  // largest flush the SPI DMA has to carry, in full-width lines
  };

  bool begin(const Config& cfg);

  int width() const override { return cfg_.width; }
  int height() const override { return cfg_.height; }
  gfx::Color* allocBuffer(size_t pixels) override;
  void flush(const gfx::Rect& area, const gfx::Color* pixels) override;
  void waitIdle() override;

 private:
  void command(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0);

  Config cfg_;  // turned: width, height and madctl as shown
  int xOffset_ = 0, yOffset_ = 0;  // window offsets that land on the panel
  esp_lcd_panel_io_handle_t io_ = nullptr;
};

}  // namespace drivers
