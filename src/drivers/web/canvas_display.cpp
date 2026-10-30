#include "drivers/web/canvas_display.h"

#include <emscripten/emscripten.h>

#include <cstdlib>

namespace drivers {

namespace {

EM_JS(void, canvasSetup, (int w, int h), {
  const c = document.getElementById('screen');
  c.width = w;
  c.height = h;
  Module.screen = c.getContext('2d');
});

// Puts one rectangle of the frame (which lives in wasm memory) on the canvas
EM_JS(void, canvasBlit, (const uint32_t* frame, int w, int h, int x, int y, int aw, int ah), {
  const px = new Uint8ClampedArray(HEAPU8.buffer, frame, w * h * 4);
  Module.screen.putImageData(new ImageData(px, w, h), 0, 0, x, y, aw, ah);
});

// Panel-order RGB565 -> canvas RGBA (0xAABBGGRR in little-endian memory),
// widening each channel by repeating its top bits as the panel does
inline uint32_t toRgba(gfx::Color px) {
  uint32_t c = gfx::toRgb565(px);
  uint32_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  return 0xFF000000u | (b << 16) | (g << 8) | r;
}

}  // namespace

void CanvasDisplay::begin(int width, int height) {
  width_ = width;
  height_ = height;
  frame_.assign(size_t(width) * height, 0xFF000000u);
  canvasSetup(width, height);
}

gfx::Color* CanvasDisplay::allocBuffer(size_t pixels) {
  return static_cast<gfx::Color*>(malloc(pixels * sizeof(gfx::Color)));
}

void CanvasDisplay::flush(const gfx::Rect& area, const gfx::Color* pixels) {
  for (int y = 0; y < area.h; y++) {
    uint32_t* row = &frame_[size_t(area.y + y) * width_ + area.x];
    for (int x = 0; x < area.w; x++) row[x] = toRgba(*pixels++);
  }
  canvasBlit(frame_.data(), width_, height_, area.x, area.y, area.w, area.h);
}

}  // namespace drivers
