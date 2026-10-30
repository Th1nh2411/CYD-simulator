#include "drivers/esp32/qmi8658.h"

#include <utility>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace drivers {

namespace {
const char* TAG = "qmi8658";
constexpr TickType_t kTimeout = pdMS_TO_TICKS(10);
constexpr uint8_t kWhoAmI = 0x00, kWhoAmIValue = 0x05;
constexpr uint8_t kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl3 = 0x04, kCtrl5 = 0x06, kCtrl7 = 0x08;
constexpr uint8_t kAccelXLow = 0x35, kReset = 0x60, kResetValue = 0xB0;
constexpr float kAccelPerG = 8192.0f;  // +-4 g
constexpr float kGyroPerDps = 64.0f;   // +-512 dps
}  // namespace

bool Qmi8658::begin(const Config& cfg) {
  cfg_ = cfg;
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

  // SA0 picks the address; boards wire it either way
  for (uint8_t a : {uint8_t(0x6B), uint8_t(0x6A)}) {
    address_ = a;
    uint8_t id = 0;
    if (readBytes(kWhoAmI, &id, 1) && id == kWhoAmIValue) break;
    address_ = 0;
  }
  if (!address_) {
    ESP_LOGW(TAG, "no QMI8658 at 0x6B or 0x6A");
    return false;
  }
  write(kReset, kResetValue);
  vTaskDelay(pdMS_TO_TICKS(20));
  // The low-pass filters cut at a share of the data rate: the narrowest (2.66%
  // of 125 Hz) smoothed a hand's shake (4..6 Hz) away. 13.37% of 250 Hz keeps it.
  bool ok = write(kCtrl1, 0x40) &&  // register address auto-increment, little endian
            write(kCtrl2, 0x15) &&  // accelerometer +-4 g, 250 Hz
            write(kCtrl3, 0x55) &&  // gyroscope +-512 dps, 250 Hz
            write(kCtrl5, 0x77) &&  // low-pass filters on at 13.37% of the rate (~33 Hz), both
            write(kCtrl7, 0x03);    // accelerometer and gyroscope on
  ESP_LOGI(TAG, "at 0x%02x %s", address_, ok ? "ready" : "did not take its setup");
  return ok;
}

bool Qmi8658::read(hal::ImuSample& s) {
  uint8_t d[12];
  if (!address_ || !readBytes(kAccelXLow, d, sizeof(d))) return false;
  auto v = [&](int i) { return float(int16_t(uint16_t(d[i]) | uint16_t(d[i + 1]) << 8)); };
  toScreen(v(0) / kAccelPerG, v(2) / kAccelPerG, v(4) / kAccelPerG, s.ax, s.ay, s.az);
  toScreen(v(6) / kGyroPerDps, v(8) / kGyroPerDps, v(10) / kGyroPerDps, s.gx, s.gy, s.gz);
  return true;
}

void Qmi8658::toScreen(float x, float y, float z, float& sx, float& sy, float& sz) const {
  if (cfg_.swapXY) std::swap(x, y);
  sx = cfg_.invertX ? -x : x;
  sy = cfg_.invertY ? -y : y;
  sz = cfg_.invertZ ? -z : z;
}

bool Qmi8658::write(uint8_t reg, uint8_t value) {
  const uint8_t b[2] = {reg, value};
  return i2c_master_write_to_device(i2c_port_t(cfg_.port), address_, b, 2, kTimeout) == ESP_OK;
}

bool Qmi8658::readBytes(uint8_t reg, uint8_t* out, int n) {
  return i2c_master_write_read_device(i2c_port_t(cfg_.port), address_, &reg, 1, out, size_t(n), kTimeout) == ESP_OK;
}

}  // namespace drivers
