#include "drivers/esp32/axs15231b_touch.h"

#include "driver/i2c.h"
#include "esp_log.h"

namespace drivers {

namespace {
const char* TAG = "axs15231b-touch";
constexpr uint8_t kAddress = 0x3B;
constexpr TickType_t kTimeout = pdMS_TO_TICKS(10);
// Read request for one touch point: 2 status bytes + 6 point bytes
constexpr uint8_t kReadPoint[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
}  // namespace

bool Axs15231bTouch::begin(const Config& cfg) {
  cfg_ = cfg;
  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = cfg.sda;
  conf.scl_io_num = cfg.scl;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = cfg.clockHz;
  esp_err_t err = i2c_param_config(i2c_port_t(cfg.port), &conf);
  if (err == ESP_OK) err = i2c_driver_install(i2c_port_t(cfg.port), I2C_MODE_MASTER, 0, 0, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c init failed: %s", esp_err_to_name(err));
    return false;
  }
  ready_ = true;
  return true;
}

bool Axs15231bTouch::read(hal::TouchPoint& p) {
  if (!ready_) return false;
  uint8_t d[8] = {};
  i2c_port_t port = i2c_port_t(cfg_.port);
  if (i2c_master_write_to_device(port, kAddress, kReadPoint, sizeof(kReadPoint), kTimeout) != ESP_OK ||
      i2c_master_read_from_device(port, kAddress, d, sizeof(d), kTimeout) != ESP_OK) {
    return false;
  }
  // d[1] = points, d[2] top bits = event (0 down, 2 held, 1 lifted), then 12-bit
  // x and y. Untouched it answers 0 points at first but filler (60 60 ..., ff ff ...)
  // once touched, so only exactly one point counts, and a lift is a release.
  int px = ((d[2] & 0x0F) << 8) | d[3], py = ((d[4] & 0x0F) << 8) | d[5];
  bool lifted = (d[2] >> 6) == 1;
  if (d[1] != 1 || lifted || px >= cfg_.panelWidth || py >= cfg_.panelHeight) return false;
  p.x = int16_t(py);
  p.y = int16_t(cfg_.panelWidth - 1 - px);
  p.pressure = 1;
  return true;
}

}  // namespace drivers
