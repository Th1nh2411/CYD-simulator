#include "app/ui/heroine/heroine_layers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/core/utf8.h"
#include "app/gfx/font.h"
#include "app/gfx/sdf.h"

namespace ui::heroine {

namespace {

const gfx::Color kInk = gfx::rgb(74, 52, 70);
const gfx::Color kWhite = gfx::rgb(255, 255, 255);
const gfx::Color kBlack = gfx::rgb(0, 0, 0);
constexpr int kLineH = 15;       // text line pitch, px
constexpr int kPad = 14;         // text inset from the box edge
constexpr int kTop = 12;         // first line below the box top
constexpr int kChoiceH = 24, kChoiceGap = 8, kChoiceBottom = 9;
constexpr int kChoiceRise = 6;   // px it rises while appearing, staying inside the box
constexpr int kRadius = 10;
constexpr uint32_t kPanelAlpha = 27;  // of 32: the room shows through a little
const char* const kMoreArrow = "\xE2\x96\xBC";  // ▼
const char* const kHeart = "\xE2\x99\xA5";      // ♥
const char* const kMenu = "LOG  AUTO  SKIP";     // decoration, as in the games

// Exact distance to a rounded rectangle (negative inside)
float roundBox(float px, float py, const gfx::Rect& r, float radius) {
  const float hw = r.w * 0.5f, hh = r.h * 0.5f;
  const float qx = fabsf(px - (r.x + hw)) - (hw - radius), qy = fabsf(py - (r.y + hh)) - (hh - radius);
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return sqrtf(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

// Rounded panel with an anti-aliased border ring
void panel(gfx::Canvas& c, const gfx::Rect& r, float radius, float border, gfx::Color fill, uint32_t fillAlpha,
           gfx::Color line, uint32_t lineAlpha) {
  const float half = border * 0.5f;
  gfx::sdf::paint(c, r, fill, fillAlpha, [&](float x, float y) { return roundBox(x, y, r, radius) + border; });
  gfx::sdf::paint(c, r, line, lineAlpha,
                  [&](float x, float y) { return fabsf(roundBox(x, y, r, radius) + half) - half; });
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

// --- BackdropLayer ---

void BackdropLayer::show(const gfx::RleImage& img) {
  if (&img == img_) return;
  img_ = &img;
  invalidate();
}

void BackdropLayer::draw(gfx::Canvas& c) const {
  if (!img_) return;
  const gfx::Rect& b = bounds();
  gfx::drawRleImage(c, *img_, b.x + (b.w - img_->w) / 2, b.y + (b.h - img_->h) / 2);
}

// --- PortraitLayer ---

namespace {

// Rows from her chest to the screen's bottom edge at rest: they stretch as her
// shoulders rise, so that her waist stays where it is
constexpr int kStretchRows = kHeroineShownHeight - kHeroineChestRow;

}  // namespace

void PortraitLayer::configure(const gfx::Color* palette, const gfx::Rect& screen) {
  palette_.colors = palette;
  palette_.opacity = kHeroinePaletteOpacity;
  setBounds(screen);
}

int PortraitLayer::pictureRow(int y) const {
  const int chest = pose_.y + kHeroineChestRow - pose_.lift;  // screen row of the chest's first row
  if (y < chest) return y - pose_.y + pose_.lift;
  const int k = y - chest;
  if (k < kStretchRows + pose_.lift) return kHeroineChestRow + k * kStretchRows / (kStretchRows + pose_.lift);
  return y - pose_.y;
}

gfx::Rect PortraitLayer::onScreen() const {
  const int rise = std::max(pose_.lift, 0);
  return gfx::Rect(pose_.x + kHeroineBoundsX, pose_.y + kHeroineBoundsY - rise, kHeroineBoundsW,
                   kHeroineBoundsH + rise);
}

const PortraitPatch* PortraitLayer::patch(int frame) const {
  return frame >= 0 ? &kHeroineFrames[frame][pose_.tilt + kHeroineTiltMax] : nullptr;
}

gfx::Rect PortraitLayer::onScreen(int frame) const {
  const PortraitPatch& p = *patch(frame);
  return gfx::Rect(pose_.x + p.x, pose_.y + p.y - pose_.lift, p.image.w, p.image.h);  // frames lie above the chest
}

void PortraitLayer::setPose(const Pose& p) {
  if (posed_ && p == pose_) return;
  if (posed_) invalidate(onScreen());
  pose_ = p;
  posed_ = true;
  invalidate(onScreen());
}

void PortraitLayer::setFrames(int face, int mouth) {
  if (face == face_ && mouth == mouth_) return;
  for (int f : {face_, face, mouth_, mouth}) {
    if (f >= 0 && posed_) invalidate(onScreen(f));
  }
  face_ = face;
  mouth_ = mouth;
}

bool PortraitLayer::contains(int x, int y) const {
  return posed_ &&
         gfx::spanOpaqueAt(kHeroinePoses[pose_.tilt + kHeroineTiltMax], x - pose_.x, pictureRow(y), palette_);
}

void PortraitLayer::draw(gfx::Canvas& c) const {
  if (!posed_) return;
  const gfx::Rect r = onScreen().intersect(c.area());
  const gfx::SpanImage& pose = kHeroinePoses[pose_.tilt + kHeroineTiltMax];
  const PortraitPatch* over[2] = {patch(face_), patch(mouth_)};
  for (int y = r.y; y < r.bottom(); y++) {
    const int row = pictureRow(y);
    gfx::drawSpanRow(c, pose, row, pose_.x, y, palette_);
    for (const PortraitPatch* p : over) {
      if (p) gfx::drawSpanRow(c, p->image, row - p->y, pose_.x + p->x, y, palette_, p->x, p->y);
    }
  }
}

// --- DialogLayer ---

void DialogLayer::configure(const gfx::Rect& box, gfx::Color accent, const char* speaker) {
  rest_ = box;
  accent_ = accent;
  core::copyUtf8(speaker_, sizeof(speaker_), speaker);
  speakerW_ = gfx::Canvas::textWidth(gfx::kFontKo11, speaker_);
  place();
  rewrap();
}

gfx::Rect DialogLayer::box() const { return gfx::Rect(rest_.x, rest_.y + drop_, rest_.w, rest_.h); }

gfx::Rect DialogLayer::plate() const {
  const gfx::Rect b = box();
  return gfx::Rect(b.x + 12, b.y - 12, speakerW_ + 28, 22);
}

void DialogLayer::place() {
  gfx::Rect b = box().unite(plate());
  if (b == bounds()) invalidate();
  else setBounds(b);
}

void DialogLayer::setDrop(int dy) {
  if (dy == drop_) return;
  drop_ = dy;
  place();
}

gfx::Rect DialogLayer::lineRect(int line) const {
  const gfx::Rect b = box();
  return gfx::Rect(b.x + kPad - 1, b.y + kTop + line * kLineH, b.w - 2 * kPad + 2, kLineH + 1);
}

gfx::Rect DialogLayer::choicesArea() const {
  const gfx::Rect b = box();
  const int top = b.bottom() - kChoiceBottom - kChoiceH - 2;
  return gfx::Rect(b.x + kPad - 2, top, b.w - 2 * kPad + 4, b.bottom() - top);
}

gfx::Rect DialogLayer::choiceRect(int i) const {
  const gfx::Rect b = box();
  const int n = std::max(1, choiceCount_), inner = b.w - 2 * kPad;
  const int w = (inner - (n - 1) * kChoiceGap) / n;
  const int rise = int((1 - choicesShown_) * kChoiceRise);  // rises into place as it appears
  return gfx::Rect(b.x + kPad + i * (w + kChoiceGap), b.bottom() - kChoiceBottom - kChoiceH + rise, w, kChoiceH);
}

gfx::Rect DialogLayer::moreRect() const {
  const gfx::Rect b = box();
  return gfx::Rect(b.right() - kPad - 12, b.bottom() - 21 - 3, 14, kLineH + 5);
}

void DialogLayer::rewrap() {
  const gfx::Font& f = gfx::kFontKo11;
  const int width = rest_.w - 2 * kPad;
  const int room = roomForChoices_ ? 2 : kMaxLines;
  lineCount_ = 0;
  titled_ = title_[0] != '\0';
  if (titled_) lineCount_ = gfx::wrapText(f, title_, width, lines_[0], kLineBytes, 1);
  lineCount_ += gfx::wrapText(f, body_, width, lines_[lineCount_], kLineBytes, room - lineCount_);
  length_ = 0;
  for (int i = 0; i < lineCount_; i++) {
    lineLength_[i] = countChars(lines_[i]);
    length_ += lineLength_[i];
  }
  typed_ = std::min(typed_, length_);
  invalidate();
}

void DialogLayer::setMessage(const char* title, const char* body, bool withChoices) {
  core::copyUtf8(title_, sizeof(title_), title);
  core::copyUtf8(body_, sizeof(body_), body);
  roomForChoices_ = withChoices;
  typed_ = 0;
  more_ = false;
  rewrap();
}

void DialogLayer::setTyped(int chars) {
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

void DialogLayer::setMore(bool on, int lift) {
  if (on == more_ && lift == moreLift_) return;
  more_ = on;
  moreLift_ = lift;
  invalidate(moreRect());
}

void DialogLayer::setChoices(const char (*options)[25], int count) {
  count = std::max(0, std::min(count, engine::kMaxOptions));
  choiceCount_ = count;
  for (int i = 0; i < count; i++) core::copyUtf8(choices_[i], sizeof(choices_[i]), options[i]);
  pressed_ = -1;
  choicesShown_ = 0;
  invalidate(choicesArea());
}

void DialogLayer::showChoices(float shown) {
  shown = std::max(0.0f, std::min(1.0f, shown));
  if (fabsf(shown - choicesShown_) < 0.01f) return;
  choicesShown_ = shown;
  invalidate(choicesArea());
}

void DialogLayer::setPressed(int option) {
  if (option == pressed_) return;
  pressed_ = option;
  invalidate(choicesArea());
}

int DialogLayer::choiceAt(int x, int y) const {
  if (!visible() || choicesShown_ < 0.9f) return -1;
  for (int i = 0; i < choiceCount_; i++) {
    if (choiceRect(i).inflate(3).contains(x, y)) return i;
  }
  return -1;
}

void DialogLayer::draw(gfx::Canvas& c) const {
  const gfx::Font& f = gfx::kFontKo11;
  const gfx::Rect b = box();
  const gfx::Color paper = gfx::mix(kWhite, accent_, 2);
  panel(c, b, kRadius, 1.6f, paper, kPanelAlpha, accent_, 26);
  c.drawText(kMenu, b.right() - kPad - gfx::Canvas::textWidth(kMenu, 1), b.y + 5, gfx::mix(kInk, paper, 18), 1);

  // Name plate on the top edge
  const gfx::Rect p = plate();
  panel(c, p, p.h * 0.5f, 1.5f, accent_, 32, kWhite, 32);
  c.drawText(f, speaker_, p.x + (p.w - speakerW_) / 2, p.y + (p.h - f.height) / 2 + 1, kWhite);

  // Text typed so far
  const gfx::Color titleInk = gfx::mix(accent_, kBlack, 9);
  int left = typed_;
  for (int i = 0; i < lineCount_ && left > 0; i++) {
    const int n = std::min(left, lineLength_[i]);
    c.drawText(f, lines_[i], b.x + kPad, b.y + kTop + i * kLineH, titled_ && i == 0 ? titleInk : kInk, 1, n);
    left -= n;
  }
  if (more_) {
    const gfx::Rect m = moreRect();
    c.drawText(f, kMoreArrow, m.x + 1, m.y + 3 - moreLift_, accent_);
  }

  // Choices: pills that rise and fade in under the text
  if (choicesShown_ <= 0) return;
  const uint32_t alpha = uint32_t(choicesShown_ * 32 + 0.5f);
  for (int i = 0; i < choiceCount_; i++) {
    const gfx::Rect r = choiceRect(i);
    const bool on = i == pressed_;
    panel(c, r, r.h * 0.5f, 1.4f, on ? accent_ : kWhite, alpha, accent_, alpha);
    if (choicesShown_ < 0.6f) continue;
    char label[32];
    snprintf(label, sizeof(label), "%s%s", on ? kHeart : "", choices_[i]);
    const int w = gfx::Canvas::textWidth(f, label);
    c.drawText(f, label, r.x + (r.w - w) / 2, r.y + (r.h - f.height) / 2 + 1, on ? kWhite : kInk);
  }
}

}  // namespace ui::heroine
