#pragma once

#include <cstdint>
#include <vector>

#include "hal/display.h"

namespace drivers {

// hal::Display for the browser simulator: flushed pixels go, converted to
// RGBA, onto the page's <canvas id="screen"> (Emscripten).
class CanvasDisplay : public hal::Display {
 public:
  void begin(int width, int height);

  int width() const override { return width_; }
  int height() const override { return height_; }
  gfx::Color* allocBuffer(size_t pixels) override;
  void flush(const gfx::Rect& area, const gfx::Color* pixels) override;
  void waitIdle() override {}  // flush() is synchronous

 private:
  int width_ = 0, height_ = 0;
  std::vector<uint32_t> frame_;  // whole screen in canvas byte order (RGBA)
};

}  // namespace drivers
