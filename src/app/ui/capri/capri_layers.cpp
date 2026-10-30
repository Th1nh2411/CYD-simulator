#include "app/ui/capri/capri_layers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/core/utf8.h"
#include "app/gfx/font.h"

namespace ui::capri {

namespace {

// lachydotmcg/capri-desktop's speech box and mochiiwuff/capri-desktop's panel
const gfx::Color kPlate = gfx::rgb(9, 11, 15);
const gfx::Color kNeon = gfx::rgb(56, 176, 255);    // her text
const gfx::Color kRed = gfx::rgb(232, 36, 64);      // "CAPRI:", the bar
const gfx::Color kDim = gfx::rgb(122, 128, 138);    // "> sys:"
const gfx::Color kInk = gfx::rgb(232, 236, 240);
const gfx::Color kShadow = gfx::rgb(123, 61, 255);  // violet drop shadow
const gfx::Color kBlack = gfx::rgb(0, 0, 0);
const gfx::Color kWhite = gfx::rgb(255, 255, 255);
const gfx::Color kSkyTop = gfx::rgb(8, 7, 16), kSkyBottom = gfx::rgb(34, 10, 38);

constexpr int kPanelChamfer = 14, kShadowOffset = 5;
constexpr int kLineH = 15, kFacePad = 10;
constexpr int kChoiceH = 24, kChoiceGap = 8, kChoiceBottom = 9;
const char kNoise[] = "#%&/\\<>01XZ";

uint32_t hash(uint32_t v) {
  v ^= v >> 16;
  v *= 0x7feb352du;
  v ^= v >> 15;
  v *= 0x846ca68bu;
  return v ^ (v >> 16);
}

// text cut to fit maxW px, ending in "…" when it had to be cut
void fitText(const gfx::Font& f, const char* text, int maxW, char* out, size_t size) {
  core::copyUtf8(out, size, text);
  if (gfx::Canvas::textWidth(f, out) <= maxW) return;
  const char* const kEllipsis = "\xE2\x80\xA6";
  const int room = maxW - gfx::Canvas::textWidth(f, kEllipsis);
  size_t n = 0;  // bytes of whole characters that fit
  for (const char* p = out; *p;) {
    const char* next = p;
    core::nextCodePoint(next);
    if (int(gfx::advanceOf(f, out, size_t(next - out))) > room) break;
    n = size_t(next - out);
    p = next;
  }
  while (n && out[n - 1] == ' ') n--;
  snprintf(out + n, size - n, "%s", kEllipsis);
}

int countChars(const char* s) {
  int n = 0;
  while (*s) {
    core::nextCodePoint(s);
    n++;
  }
  return n;
}

void fillSpan(gfx::Canvas& c, int y, int x0, int x1, gfx::Color color) {
  const gfx::Rect& a = c.area();
  x0 = std::max<int>(x0, a.x);
  x1 = std::min<int>(x1, a.right());
  if (x1 <= x0) return;
  gfx::Color* p = c.rowPtr(y) + (x0 - a.x);
  for (int i = x1 - x0; i > 0; i--) *p++ = color;
}

void blendSpan(gfx::Canvas& c, int y, int x0, int x1, const gfx::MixTo& blend) {
  const gfx::Rect& a = c.area();
  x0 = std::max<int>(x0, a.x);
  x1 = std::min<int>(x1, a.right());
  if (x1 <= x0) return;
  gfx::Color* p = c.rowPtr(y) + (x0 - a.x);
  for (int i = x1 - x0; i > 0; i--, p++) *p = blend(*p);
}

// One row of an opaque RLE picture placed at x, drawn only on [clip0, clip1)
void rleRow(gfx::Canvas& c, const gfx::RleImage& img, int row, int x, int y, int clip0, int clip1) {
  const gfx::Rect& a = c.area();
  clip0 = std::max<int>(clip0, a.x);
  clip1 = std::min<int>(clip1, a.right());
  const uint8_t* run = img.runs + 2 * img.rows[row];
  gfx::Color* base = c.rowPtr(y);
  for (int px = x; px < clip1 && px < x + img.w; run += 2) {
    const int l = std::max(px, clip0), r = std::min(px + run[0], clip1);
    px += run[0];
    if (r <= l) continue;
    const gfx::Color color = img.palette[run[1]];
    gfx::Color* p = base + (l - a.x);
    for (int i = r - l; i > 0; i--) *p++ = color;
  }
}

// Row r of a w x h box with its top-left and bottom-right corners cut by k: [left, right)
void cutTopLeft(int r, int w, int h, int k, int& left, int& right) {
  left = r < k ? k - r : 0;
  right = r >= h - k ? w - (r - (h - k)) : w;
}

// The same with the top-right and bottom-left corners cut
void cutTopRight(int r, int w, int h, int k, int& left, int& right) {
  left = r >= h - k ? r - (h - k) : 0;
  right = r < k ? w - (k - r) : w;
}

}  // namespace

// ---- VoidLayer ----

void VoidLayer::configure(const gfx::Rect& screen, uint32_t seed) {
  setBounds(screen);
  for (int i = 0; i < kStars; i++) {
    const uint32_t h = hash(seed + uint32_t(i) * 0x9E3779B9u);
    starX_[i] = int16_t(h % uint32_t(screen.w));
    starY_[i] = int16_t((h >> 12) % uint32_t(std::max(1, screen.h * 3 / 4)));
    starLevel_[i] = uint8_t((h >> 24) % 7 == 0 ? 2 : (h >> 24) % 3 == 0 ? 1 : 0);
  }
}

gfx::Color VoidLayer::colorAt(int y) const {
  const int h = std::max<int>(1, bounds().h);
  gfx::Color c = gfx::mix(kSkyTop, kSkyBottom, uint32_t(std::max(0, std::min(32, y * 32 / h))));
  return y % 3 == 0 ? gfx::mix(c, kBlack, 7) : c;
}

gfx::Rect VoidLayer::satRect() const { return gfx::Rect(satX_ - 10, satY_ - 1, 13, 4); }

void VoidLayer::setSatellite(int x, int y) {
  if (x == satX_ && y == satY_) return;
  if (satX_ >= 0) invalidate(satRect());
  satX_ = x;
  satY_ = y;
  if (satX_ >= 0) invalidate(satRect());
}

gfx::Rect VoidLayer::talkRect() const {
  const gfx::Rect& b = bounds();
  return gfx::Rect(b.right() - 66, b.bottom() - 30, 58, 22);
}

void VoidLayer::setEdges(uint32_t crackle) {
  if (crackle == edges_) return;
  edges_ = crackle;
  const gfx::Rect& b = bounds();
  invalidate(gfx::Rect(b.x, b.y, 10, b.h));
  invalidate(gfx::Rect(b.right() - 10, b.y, 10, b.h));
}

void VoidLayer::setTalk(bool shown, bool lit) {
  if (shown == talk_ && lit == talkLit_) return;
  talk_ = shown;
  talkLit_ = lit;
  invalidate(talkRect());
}

void VoidLayer::draw(gfx::Canvas& c) const {
  const gfx::Rect& a = c.area();
  for (int y = a.y; y < a.bottom(); y++) fillSpan(c, y, a.x, a.right(), colorAt(y));
  if (edges_) {  // jagged red runs down both edges, a new pattern for each crackle
    const gfx::Rect& b = bounds();
    for (int y = a.y; y < a.bottom(); y++) {
      const uint32_t h = hash(edges_ * 977u + uint32_t(y / 5));
      if (h % 3 == 0) continue;
      const int w = 2 + int(h >> 8) % 7;
      fillSpan(c, y, b.x, b.x + w, (h >> 4) & 1 ? kRed : kWhite);
      fillSpan(c, y, b.right() - 2 - int(h >> 16) % 7, b.right(), (h >> 5) & 1 ? kRed : kWhite);
    }
  }
  if (talk_ && talkRect().intersects(a)) {  // a chamfered chip like her panel's buttons
    const gfx::Rect r = talkRect();
    for (int y = std::max<int>(a.y, r.y); y < std::min<int>(a.bottom(), r.bottom()); y++) {
      int l, rt;
      cutTopLeft(y - r.y, r.w, r.h, 7, l, rt);
      if (talkLit_ || y == r.y || y == r.bottom() - 1) {
        fillSpan(c, y, r.x + l, r.x + rt, talkLit_ ? kNeon : gfx::mix(kNeon, kPlate, 8));
      } else {
        fillSpan(c, y, r.x + l, r.x + rt, kPlate);
        fillSpan(c, y, r.x + l, r.x + l + 1, kNeon);
        fillSpan(c, y, r.x + rt - 1, r.x + rt, kNeon);
      }
    }
    c.drawText("TALK", r.x + (r.w - gfx::Canvas::textWidth("TALK", 1)) / 2 + 4, r.y + 8, talkLit_ ? kBlack : kNeon, 1);
    c.drawText(">", r.x + 7, r.y + 8, talkLit_ ? kBlack : kRed, 1);
  }
  static const gfx::Color kStar[3] = {gfx::rgb(92, 30, 74), gfx::rgb(170, 52, 120), gfx::rgb(255, 170, 214)};
  for (int i = 0; i < kStars; i++) {
    const int x = starX_[i], y = starY_[i];
    if (!a.contains(x, y)) continue;
    c.rowPtr(y)[x - a.x] = kStar[starLevel_[i]];
    if (starLevel_[i] == 2 && x + 1 < a.right()) c.rowPtr(y)[x + 1 - a.x] = kStar[1];
  }
  if (satX_ < 0) return;
  // The satellite: a bright dot with a short cyan trail behind it
  const gfx::Rect s = satRect().intersect(a);
  for (int y = s.y; y < s.bottom(); y++) {
    const int dy = y - satY_;
    if (dy == 0 || dy == 1) fillSpan(c, y, satX_, satX_ + 2, kWhite);
    if (dy == 0) {
      for (int k = 1; k <= 9; k++) {
        const int x = satX_ - k;
        if (x >= a.x && x < a.right()) c.rowPtr(y)[x - a.x] = gfx::mix(kNeon, colorAt(y), uint32_t(k * 3));
      }
    }
  }
}

// ---- FigureLayer ----

bool FigureLayer::Pose::operator==(const Pose& o) const {
  if (x != o.x || y != o.y || sway != o.sway || ghostDx != o.ghostDx || ghostY0 != o.ghostY0 || ghostY1 != o.ghostY1 ||
      cel != o.cel || rowsFrom != o.rowsFrom || rowsTo != o.rowsTo) {
    return false;
  }
  for (int i = 0; i < kSlices; i++) {
    if (slices[i].y0 != o.slices[i].y0 || slices[i].y1 != o.slices[i].y1 || slices[i].dx != o.slices[i].dx) return false;
  }
  return true;
}

void FigureLayer::configure(const gfx::SpanImage* cels, const CelRows* changed, int neck, const gfx::SpanPalette& main,
                            const gfx::SpanPalette& ghost, const gfx::Rect& screen) {
  cels_ = cels;
  changed_ = changed;
  neck_ = neck;
  main_ = main;
  ghost_ = ghost;
  posed_ = false;
  setBounds(screen);
}

int FigureLayer::shift(int row) const {
  int dx = row < neck_ && neck_ > 0 ? pose_.sway * (neck_ - row) / neck_ : 0;
  for (const Slice& s : pose_.slices) {
    if (row >= s.y0 && row < s.y1) dx += s.dx;
  }
  return std::max(-kReach, std::min(kReach, dx));
}

gfx::Rect FigureLayer::onScreen() const {
  return gfx::Rect(pose_.x - 2 * kReach, pose_.y, img().w + 4 * kReach, img().h);
}

void FigureLayer::setPose(const Pose& p) {
  if (posed_ && p == pose_) return;
  Pose same = p;
  same.cel = pose_.cel;
  if (posed_ && same == pose_) {  // only the cel: the rows where either differs from the rest picture
    const CelRows a = changed_[pose_.cel], b = changed_[p.cel];
    const int first = std::min(a.end > a.first ? a.first : b.first, b.end > b.first ? b.first : a.first);
    const int end = std::max(a.end, b.end);
    pose_.cel = p.cel;
    const gfx::Rect r = onScreen();
    if (end > first) invalidate(gfx::Rect(r.x, r.y + first, r.w, end - first));
    return;
  }
  if (posed_) invalidate(onScreen());
  pose_ = p;
  pose_.ghostDx = std::max(-kReach, std::min(kReach, pose_.ghostDx));
  posed_ = true;
  invalidate(onScreen());
}

bool FigureLayer::contains(int x, int y) const {
  if (!posed_ || !visible()) return false;
  // Near her counts: she is mostly thin limbs and spikes, too fine for a fingertip
  static const int8_t kNear[9][2] = {{0, 0}, {-6, 0}, {6, 0}, {0, -6}, {0, 6}, {-12, 0}, {12, 0}, {0, -12}, {0, 12}};
  for (const auto& d : kNear) {
    const int row = y + d[1] - pose_.y;
    if (row < std::max<int>(0, pose_.rowsFrom) || row >= std::min<int>(img().h, pose_.rowsTo)) continue;
    if (gfx::spanOpaqueAt(img(), x + d[0] - pose_.x - shift(row), row, main_)) return true;
  }
  return false;
}

void FigureLayer::draw(gfx::Canvas& c) const {
  if (!posed_) return;
  const gfx::Rect r = onScreen().intersect(c.area());
  for (int y = r.y; y < r.bottom(); y++) {
    const int row = y - pose_.y, x = pose_.x + shift(row);
    if (row < pose_.rowsFrom || row >= pose_.rowsTo) continue;
    if (pose_.ghostDx && row >= pose_.ghostY0 && row < pose_.ghostY1) {
      gfx::drawSpanRow(c, img(), row, x + pose_.ghostDx, y, ghost_);
    }
    gfx::drawSpanRow(c, img(), row, x, y, main_);
  }
}

// ---- CutinLayer ----

void CutinLayer::show(const gfx::RleImage& img, int x, int y) {
  img_ = &img;
  x_ = x;
  y_ = y;
  reveal_ = 0;
  setBounds(gfx::Rect(x - 1, y - 1, img.w + 2 * kBorder + 2, img.h + 2 * kBorder + 2));
  setVisible(true);
}

void CutinLayer::setReveal(float u) {
  const int steps = std::max(0, std::min(4, int(u * 4)));
  if (steps == reveal_) return;
  reveal_ = steps;
  invalidate();
}

int CutinLayer::bandDx(int row) const {
  if (reveal_ >= 4) return 0;
  const int band = row / 10;
  const int side = (band & 1) ? 1 : -1, weight = 1 + int(hash(uint32_t(band)) % 3);
  return side * (4 - reveal_) * kReach * weight / 12;
}

void CutinLayer::draw(gfx::Canvas& c) const {
  if (!img_) return;
  const gfx::Rect& b = bounds();
  const gfx::Rect r = b.intersect(c.area());
  const int fw = img_->w + 2 * kBorder, fh = img_->h + 2 * kBorder;
  for (int y = r.y; y < r.bottom(); y++) {
    int l, rt;
    cutTopLeft(y - b.y, b.w, b.h, kChamfer + 1, l, rt);
    fillSpan(c, y, b.x + l, b.x + rt, kBlack);  // outline
    const int fr = y - y_;
    if (fr < 0 || fr >= fh) continue;
    cutTopLeft(fr, fw, fh, kChamfer, l, rt);
    fillSpan(c, y, x_ + l, x_ + rt, kWhite);  // frame
    const int row = fr - kBorder;
    if (row < 0 || row >= img_->h) continue;
    int il, ir;
    cutTopLeft(row, img_->w, img_->h, kChamfer - kBorder, il, ir);
    const int x0 = x_ + kBorder + il, x1 = x_ + kBorder + ir;
    const int dx = bandDx(row);
    if (dx) fillSpan(c, y, x0, x1, kBlack);
    rleRow(c, *img_, row, x_ + kBorder + dx, y, x0, x1);
  }
}

// ---- PanelLayer ----

void PanelLayer::configure(const gfx::Rect& room, bool withFace, bool onBottom) {
  room_ = box_ = room;
  withFace_ = withFace;
  onBottom_ = onBottom;
  rewrap();
}

void PanelLayer::place(int h) {
  h = std::min<int>(h, room_.h);
  const int y = onBottom_ ? room_.bottom() - h : room_.y + (room_.h - h) / 2;
  box_ = gfx::Rect(room_.x, y, room_.w, h);
  setBounds(gfx::Rect(box_.x, box_.y, box_.w + kShadowOffset, box_.h + kShadowOffset));
}

void PanelLayer::setStyle(gfx::Color accent, const char* badge, const gfx::RleImage* face) {
  accent_ = accent;
  core::copyUtf8(badge_, sizeof(badge_), badge);
  face_ = face;
  invalidate();
}

void PanelLayer::setOpen(float u) {
  const int steps = std::max(0, std::min(4, int(u * 4 + 0.001f)));
  if (steps == open_) return;
  open_ = steps;
  invalidate();
}

int PanelLayer::textX() const { return box_.x + (withFace_ ? kFacePad + 64 + 12 : 16); }
int PanelLayer::textTop() const { return box_.y + 38; }

gfx::Rect PanelLayer::lineRect(int line) const {
  return gfx::Rect(textX() - 1, textTop() + line * kLineH, box_.right() - textX() - 6, kLineH + 1);
}

gfx::Rect PanelLayer::choicesArea() const {
  const int top = box_.bottom() - kChoiceBottom - kChoiceH - 8;
  return gfx::Rect(box_.x + 10, top, box_.w - 20, box_.bottom() - top);
}

gfx::Rect PanelLayer::choiceRect(int i) const {
  const int n = std::max(1, choiceCount_), x0 = box_.x + 14, inner = box_.w - 28;
  const int w = (inner - (n - 1) * kChoiceGap) / n;
  const int rise = int((1 - choicesShown_) * 6);
  return gfx::Rect(x0 + i * (w + kChoiceGap), box_.bottom() - kChoiceBottom - kChoiceH + rise, w, kChoiceH);
}

void PanelLayer::rewrap() {
  const gfx::Font& f = gfx::kFontKo11;
  const int width = room_.right() - textX() - 12;
  const int below = roomForChoices_ ? kChoiceBottom + kChoiceH + 6 : 8;  // under the text
  const int room = std::max(1, std::min(kMaxLines, (room_.h - (textTop() - box_.y) - below) / kLineH));
  lineCount_ = gfx::wrapText(f, text_, width, lines_[0], kLineBytes, room);
  // As tall as the text needs, and never shorter than her face
  const int faceH = withFace_ ? 2 * kFacePad + 64 + (roomForChoices_ ? kChoiceH + 8 : 0) : 0;
  place(std::max(faceH, textTop() - box_.y + std::max(1, lineCount_) * kLineH + below));
  length_ = 0;
  for (int i = 0; i < lineCount_; i++) {
    lineLength_[i] = countChars(lines_[i]);
    length_ += lineLength_[i];
  }
  typed_ = std::min(typed_, length_);
  invalidate();
}

void PanelLayer::setMessage(const char* sys, const char* text, bool withChoices) {
  // The sys line on one row, cut short with "..." when long
  const int badgeW = gfx::Canvas::textWidth(badge_, 1) + 10;
  const int sysW = box_.right() - (textX() + badgeW + 8) - 10;
  char wrapped[kLineBytes];
  if (sys && *sys && gfx::wrapText(gfx::kFontKo11, sys, sysW, wrapped, sizeof(wrapped), 1)) {
    snprintf(sys_, sizeof(sys_), "%s", wrapped);
  } else {
    sys_[0] = '\0';
  }
  core::copyUtf8(text_, sizeof(text_), text);
  roomForChoices_ = withChoices;
  typed_ = 0;
  cursor_ = false;
  rewrap();
}

void PanelLayer::setTyped(int chars, uint32_t noise) {
  chars = std::max(0, std::min(chars, length_));
  const bool edge = chars < length_;
  if (chars == typed_ && (!edge || noise == noise_)) return;
  const int lo = std::min(chars, typed_), hi = std::max(chars, typed_) + 2;
  int start = 0;
  for (int i = 0; i < lineCount_; i++) {
    const int end = start + lineLength_[i];
    if (end >= lo && start <= hi) invalidate(lineRect(i));
    start = end;
  }
  typed_ = chars;
  noise_ = noise;
}

void PanelLayer::setCursor(bool on) {
  if (on == cursor_) return;
  cursor_ = on;
  if (lineCount_) invalidate(lineRect(lineCount_ - 1));
}

void PanelLayer::setBadgeOn(bool on) {
  if (on == badgeOn_) return;
  badgeOn_ = on;
  invalidate(gfx::Rect(textX() - 2, box_.y + 6, gfx::Canvas::textWidth(badge_, 1) + 14, 16));
}

void PanelLayer::setChoices(const char (*options)[25], int count) {
  count = std::max(0, std::min(count, engine::kMaxOptions));
  choiceCount_ = count;
  for (int i = 0; i < count; i++) core::copyUtf8(choices_[i], sizeof(choices_[i]), options[i]);
  pressed_ = -1;
  choicesShown_ = 0;
  invalidate(choicesArea());
}

void PanelLayer::showChoices(float shown) {
  shown = std::max(0.0f, std::min(1.0f, shown));
  if (shown - choicesShown_ < 0.01f && choicesShown_ - shown < 0.01f) return;
  choicesShown_ = shown;
  invalidate(choicesArea());
}

void PanelLayer::setPressed(int option) {
  if (option == pressed_) return;
  pressed_ = option;
  invalidate(choicesArea());
}

int PanelLayer::choiceAt(int x, int y) const {
  if (!visible() || choicesShown_ < 0.9f) return -1;
  for (int i = 0; i < choiceCount_; i++) {
    if (choiceRect(i).inflate(4).contains(x, y)) return i;
  }
  return -1;
}

void PanelLayer::draw(gfx::Canvas& c) const {
  if (open_ <= 0) return;
  const gfx::Rect& a = c.area();
  const gfx::Font& f = gfx::kFontKo11;
  const int w = box_.w, h = box_.h;
  // Transmitting in: the plate grows from its left edge in steps
  const int shownW = w * open_ / 4;
  const gfx::MixTo plate(kPlate, 29), shadow(kShadow, 20), scan(kPlate, 25);
  const gfx::Rect r = bounds().intersect(a);
  for (int y = r.y; y < r.bottom(); y++) {
    int l, rt;
    const int sr = y - box_.y - kShadowOffset;
    if (sr >= 0 && sr < h) {
      cutTopRight(sr, w, h, kPanelChamfer, l, rt);
      blendSpan(c, y, box_.x + kShadowOffset + l, box_.x + kShadowOffset + std::min(rt, shownW), shadow);
    }
    const int pr = y - box_.y;
    if (pr < 0 || pr >= h) continue;
    cutTopRight(pr, w, h, kPanelChamfer, l, rt);
    rt = std::min(rt, shownW);
    if (rt <= l) continue;
    // Plate with scanlines, then the border and the red bar
    blendSpan(c, y, box_.x + l, box_.x + rt, pr % 3 == 0 ? scan : plate);
    if (pr < 2 || pr >= h - 2) {
      fillSpan(c, y, box_.x + l, box_.x + rt, accent_);
    } else {
      fillSpan(c, y, box_.x + l, box_.x + l + 2, accent_);
      fillSpan(c, y, box_.x + rt - 2, box_.x + rt, accent_);
      if (pr >= 8 && pr < h - 8) fillSpan(c, y, box_.x + l + 5, box_.x + l + 8, kRed);
    }
  }
  if (open_ < 4) {  // a bright scan edge while it transmits
    const int x = box_.x + shownW - 1;
    for (int y = std::max<int>(a.y, box_.y); y < std::min<int>(a.bottom(), box_.bottom()); y++) {
      fillSpan(c, y, x, x + 2, kWhite);
    }
    return;
  }

  // Her face in a frame with its top-left corner cut
  if (withFace_ && face_) {
    const int fx = box_.x + kFacePad + 4, fy = box_.y + kFacePad;
    const int s = face_->w;
    for (int y = std::max<int>(a.y, fy - 1); y < std::min<int>(a.bottom(), fy + s + 1); y++) {
      int l, rt;
      cutTopLeft(y - (fy - 1), s + 2, s + 2, 9, l, rt);
      fillSpan(c, y, fx - 1 + l, fx - 1 + rt, accent_);
      const int row = y - fy;
      if (row < 0 || row >= s) continue;
      cutTopLeft(row, s, s, 8, l, rt);
      rleRow(c, *face_, row, fx, y, fx + l, fx + rt);
    }
  }

  // Badge, "> sys: <title>", "CAPRI:"
  const int tx = textX(), by = box_.y + 8;
  const int badgeW = gfx::Canvas::textWidth(badge_, 1) + 10;
  const gfx::Rect badge(tx, by, badgeW, 13);
  if (badgeOn_) c.fillRect(badge, accent_);
  else c.fillRect(badge.inflate(-1), kPlate);
  c.drawText(badge_, tx + 5, by + 3, badgeOn_ ? kBlack : accent_, 1);
  if (sys_[0]) {
    const int sx = tx + badgeW + 8;
    const int px = c.drawText(f, "> sys:", sx, by - 1, kDim);
    c.drawText(f, sys_, px + 4, by - 1, kDim);
  }
  c.drawText("CAPRI:", tx, box_.y + 27, kRed, 1);

  // Her text so far, noise glyphs at its edge, a block cursor once it is out
  int left = typed_, penX = tx, penY = textTop();
  for (int i = 0; i < lineCount_; i++) {
    const int y = textTop() + i * kLineH;
    const int n = std::min(left, lineLength_[i]);
    const int pen = c.drawText(f, lines_[i], tx, y, kNeon, 1, n);
    left -= n;
    penX = pen;
    penY = y;
    if (n < lineLength_[i]) {
      char glyphs[3] = {kNoise[hash(noise_) % (sizeof(kNoise) - 1)], kNoise[hash(noise_ + 7) % (sizeof(kNoise) - 1)],
                        '\0'};
      if (lineLength_[i] - n < 2) glyphs[1] = '\0';
      c.drawText(f, glyphs, pen, y, gfx::mix(accent_, kPlate, 10));
      break;
    }
  }
  if (cursor_) c.fillRect(gfx::Rect(penX + 2, penY + 2, 6, 11), kNeon);

  // Choices: chamfered buttons rising in
  if (choicesShown_ <= 0) return;
  for (int i = 0; i < choiceCount_; i++) {
    const gfx::Rect b = choiceRect(i);
    const bool on = i == pressed_;
    for (int y = std::max<int>(a.y, b.y); y < std::min<int>(a.bottom(), b.bottom()); y++) {
      int l, rt;
      cutTopLeft(y - b.y, b.w, b.h, 7, l, rt);
      if (on || y == b.y || y == b.bottom() - 1) {
        fillSpan(c, y, b.x + l, b.x + rt, on ? accent_ : gfx::mix(accent_, kPlate, 8));
      } else {
        fillSpan(c, y, b.x + l, b.x + l + 1, accent_);
        fillSpan(c, y, b.x + rt - 1, b.x + rt, accent_);
      }
    }
    if (choicesShown_ < 0.6f) continue;
    char label[32];
    fitText(f, choices_[i], b.w - 10, label, sizeof(label));
    const int tw = gfx::Canvas::textWidth(f, label);
    c.drawText(f, label, b.x + (b.w - tw) / 2, b.y + (b.h - f.height) / 2 + 1, on ? kBlack : kInk);
  }
}

}  // namespace ui::capri
