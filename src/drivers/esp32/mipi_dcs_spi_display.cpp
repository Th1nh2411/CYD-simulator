#include "drivers/esp32/mipi_dcs_spi_display.h"

#include <utility>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace drivers {

namespace {
const char* TAG = "lcd";

void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }

// MIPI-DCS standard commands
constexpr uint8_t kSwReset = 0x01, kSleepOut = 0x11, kInvOff = 0x20, kInvOn = 0x21;
constexpr uint8_t kDisplayOn = 0x29, kColSet = 0x2A, kRowSet = 0x2B, kMemWrite = 0x2C;
constexpr uint8_t kMadctl = 0x36, kColmod = 0x3A, kNop = 0x00;
constexpr uint8_t kMY = 0x80, kMX = 0x40, kMV = 0x20;

// The picture a quarter turn clockwise further: with rows and columns exchanged
// (MV) the column order flips as the exchange ends, otherwise the row order
// flips as it starts
uint8_t turnMadctl(uint8_t m) { return (m & kMV) ? uint8_t((m & ~kMV) ^ kMX) : uint8_t((m | kMV) ^ kMY); }
}  // namespace

bool MipiDcsSpiDisplay::begin(const Config& in) {
  cfg_ = in;
  for (int i = 0; i < (in.quarterTurns & 3); i++) {
    cfg_.madctl = turnMadctl(cfg_.madctl);
    std::swap(cfg_.width, cfg_.height);
  }
  // Host columns run along memory columns, or memory rows under MV
  if (in.mirror) cfg_.madctl ^= (cfg_.madctl & kMV) ? kMY : kMX;
  const Config& cfg = cfg_;
  // Host column / row offsets that land the window on the panel's memory: MV
  // sends host columns along memory rows, MX / MY count memory from the far end
  const bool mv = cfg.madctl & kMV, mx = cfg.madctl & kMX, my = cfg.madctl & kMY;
  const int panelW = mv ? cfg.height : cfg.width, panelH = mv ? cfg.width : cfg.height;  // memory orientation
  const int ramW = cfg.ramWidth ? cfg.ramWidth : panelW, ramH = cfg.ramHeight ? cfg.ramHeight : panelH;
  const int memColStart = mx ? ramW - panelW - cfg.memX : cfg.memX;
  const int memRowStart = my ? ramH - panelH - cfg.memY : cfg.memY;
  xOffset_ = mv ? memRowStart : memColStart;
  yOffset_ = mv ? memColStart : memRowStart;

  spi_bus_config_t bus = {};
  bus.mosi_io_num = cfg.mosi;
  bus.miso_io_num = -1;
  bus.sclk_io_num = cfg.sclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = cfg.width * cfg.maxStripLines * int(sizeof(gfx::Color));
  esp_err_t err = spi_bus_initialize(spi_host_device_t(cfg.spiHost), &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = cfg.cs;
  io.dc_gpio_num = cfg.dc;
  io.spi_mode = 0;
  io.pclk_hz = cfg.pclkHz;
  io.trans_queue_depth = 4;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  err = esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t(cfg.spiHost), &io, &io_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "panel io init failed: %s", esp_err_to_name(err));
    return false;
  }

  if (cfg.rst >= 0) {
    // gpio_config also selects the pad's GPIO function: set_direction alone
    // leaves pads that start as something else (the S3's JTAG pins 39..42) unmoved
    gpio_config_t rst = {};
    rst.pin_bit_mask = 1ULL << cfg.rst;
    rst.mode = GPIO_MODE_OUTPUT;
    gpio_config(&rst);
    gpio_set_level(gpio_num_t(cfg.rst), 0);
    sleepMs(10);
    gpio_set_level(gpio_num_t(cfg.rst), 1);
  } else {
    command(kSwReset);
  }
  sleepMs(150);

  if (cfg.panel) {
    for (uint8_t i = 0; i < cfg.panel->count; i++) {
      const PanelInitCmd& c = cfg.panel->cmds[i];
      command(c.cmd, c.data, c.len);
      if (c.delayMs) sleepMs(c.delayMs);
    }
  }
  const uint8_t colmod = 0x55;  // 16-bit RGB565
  command(kColmod, &colmod, 1);
  command(kMadctl, &cfg.madctl, 1);
  command(cfg.invert ? kInvOn : kInvOff);
  command(kSleepOut);
  sleepMs(120);
  command(kDisplayOn);
  sleepMs(20);

  ESP_LOGI(TAG, "%s %dx%d @ %u Hz", cfg.panel ? cfg.panel->name : "panel", cfg.width, cfg.height,
           cfg.pclkHz);
  return true;
}

gfx::Color* MipiDcsSpiDisplay::allocBuffer(size_t pixels) {
  return static_cast<gfx::Color*>(heap_caps_malloc(pixels * sizeof(gfx::Color), MALLOC_CAP_DMA));
}

void MipiDcsSpiDisplay::flush(const gfx::Rect& a, const gfx::Color* pixels) {
  int x0 = a.x + xOffset_, x1 = x0 + a.w - 1;
  int y0 = a.y + yOffset_, y1 = y0 + a.h - 1;
  const uint8_t col[4] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
  const uint8_t row[4] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
  // Parameter writes wait for the previous pixel DMA, then the new one is queued
  command(kColSet, col, 4);
  command(kRowSet, row, 4);
  esp_lcd_panel_io_tx_color(io_, kMemWrite, pixels, size_t(a.w) * a.h * sizeof(gfx::Color));
}

void MipiDcsSpiDisplay::waitIdle() { command(kNop); }

void MipiDcsSpiDisplay::command(uint8_t cmd, const uint8_t* data, size_t len) {
  esp_lcd_panel_io_tx_param(io_, cmd, data, len);
}

}  // namespace drivers
