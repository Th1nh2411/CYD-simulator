#include "drivers/esp32/cst816_touch.h"

#include <utility>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace drivers {

namespace {
const char* TAG = "cst816-touch";
constexpr uint8_t kAddress = 0x15;
constexpr TickType_t kTimeout = pdMS_TO_TICKS(10);
constexpr uint8_t kRegPoint = 0x02, kRegChipId = 0xA7;
}  // namespace

bool Cst816Touch::begin(const Config& cfg) {
  cfg_ = cfg;
  if (cfg.rst >= 0) {
    gpio_set_direction(gpio_num_t(cfg.rst), GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num_t(cfg.rst), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gpio_num_t(cfg.rst), 1);
    vTaskDelay(pdMS_TO_TICKS(60));  // the controller answers about 50 ms after reset
  }

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = cfg.sda;
  conf.scl_io_num = cfg.scl;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = cfg.clockHz;
  i2c_port_t port = i2c_port_t(cfg.port);
  esp_err_t err = i2c_param_config(port, &conf);
  if (err == ESP_OK) err = i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c init failed: %s", esp_err_to_name(err));
    return false;
  }

  // Boards are sold with and without the touch layer: no answer = no touch
  uint8_t id = 0;
  if (i2c_master_write_read_device(port, kAddress, &kRegChipId, 1, &id, 1, kTimeout) != ESP_OK) {
    ESP_LOGW(TAG, "no answer at 0x%02x", kAddress);
    return false;
  }
  ESP_LOGI(TAG, "chip id 0x%02x", id);
  ready_ = true;
  return true;
}

bool Cst816Touch::read(hal::TouchPoint& p) {
  if (!ready_) return false;
  uint8_t d[5] = {};
  if (i2c_master_write_read_device(i2c_port_t(cfg_.port), kAddress, &kRegPoint, 1, d, sizeof(d), kTimeout) !=
      ESP_OK) {
    return false;
  }
  // d[0] = points, then 12-bit x and y; the top bits of x's high byte are the
  // event (0 down, 1 lifted, 2 held), so a lift is a release
  int x = ((d[1] & 0x0F) << 8) | d[2], y = ((d[3] & 0x0F) << 8) | d[4];
  bool lifted = (d[1] >> 6) == 1;
  if (cfg_.swapXY) std::swap(x, y);
  if ((d[0] & 0x0F) == 0 || lifted || x >= cfg_.width || y >= cfg_.height) return false;
  p.x = int16_t(cfg_.mirrorX ? cfg_.width - 1 - x : x);
  p.y = int16_t(cfg_.mirrorY ? cfg_.height - 1 - y : y);
  p.pressure = 1;
  return true;
}

}  // namespace drivers
