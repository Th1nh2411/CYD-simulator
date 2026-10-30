#include "app/gfx/canvas.h"

#include <cmath>
#include <cstring>

#include "app/core/utf8.h"
#include "app/gfx/font5x7.h"

namespace gfx {

namespace {

typedef uint32_t __attribute__((__may_alias__)) Word;

// Fills n pixels using 32-bit stores where possible
inline void fillSpan(Color* p, int n, Color c) {
  if (n <= 0) return;
  if (reinterpret_cast<uintptr_t>(p) & 2) {
    *p++ = c;
    n--;
  }
  Word c2 = Word(c) | (Word(c) << 16);
  Word* q = reinterpret_cast<Word*>(p);
  for (int i = n >> 1; i > 0; i--) *q++ = c2;
  if (n & 1) *reinterpret_cast<Color*>(q) = c;
}

}  // namespace

void Canvas::span(int y, int x0, int x1, Color c) {
  if (y < area_.y || y >= area_.bottom()) return;
  x0 = std::max<int>(x0, area_.x);
  x1 = std::min<int>(x1, area_.right());
  if (x1 > x0) fillSpan(at(x0, y), x1 - x0, c);
}

void Canvas::fillRect(const Rect& r, Color c) {
  Rect v = r.intersect(area_);
  if (v.empty()) return;
  for (int y = v.y; y < v.bottom(); y++) fillSpan(at(v.x, y), v.w, c);
}

void Canvas::fillRoundRect(const Rect& r, int radius, Color c) {
  Rect v = r.intersect(area_);
  if (v.empty()) return;
  radius = std::min({radius, r.w / 2, r.h / 2});
  for (int y = v.y; y < v.bottom(); y++) {
    int dy = 0;
    if (y < r.y + radius) dy = r.y + radius - y;
    else if (y >= r.bottom() - radius) dy = y - (r.bottom() - radius - 1);
    int inset = 0;
    if (dy > 0) {
      float d = dy - 0.5f;
      inset = radius - int(sqrtf(float(radius * radius) - d * d) + 0.5f);
    }
    span(y, r.x + inset, r.right() - inset, c);
  }
}

void Canvas::fillGradientV(const Rect& r, Color top, Color bottom) {
  Rect v = r.intersect(area_);
  if (v.empty()) return;
  int den = std::max(1, r.h - 1);
  for (int y = v.y; y < v.bottom(); y++) {
    uint32_t t = uint32_t((y - r.y) * 32 + den / 2) / den;
    fillSpan(at(v.x, y), v.w, mix(top, bottom, t));
  }
}

void Canvas::darkenEllipse(const Rect& r, uint32_t amount) {
  Rect v = r.intersect(area_);
  if (v.empty()) return;
  float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
  float rx = r.w * 0.5f, ry = r.h * 0.5f;
  for (int y = v.y; y < v.bottom(); y++) {
    float dy = (y + 0.5f - cy) / ry;
    float k = 1.0f - dy * dy;
    if (k <= 0) continue;
    float half = rx * sqrtf(k);
    int x0 = std::max<int>(v.x, int(cx - half + 0.5f));
    int x1 = std::min<int>(v.right(), int(cx + half + 0.5f));
    Color* p = at(x0, y);
    for (int x = x0; x < x1; x++, p++) *p = mix(*p, 0, amount);
  }
}

void Canvas::drawImage(const IndexedImage& img, const Color* palette, int x, int y, int scale,
                       bool flipX) {
  Rect dst(x, y, img.w * scale, img.h * scale);
  Rect v = dst.intersect(area_);
  if (v.empty()) return;

  int firstDx = v.x - x;
  for (int yy = v.y; yy < v.bottom(); yy++) {
    const uint8_t* src = img.pixels + ((yy - y) / scale) * img.w;
    Color* p = at(v.x, yy);
    int sx = firstDx / scale;
    int run = scale - firstDx % scale;  // first source pixel may be partly clipped
    for (int i = 0; i < v.w; sx++, run = scale) {
      if (run > v.w - i) run = v.w - i;
      uint8_t idx = src[flipX ? img.w - 1 - sx : sx];
      if (idx) {
        Color c = palette[idx];
        for (int k = 0; k < run; k++) p[i + k] = c;
      }
      i += run;
    }
  }
}

void Canvas::drawText(const char* text, int x, int y, Color c, int scale) {
  if (y >= area_.bottom() || y + kFontH * scale <= area_.y) return;
  for (const char* s = text; *s; s++, x += kFontAdvance * scale) {
    if (x >= area_.right()) break;
    if (x + kFontW * scale <= area_.x) continue;
    char ch = *s;
    if (ch < kFontFirst || ch > kFontLast) ch = '?';
    const uint8_t* glyph = &kFont5x7[(ch - kFontFirst) * kFontW];
    for (int col = 0; col < kFontW; col++) {
      uint8_t bits = glyph[col];
      for (int row = 0; bits; row++, bits >>= 1) {
        if (bits & 1) fillRect(Rect(x + col * scale, y + row * scale, scale, scale), c);
      }
    }
  }
}

namespace {
const Glyph* glyphOrBox(const Font& f, uint32_t code) {
  const Glyph* g = f.find(code);
  return g ? g : f.find(0x25A1);  // white square
}
}  // namespace

int Canvas::drawText(const Font& f, const char* utf8, int x, int y, Color c, int scale, int maxChars) {
  if (y - f.above * scale >= area_.bottom() || y + f.height * scale <= area_.y) {
    return x + textWidth(f, utf8, scale);
  }
  for (const char* s = utf8; *s && maxChars > 0; maxChars--) {
    const Glyph* g = glyphOrBox(f, core::nextCodePoint(s));
    if (!g) continue;
    int gx = x + g->x * scale, gy = y + g->y * scale;
    x += g->advance * scale;
    if (gx >= area_.right() || gx + g->w * scale <= area_.x) continue;
    uint32_t bit = g->offset;
    for (int row = 0; row < g->h; row++) {
      int py = gy + row * scale;
      if (py + scale <= area_.y || py >= area_.bottom()) {
        bit += g->w;
        continue;
      }
      for (int col = 0; col < g->w; col++, bit++) {
        if (!(f.bits[bit >> 3] & (0x80 >> (bit & 7)))) continue;
        int px = gx + col * scale;
        if (scale == 1) {
          if (px >= area_.x && px < area_.right() && py >= area_.y) *at(px, py) = c;
        } else {
          fillRect(Rect(px, py, scale, scale), c);
        }
      }
    }
  }
  return x;
}

int Canvas::textWidth(const Font& f, const char* utf8, int scale) {
  int w = 0;
  for (const char* s = utf8; *s;) {
    const Glyph* g = glyphOrBox(f, core::nextCodePoint(s));
    if (g) w += g->advance;
  }
  return w * scale;
}

int Canvas::textWidth(const char* text, int scale) {
  int n = int(strlen(text));
  return n ? (n * kFontAdvance - 1) * scale : 0;
}

int Canvas::textHeight(int scale) { return kFontH * scale; }

}  // namespace gfx
