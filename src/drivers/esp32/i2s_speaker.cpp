#include "drivers/esp32/i2s_speaker.h"

#include <driver/gpio.h>
#include <driver/i2s.h>

#include <algorithm>

namespace drivers {

bool I2sSpeaker::begin(const Config& c) {
  i2s_config_t cfg = {};
  cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = uint32_t(c.sampleRate);
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;  // frames: 4 x 16 ms queued at 16 kHz
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  const i2s_port_t port = i2s_port_t(c.port);
  if (i2s_driver_install(port, &cfg, 0, nullptr) != ESP_OK) return false;
  // GPIO39..42 start as JTAG on the S3: back to plain GPIO before the matrix takes them
  for (int pin : {c.bclk, c.lrclk, c.dout}) gpio_reset_pin(gpio_num_t(pin));
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = c.bclk;
  pins.ws_io_num = c.lrclk;
  pins.data_out_num = c.dout;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(port, &pins) != ESP_OK) {
    i2s_driver_uninstall(port);
    return false;
  }
  i2s_zero_dma_buffer(port);
  port_ = c.port;
  return true;
}

void I2sSpeaker::write(const int16_t* samples, size_t count) {
  if (port_ < 0) return;
  silent_ = false;
  int16_t frames[2 * 128];
  while (count) {
    const size_t n = std::min<size_t>(count, 128);
    for (size_t i = 0; i < n; i++) frames[2 * i] = frames[2 * i + 1] = samples[i];
    size_t written = 0;
    i2s_write(i2s_port_t(port_), frames, n * 4, &written, portMAX_DELAY);
    samples += n;
    count -= n;
  }
}

void I2sSpeaker::idle() {
  if (port_ < 0 || silent_) return;
  silent_ = true;
  i2s_zero_dma_buffer(i2s_port_t(port_));
}

}  // namespace drivers
