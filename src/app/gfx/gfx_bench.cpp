#include "app/gfx/gfx_bench.h"

#include <cmath>

#include "app/gfx/canvas.h"

namespace gfx::bench {

namespace {

constexpr int kSprite = 32;
uint8_t gSprite[kSprite * kSprite];

// Filled circle with a 3-color pattern: ~79% opaque, like a character sprite
void makeSprite() {
  for (int y = 0; y < kSprite; y++) {
    for (int x = 0; x < kSprite; x++) {
      int dx = 2 * x - kSprite + 1, dy = 2 * y - kSprite + 1;
      bool inside = dx * dx + dy * dy <= kSprite * kSprite;
      gSprite[y * kSprite + x] = inside ? uint8_t(1 + (x + y) % 3) : 0;
    }
  }
}

// Rotated and scaled indexed sprite, nearest neighbour, centered in a
// side x side buffer. Prototype to price runtime transforms; the renderer
// does not use it yet.
void drawRotated(Color* buf, int side, const IndexedImage& img, const Color* pal, float angle,
                 float scale) {
  float c = cosf(angle) / scale, s = sinf(angle) / scale;
  // Inverse map dest -> source in 16.16 fixed point
  int32_t ux = int32_t(c * 65536), vx = int32_t(-s * 65536);
  int32_t uy = int32_t(s * 65536), vy = int32_t(c * 65536);
  int32_t uc = int32_t(img.w) << 15, vc = int32_t(img.h) << 15;
  int half = side / 2;
  for (int y = 0; y < side; y++) {
    int32_t u = uc - half * ux + (y - half) * uy;
    int32_t v = vc - half * vx + (y - half) * vy;
    Color* row = buf + y * side;
    for (int x = 0; x < side; x++, u += ux, v += vx) {
      uint32_t sx = uint32_t(u >> 16), sy = uint32_t(v >> 16);  // negative wraps out of range
      if (sx < img.w && sy < img.h) {
        uint8_t idx = img.pixels[sy * img.w + sx];
        if (idx) row[x] = pal[idx];
      }
    }
  }
}

}  // namespace

int run(Color* buf, ClockUs now, Result* out, int maxResults) {
  makeSprite();
  const Color pal[4] = {0, rgb(40, 36, 64), rgb(120, 220, 160), rgb(255, 150, 170)};
  const IndexedImage img{kSprite, kSprite, gSprite};
  const Rect area(0, 0, kSide, kSide);
  const uint32_t px = uint32_t(kSide) * kSide;
  Canvas c;
  c.reset(buf, area);

  int n = 0;
  auto add = [&](const char* name, uint32_t pixels, uint32_t us) {
    if (n < maxResults) out[n++] = Result{name, pixels, us};
  };

  uint32_t t0 = now();
  for (int i = 0; i < 40; i++) c.fillRect(area, Color(i * 2113));
  add("fill solid", px * 40, now() - t0);

  t0 = now();
  for (int i = 0; i < 40; i++) c.fillGradientV(area, rgb(28, 32, 72), rgb(92, 120, 196));
  add("gradient", px * 40, now() - t0);

  t0 = now();
  for (int i = 0; i < 40; i++) {
    for (int k = 0; k < 16; k++) c.drawImage(img, pal, (k % 4) * kSprite, (k / 4) * kSprite, 1, false);
  }
  add("sprite 1x", px * 40, now() - t0);

  t0 = now();
  for (int i = 0; i < 40; i++) c.drawImage(img, pal, 0, 0, kSide / kSprite, (i & 1) != 0);
  add("sprite 4x", px * 40, now() - t0);

  t0 = now();
  const Color white = rgb(255, 255, 255);
  for (int i = 0; i < 20; i++) {
    for (uint32_t p = 0; p < px; p++) buf[p] = mix(buf[p], white, 16);
  }
  add("alpha blend 50%", px * 20, now() - t0);

  t0 = now();
  for (int i = 0; i < 20; i++) c.darkenEllipse(area, 12);
  add("shadow (darken)", uint32_t(float(px) * 0.785f) * 20, now() - t0);

  const char* line = "Hello CYD 0123456789";  // 20 glyphs, 119 x 8 px at 1x
  t0 = now();
  for (int i = 0; i < 20; i++) {
    for (int l = 0; l < kSide / 8; l++) c.drawText(line, 0, l * 8, white, 1);
  }
  add("text 1x (glyph box)", uint32_t(Canvas::textWidth(line, 1)) * kSide * 20, now() - t0);

  t0 = now();
  for (int i = 0; i < 20; i++) drawRotated(buf, kSide, img, pal, float(i) * 0.3f, 3.0f);
  add("rotate+scale sprite", px * 20, now() - t0);

  return n;
}

}  // namespace gfx::bench
