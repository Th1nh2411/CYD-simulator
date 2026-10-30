#include "drivers/common/xpt2046_bitbang.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdlib>

namespace drivers {

namespace {

// Keeps DCLK under the XPT2046's 2.5 MHz limit regardless of GPIO speed
inline void halfClock() {
  for (int i = 0; i < 16; i++) __asm__ __volatile__("nop");
}

// Average of the two closest of three samples
int bestTwoAvg(const int s[3]) {
  int da = std::abs(s[0] - s[1]), db = std::abs(s[0] - s[2]), dc = std::abs(s[1] - s[2]);
  if (da <= db && da <= dc) return (s[0] + s[1]) / 2;
  if (db <= dc) return (s[0] + s[2]) / 2;
  return (s[1] + s[2]) / 2;
}

int mapAxis(int raw, int rawMin, int rawMax, int size, bool invert) {
  int v = int(long(raw - rawMin) * (size - 1) / (rawMax - rawMin));
  v = std::max(0, std::min(size - 1, v));
  return invert ? size - 1 - v : v;
}

// Command bytes: start bit, channel, 12-bit mode, PD bits = 01 (ADC stays on)
constexpr uint8_t kZ1 = 0xB1, kZ2 = 0xC1, kChanA = 0x91, kChanB = 0xD1;
constexpr uint8_t kPowerDown = 0xD0;  // PD = 00 re-enables PENIRQ

}  // namespace

void Xpt2046Bitbang::begin(const Config& cfg) {
  cfg_ = cfg;
  pinMode(cfg.clk, OUTPUT);
  pinMode(cfg.mosi, OUTPUT);
  pinMode(cfg.cs, OUTPUT);
  pinMode(cfg.miso, INPUT);
  if (cfg.irq >= 0) pinMode(cfg.irq, INPUT);
  digitalWrite(cfg.cs, HIGH);
  digitalWrite(cfg.clk, LOW);

  digitalWrite(cfg.cs, LOW);
  transfer(kPowerDown);
  digitalWrite(cfg.cs, HIGH);
}

uint16_t Xpt2046Bitbang::transfer(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(cfg_.mosi, (cmd >> i) & 1);
    halfClock();
    digitalWrite(cfg_.clk, HIGH);
    halfClock();
    digitalWrite(cfg_.clk, LOW);
  }
  digitalWrite(cfg_.mosi, LOW);
  // 1 busy bit, 12 data bits MSB first, 3 trailing zeros
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(cfg_.clk, HIGH);
    halfClock();
    v = uint16_t((v << 1) | (digitalRead(cfg_.miso) & 1));
    digitalWrite(cfg_.clk, LOW);
    halfClock();
  }
  return v >> 3;
}

bool Xpt2046Bitbang::read(hal::TouchPoint& p) {
  if (cfg_.irq >= 0 && digitalRead(cfg_.irq) == HIGH) {
    pressedSamples_ = 0;
    return false;
  }

  digitalWrite(cfg_.cs, LOW);
  int z = int(transfer(kZ1)) + 4095 - int(transfer(kZ2));
  bool pressed = z >= cfg_.zMin;
  int a[3] = {}, b[3] = {};
  if (pressed) {
    transfer(kChanA);  // first conversion after switching drivers is noisy
    for (int i = 0; i < 3; i++) {
      a[i] = transfer(kChanA);
      b[i] = transfer(kChanB);
    }
  }
  transfer(kPowerDown);
  digitalWrite(cfg_.cs, HIGH);

  if (!pressed) {
    pressedSamples_ = 0;
    return false;
  }

  int ra = bestTwoAvg(a), rb = bestTwoAvg(b);
  // Near the last sample, or where the finger's last step carries on to (a
  // fast drag); a lone glitch is neither
  auto near = [&](int v, int last, int step) {
    return std::min(std::abs(v - last), std::abs(v - last - step)) < cfg_.maxJump;
  };
  bool steady = near(ra, prevA_, stepA_) && near(rb, prevB_, stepB_);
  stepA_ = ra - prevA_;
  stepB_ = rb - prevB_;
  prevA_ = ra;
  prevB_ = rb;
  if (pressedSamples_ < 255) pressedSamples_++;
  if (pressedSamples_ <= cfg_.settleSamples || !steady) return false;

  int rx = cfg_.swapXY ? rb : ra, ry = cfg_.swapXY ? ra : rb;
  p.x = int16_t(mapAxis(rx, cfg_.rawXMin, cfg_.rawXMax, cfg_.width, cfg_.invertX));
  p.y = int16_t(mapAxis(ry, cfg_.rawYMin, cfg_.rawYMax, cfg_.height, cfg_.invertY));
  p.pressure = uint16_t(z);
  return true;
}

}  // namespace drivers
