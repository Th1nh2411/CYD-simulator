#pragma once

#include "hal/speaker.h"

namespace drivers {

// An I2S amplifier (e.g. NS4168, MAX98357) on the legacy IDF 4.4 I2S driver:
// 16-bit stereo frames with the mono sample in both channels, so it plays
// whichever channel the amplifier is wired to. Silence between sounds comes
// from the driver clearing its DMA buffers (tx_desc_auto_clear).
class I2sSpeaker : public hal::Speaker {
 public:
  struct Config {
    int bclk = -1, lrclk = -1, dout = -1;
    int sampleRate = 16000;
    int port = 0;
  };
  bool begin(const Config& c);
  void write(const int16_t* samples, size_t count) override;
  void idle() override;

 private:
  int port_ = -1;
  bool silent_ = true;
};

}  // namespace drivers
