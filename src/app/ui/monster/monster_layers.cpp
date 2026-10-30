#include "app/ui/monster/monster_layers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/core/utf8.h"
#include "app/gfx/font.h"
#include "app/ui/monster/monster_sprites.h"

namespace ui::monster {

namespace {

const gfx::Color kInk = gfx::rgb(64, 64, 72);
const gfx::Color kInkShadow = gfx::rgb(208, 208, 200);
const gfx::Color kTitleInk = gfx::rgb(208, 80, 40);  // message titles, the "next" arrow
const gfx::Color kFrame = gfx::rgb(48, 56, 72);
const gfx::Color kFrameInner = gfx::rgb(104, 120, 160);
const gfx::Color kPaper = gfx::rgb(248, 248, 240);
const gfx::Color kButton = gfx::rgb(232, 232, 240);
const gfx::Color kButtonPressed = gfx::rgb(255, 216, 96);
const gfx::Color kPlate = gfx::rgb(248, 240, 216);
const gfx::Color kHpBack = gfx::rgb(64, 72, 80);
const gfx::Color kHpLabel = gfx::rgb(240, 160, 32);
const gfx::Color kSkyTop = gfx::rgb(248, 240, 208);
const gfx::Color kSkyBottom = gfx::rgb(216, 238, 176);
const gfx::Color kWipe = gfx::rgb(16, 16, 24);
const gfx::Color kWhite = gfx::rgb(255, 255, 255);
constexpr int kLineH = 16;   // text line pitch, px
constexpr int kBandH = 16;   // transition bar height, px
const char* const kMoreArrow = "\xE2\x96\xBC";  // ▼
const char* const kCursor = "\xE2\x96\xB6";     // ▶

uint32_t xorshift(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

// Solid ellipse filling r, pixel-art style (no anti-aliasing)
void fillEllipse(gfx::Canvas& c, const gfx::Rect& r, gfx::Color color) {
  const gfx::Rect v = r.intersect(c.area());
  if (v.empty()) return;
  const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, rx = r.w * 0.5f, ry = r.h * 0.5f;
  for (int y = v.y; y < v.bottom(); y++) {
    float dy = (y + 0.5f - cy) / ry;
    float k = 1.0f - dy * dy;
    if (k <= 0) continue;
    int half = int(rx * sqrtf(k) + 0.5f);
    c.fillRect(gfx::Rect(int(cx) - half, y, 2 * half, 1), color);
  }
}

int countChars(const char* s) {
  int n = 0;
  while (*s) {
    core::nextCodePoint(s);
    n++;
  }
  return n;
}

}  // namespace

// --- SceneLayer ---

void SceneLayer::configure(const gfx::Rect& screen, int tileScale, const gfx::Color* tilePalette, uint32_t seed) {
  scale_ = tileScale;
  palette_ = tilePalette;
  const int tile = 16 * scale_;
  cols_ = std::min(kMaxCols, (screen.w + tile - 1) / tile);
  rows_ = std::min(kMaxRows, (screen.h + tile - 1) / tile);
  uint32_t rng = seed ? seed : 1;
  for (int y = 0; y < rows_; y++) {
    for (int x = 0; x < cols_; x++) {
      uint32_t r = xorshift(rng) % 100;
      tiles_[y][x] = r < 8 ? kTileFlowers : (r < 50 ? kTileGrass2 : kTileGrass);
    }
  }
  // A couple of tall-grass patches, as in any good field
  for (int patch = 0; patch < 2; patch++) {
    int w = 2 + int(xorshift(rng) % 2), h = 1 + int(xorshift(rng) % 2);
    int x0 = int(xorshift(rng) % uint32_t(std::max(1, cols_ - w))), y0 = int(xorshift(rng) % uint32_t(std::max(1, rows_ - h)));
    for (int y = y0; y < y0 + h; y++) {
      for (int x = x0; x < x0 + w; x++) tiles_[y][x] = kTileTall;
    }
  }
  if (bounds() == screen) invalidate();
  else setBounds(screen);
}

void SceneLayer::setMode(Mode m) {
  if (m == mode_) return;
  mode_ = m;
  invalidate();
}

void SceneLayer::setPlatforms(const gfx::Rect& foe, const gfx::Rect& mine) {
  foe_ = foe;
  mine_ = mine;
  if (mode_ == Mode::Battle) invalidate();
}

void SceneLayer::draw(gfx::Canvas& c) const {
  if (mode_ == Mode::Field) drawField(c);
  else drawBattle(c);
}

void SceneLayer::drawField(gfx::Canvas& c) const {
  const gfx::Rect& a = c.area();
  const int tile = 16 * scale_;
  int x0 = std::max(0, a.x / tile), x1 = std::min(cols_, (a.right() + tile - 1) / tile);
  int y0 = std::max(0, a.y / tile), y1 = std::min(rows_, (a.bottom() + tile - 1) / tile);
  for (int ty = y0; ty < y1; ty++) {
    for (int tx = x0; tx < x1; tx++) c.drawImage(kTiles[tiles_[ty][tx]], palette_, tx * tile, ty * tile, scale_, false);
  }
}

void SceneLayer::drawBattle(gfx::Canvas& c) const {
  c.fillGradientV(bounds(), kSkyTop, kSkyBottom);
  // Platforms: a darker rim under a lighter top, in the field's grass colors
  for (const gfx::Rect& p : {foe_, mine_}) {
    fillEllipse(c, p, palette_[2]);
    fillEllipse(c, gfx::Rect(p.x + 4, p.y, p.w - 8, p.h - 4), palette_[3]);
  }
}

// --- SpriteLayer ---

gfx::Rect SpriteLayer::shadowRect() const {
  int w = img_->w * scale_, h = img_->h * scale_;
  int sw = w * 7 / 10 - air_ / 2;
  return gfx::Rect(x_ + (w - sw) / 2, y_ + air_ + h - 2 * scale_, sw, 3 * scale_);
}

void SpriteLayer::show(const gfx::IndexedImage& img, const gfx::Color* palette, int x, int y, int scale,
                       bool flipX, bool shadow, int air) {
  bool same = visible() && img_ == &img && palette_ == palette && x_ == x && y_ == y && scale_ == scale &&
              flip_ == flipX && shadow_ == shadow && air_ == air;
  if (same) return;
  img_ = &img;
  palette_ = palette;
  x_ = x;
  y_ = y;
  scale_ = scale;
  flip_ = flipX;
  shadow_ = shadow;
  air_ = air;
  setVisible(true);
  gfx::Rect b(x, y, img.w * scale, img.h * scale);
  if (shadow) b = b.unite(shadowRect());
  if (b == bounds()) invalidate();
  else setBounds(b);
}

void SpriteLayer::draw(gfx::Canvas& c) const {
  if (!img_) return;
  if (shadow_) c.darkenEllipse(shadowRect(), 10);
  c.drawImage(*img_, palette_, x_, y_, scale_, flip_);
}

// --- BoxLayer ---

void BoxLayer::place(const gfx::Rect& r) {
  if (r == bounds()) return;
  bool resized = r.w != bounds().w || r.h != bounds().h;
  setBounds(r);
  if (resized) rewrap();
}

gfx::Rect BoxLayer::textArea() const {
  const gfx::Rect& b = bounds();
  int right = optionCount_ ? optionRect(0).x - 6 : b.right() - 10;
  return gfx::Rect(b.x + 10, b.y + 7, right - (b.x + 10), b.h - 12);
}

gfx::Rect BoxLayer::optionRect(int i) const {
  const gfx::Rect& b = bounds();
  int w = b.w * 37 / 100;
  int h = std::min(24, (b.h - 12 - (optionCount_ - 1) * 3) / std::max(1, optionCount_));
  int top = b.y + (b.h - (optionCount_ * h + (optionCount_ - 1) * 3)) / 2;
  return gfx::Rect(b.right() - 7 - w, top + i * (h + 3), w, h);
}

gfx::Rect BoxLayer::lineRect(int line) const {
  gfx::Rect t = textArea();
  return gfx::Rect(t.x, t.y + line * kLineH, t.w + 2, kLineH + 1);
}

void BoxLayer::rewrap() {
  const gfx::Font& f = gfx::kFontKo11;
  const int width = textArea().w;
  lineCount_ = 0;
  titled_ = title_[0] != '\0';
  if (titled_) lineCount_ = gfx::wrapText(f, title_, width, lines_[0], kLineBytes, 1);
  lineCount_ += gfx::wrapText(f, body_, width, lines_[lineCount_], kLineBytes, kMaxLines - lineCount_);
  length_ = 0;
  for (int i = 0; i < lineCount_; i++) {
    lineLength_[i] = countChars(lines_[i]);
    length_ += lineLength_[i];
  }
  typed_ = std::min(typed_, length_);
  invalidate();
}

void BoxLayer::setMessage(const char* title, const char* body) {
  core::copyUtf8(title_, sizeof(title_), title);
  core::copyUtf8(body_, sizeof(body_), body);
  typed_ = 0;
  more_ = false;
  rewrap();
}

void BoxLayer::setTyped(int chars) {
  chars = std::max(0, std::min(chars, length_));
  if (chars == typed_) return;
  // Redraw only the lines that gained or lost characters
  int lo = std::min(chars, typed_), hi = std::max(chars, typed_), start = 0;
  for (int i = 0; i < lineCount_; i++) {
    int end = start + lineLength_[i];
    if (end > lo && start < hi) invalidate(lineRect(i));
    start = end;
  }
  typed_ = chars;
}

void BoxLayer::setMore(bool on) {
  if (on == more_) return;
  more_ = on;
  gfx::Rect t = textArea();
  invalidate(gfx::Rect(t.right() - 14, t.bottom() - kLineH, 16, kLineH + 2));
}

void BoxLayer::setOptions(const char (*options)[25], int count) {
  count = std::max(0, std::min(count, engine::kMaxOptions));
  bool same = count == optionCount_;
  for (int i = 0; same && i < count; i++) same = strcmp(options_[i], options[i]) == 0;
  if (same) return;
  optionCount_ = count;
  for (int i = 0; i < count; i++) core::copyUtf8(options_[i], sizeof(options_[i]), options[i]);
  pressed_ = -1;
  rewrap();  // the text area changed width
}

void BoxLayer::setPressed(int option) {
  if (option == pressed_) return;
  if (pressed_ >= 0 && pressed_ < optionCount_) invalidate(optionRect(pressed_));
  pressed_ = option;
  if (pressed_ >= 0 && pressed_ < optionCount_) invalidate(optionRect(pressed_));
}

int BoxLayer::optionAt(int x, int y) const {
  if (!visible()) return -1;
  for (int i = 0; i < optionCount_; i++) {
    if (optionRect(i).inflate(2).contains(x, y)) return i;
  }
  return -1;
}

void BoxLayer::draw(gfx::Canvas& c) const {
  const gfx::Font& f = gfx::kFontKo11;
  const gfx::Rect& b = bounds();
  c.fillRoundRect(b, 6, kFrame);
  c.fillRoundRect(b.inflate(-2), 5, kFrameInner);
  c.fillRoundRect(b.inflate(-4), 4, kPaper);

  gfx::Rect t = textArea();
  int left = typed_;
  for (int i = 0; i < lineCount_ && left > 0; i++) {
    int n = std::min(left, lineLength_[i]);
    int y = t.y + i * kLineH;
    c.drawText(f, lines_[i], t.x + 1, y + 1, kInkShadow, 1, n);
    c.drawText(f, lines_[i], t.x, y, titled_ && i == 0 ? kTitleInk : kInk, 1, n);
    left -= n;
  }
  if (more_) c.drawText(f, kMoreArrow, t.right() - 12, t.bottom() - kLineH, kTitleInk);

  for (int i = 0; i < optionCount_; i++) {
    gfx::Rect r = optionRect(i);
    c.fillRoundRect(r, 4, kFrame);
    c.fillRoundRect(r.inflate(-2), 3, i == pressed_ ? kButtonPressed : kButton);
    int ty = r.y + (r.h - f.height) / 2 + 1;
    if (i == pressed_) c.drawText(f, kCursor, r.x + 5, ty, kInk);
    c.drawText(f, options_[i], r.x + 17, ty, kInk);
  }
}

// --- PlateLayer ---

void PlateLayer::place(const gfx::Rect& r) {
  if (r == bounds()) invalidate();
  else setBounds(r);
}

void PlateLayer::set(const char* name, int level, float hp) {
  int barW = bounds().w - 44;
  int px = int(std::max(0.0f, std::min(1.0f, hp)) * barW + 0.5f);
  if (strcmp(name, name_) == 0 && level == level_ && px == hpPx_) return;
  core::copyUtf8(name_, sizeof(name_), name);
  level_ = level;
  hpPx_ = px;
  invalidate();
}

void PlateLayer::draw(gfx::Canvas& c) const {
  const gfx::Font& f = gfx::kFontKo11;
  const gfx::Rect& b = bounds();
  c.fillRoundRect(b, 5, kFrame);
  c.fillRoundRect(b.inflate(-2), 4, kPlate);

  char lv[8];
  snprintf(lv, sizeof(lv), "Lv%d", level_);
  int lvW = gfx::Canvas::textWidth(f, lv);
  c.drawText(f, name_, b.x + 8, b.y + 4, kInk);
  c.drawText(f, lv, b.right() - 8 - lvW, b.y + 4, kInk);

  // "HP" tag and the bar: green, then yellow below half, red below a fifth
  int barW = b.w - 44, by = b.y + b.h - 13;
  c.fillRoundRect(gfx::Rect(b.x + 8, by - 2, b.w - 16, 10), 4, kHpBack);
  c.drawText("HP", b.x + 13, by, kHpLabel, 1);
  c.fillRect(gfx::Rect(b.x + 32, by + 1, barW, 4), kPlate);
  gfx::Color bar = hpPx_ * 2 > barW ? gfx::rgb(72, 200, 88) : (hpPx_ * 5 > barW ? gfx::rgb(248, 192, 48) : gfx::rgb(240, 80, 56));
  if (hpPx_ > 0) c.fillRect(gfx::Rect(b.x + 32, by + 1, hpPx_, 4), bar);
}

// --- FxLayer ---

void FxLayer::spawn(const Spark& s) {
  if (count_ == kMax) return;
  sparks_[count_++] = s;
}

void FxLayer::step(float dt) {
  if (!count_) return;
  int n = 0;
  for (int i = 0; i < count_; i++) {
    Spark s = sparks_[i];
    s.age += dt;
    if (s.age >= s.life) continue;
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    sparks_[n++] = s;
  }
  count_ = n;
  updateBounds();
}

void FxLayer::clear() {
  count_ = 0;
  updateBounds();
}

void FxLayer::updateBounds() {
  gfx::Rect b;
  for (int i = 0; i < count_; i++) {
    const Spark& s = sparks_[i];
    b = b.unite(gfx::Rect(int(s.x) - s.size, int(s.y) - s.size, 2 * s.size, 2 * s.size));
  }
  setVisible(count_ > 0);
  if (b == bounds()) invalidate();
  else setBounds(b);
}

void FxLayer::draw(gfx::Canvas& c) const {
  for (int i = 0; i < count_; i++) {
    const Spark& s = sparks_[i];
    int half = std::max(1, int(s.size * (1.0f - 0.5f * s.age / s.life)));  // shrink as they fly
    gfx::Rect r(int(s.x) - half, int(s.y) - half, 2 * half, 2 * half);
    if (s.round) c.fillRoundRect(r, half, s.color);
    else c.fillRect(r, s.color);
  }
}

// --- WipeLayer ---

void WipeLayer::configure(const gfx::Rect& screen) {
  setVisible(false);
  setBounds(screen);
}

// Hiding invalidates the covered area, so whatever is underneath gets redrawn
void WipeLayer::flash(bool on) {
  if (on == (visible() && flash_)) return;
  flash_ = on;
  barsPx_ = 0;
  setVisible(on);
  if (on) invalidate();
}

void WipeLayer::bars(float closed) {
  int px = int(std::max(0.0f, std::min(1.0f, closed)) * bounds().w + 0.5f);
  if (!flash_ && px == barsPx_ && visible() == (px > 0)) return;
  flash_ = false;
  barsPx_ = px;
  setVisible(px > 0);
  if (px > 0) invalidate();
}

void WipeLayer::off() {
  flash_ = false;
  barsPx_ = 0;
  setVisible(false);
}

void WipeLayer::draw(gfx::Canvas& c) const {
  const gfx::Rect& b = bounds();
  if (flash_) {
    c.fillRect(b, kWhite);
    return;
  }
  for (int y = b.y, i = 0; y < b.bottom(); y += kBandH, i++) {
    int x = (i & 1) ? b.right() - barsPx_ : b.x;  // alternate bands close from either side
    c.fillRect(gfx::Rect(x, y, barsPx_, kBandH), kWipe);
  }
}

}  // namespace ui::monster
