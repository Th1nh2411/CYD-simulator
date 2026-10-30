#include "app/ui/layers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/core/utf8.h"

namespace ui {

namespace {
const gfx::Color kCardFill = gfx::rgb(28, 30, 48);
const gfx::Color kNotifyAccent = gfx::rgb(255, 200, 80);
const gfx::Color kPromptAccent = gfx::rgb(120, 210, 255);
const gfx::Color kBodyText = gfx::rgb(232, 232, 240);
const gfx::Color kButtonFill = gfx::rgb(52, 56, 88);
const gfx::Color kButtonText = gfx::rgb(232, 232, 240);
const gfx::Color kPrimaryText = gfx::rgb(20, 22, 36);
const gfx::Color kWhite = gfx::rgb(255, 255, 255);

void copyText(char* dst, size_t size, const char* src, size_t maxChars) {
  size_t n = std::min(strlen(src), std::min(maxChars, size - 1));
  memcpy(dst, src, n);
  dst[n] = '\0';
}
}  // namespace

// --- BackgroundLayer ---

void BackgroundLayer::configure(const gfx::Rect& screen, gfx::Color color) {
  color_ = color;
  setBounds(screen);
  invalidate();
}

void BackgroundLayer::draw(gfx::Canvas& c) const { c.fillRect(bounds(), color_); }

// --- TextLayer ---

void TextLayer::set(const char* text, int x, int y, gfx::Color color, int scale) {
  gfx::Rect b(x, y, gfx::Canvas::textWidth(text, scale), gfx::Canvas::textHeight(scale));
  if (strcmp(text, text_) == 0 && color == color_ && scale == scale_ && b == bounds()) return;
  copyText(text_, sizeof(text_), text, sizeof(text_));
  color_ = color;
  scale_ = scale;
  if (b == bounds()) invalidate();
  else setBounds(b);
}

void TextLayer::draw(gfx::Canvas& c) const {
  c.drawText(text_, bounds().x, bounds().y, color_, scale_);
}

// --- ClockLayer ---

void ClockLayer::configure(int screenW, int margin, int scale) {
  screenW_ = screenW;
  margin_ = margin;
  scale_ = scale;
  minute_ = -1;
  setVisible(false);
}

void ClockLayer::set(int minuteOfDay) {
  if (minuteOfDay == minute_) return;
  minute_ = minuteOfDay;
  if (minute_ < 0) {
    setVisible(false);
    return;
  }
  snprintf(text_, sizeof(text_), "%02d:%02d", minute_ / 60 % 24, minute_ % 60);
  int w = gfx::Canvas::textWidth(text_, scale_) + scale_, h = gfx::Canvas::textHeight(scale_) + scale_;
  gfx::Rect b(screenW_ - margin_ - w, margin_ / 2, w, h);
  setVisible(true);
  if (b == bounds()) invalidate();
  else setBounds(b);
}

void ClockLayer::draw(gfx::Canvas& c) const {
  const gfx::Rect& b = bounds();
  c.drawText(text_, b.x + scale_, b.y + scale_, gfx::rgb(16, 18, 32), scale_);
  c.drawText(text_, b.x, b.y, gfx::rgb(240, 240, 248), scale_);
}

// --- CardLayer ---

int CardLayer::Face::height() const {
  return font ? font->height * scale : gfx::Canvas::textHeight(scale);
}

int CardLayer::Face::advance(const char* s, size_t bytes) const {
  return font ? gfx::advanceOf(*font, s, bytes) * scale : int(bytes) * 6 * scale;  // 5x7: ASCII only
}

int CardLayer::Face::width(const char* s) const {
  return font ? gfx::Canvas::textWidth(*font, s, scale) : gfx::Canvas::textWidth(s, scale);
}

void CardLayer::Face::draw(gfx::Canvas& c, const char* s, int x, int y, gfx::Color color) const {
  if (font) c.drawText(*font, s, x, y, color, scale);
  else c.drawText(s, x, y, color, scale);
}

size_t CardLayer::Face::fit(const char* s, int maxWidth, size_t maxBytes) const {
  size_t n = 0;
  while (s[n] && n < maxBytes) {
    const char* next = s + n;
    core::nextCodePoint(next);
    size_t m = size_t(next - s);
    if (m > maxBytes || (n > 0 && advance(s, m) > maxWidth)) break;
    n = m;
  }
  return n;
}

namespace {
bool isAscii(const char* s) {
  for (; *s; s++) {
    if (uint8_t(*s) >= 0x80) return false;
  }
  return true;
}
}  // namespace

void CardLayer::setContent(const engine::CardContent& content, int width, int height,
                           int titleScale, int bodyScale) {
  kind_ = content.kind;
  w_ = width;
  h_ = height;
  titleScale_ = titleScale;
  pressed_ = -1;
  const int pad = 5 * titleScale, gap = 3 * titleScale;

  optionCount_ = kind_ == engine::CardKind::Prompt ? std::min<int>(content.optionCount, engine::kMaxOptions) : 0;
  bool ascii = isAscii(content.title) && isAscii(content.body);
  for (int i = 0; i < optionCount_; i++) ascii = ascii && isAscii(content.options[i]);
  const gfx::Font* font = ascii ? nullptr : &gfx::kFontKo11;
  titleFace_ = Face{font, ascii ? titleScale : 1};
  bodyFace_ = Face{font, ascii ? bodyScale : 1};
  buttonFace_ = titleFace_;

  size_t n = titleFace_.fit(content.title, w_ - 2 * pad, sizeof(title_) - 1);
  memcpy(title_, content.title, n);
  title_[n] = '\0';

  // Buttons along the bottom: one row when wide enough, else stacked
  int buttonsTop = h_ - pad;
  if (optionCount_ > 0) {
    int inner = w_ - 2 * pad;
    int longest = 0;
    for (int i = 0; i < optionCount_; i++) {
      core::copyUtf8(options_[i], sizeof(options_[i]), content.options[i]);
      longest = std::max(longest, buttonFace_.advance(options_[i], strlen(options_[i])));
    }
    bool row = inner >= 200;
    int bw = row ? (inner - (optionCount_ - 1) * gap) / optionCount_ : inner;
    if (ascii && longest > bw - 8) buttonFace_.scale = 1;
    int bh = buttonFace_.height() + 7 * buttonFace_.scale;
    for (int i = 0; i < optionCount_; i++) {
      if (row) {
        buttons_[i] = gfx::Rect(pad + i * (bw + gap), h_ - pad - bh, bw, bh);
      } else {
        buttons_[i] = gfx::Rect(pad, h_ - pad - (optionCount_ - i) * (bh + gap) + gap, bw, bh);
      }
    }
    buttonsTop = buttons_[0].y - gap;
  }

  // Body text fills what is left between the title and the buttons
  int bodyTop = pad + titleFace_.height() + gap;
  int lineH = bodyFace_.height() + bodyFace_.lineGap();
  int maxLines = std::max(0, std::min(kMaxLines, (buttonsTop - bodyTop) / lineH));
  lineCount_ = wrap(content.body, w_ - 2 * pad, maxLines);

  gfx::Rect b(x_, y_, w_, h_);
  if (b == bounds()) invalidate();
  else setBounds(b);
}

void CardLayer::setPosition(int x, int y) {
  x_ = x;
  y_ = y;
  setBounds(gfx::Rect(x_, y_, w_, h_));
}

void CardLayer::setPressed(int option) {
  if (option == pressed_) return;
  pressed_ = option;
  invalidate();
}

int CardLayer::optionAt(int x, int y) const {
  for (int i = 0; i < optionCount_; i++) {
    gfx::Rect r = buttons_[i];
    r.x = int16_t(r.x + x_);
    r.y = int16_t(r.y + y_);
    if (r.inflate(4).contains(x, y)) return i;
  }
  return -1;
}

int CardLayer::wrap(const char* text, int maxWidth, int maxLines) {
  const Face& f = bodyFace_;
  int count = 0;
  const char* p = text;
  while (*p && count < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;
    size_t take = f.fit(p, maxWidth, kLineBytes - 1);
    if (p[take]) {
      size_t cut = take;
      while (cut > 0 && p[cut] != ' ') cut--;
      if (cut > 0) take = cut;  // break at the last space that fits
    }
    memcpy(lines_[count], p, take);
    lines_[count][take] = '\0';
    count++;
    p += take;
  }
  while (*p == ' ') p++;
  if (*p && count == maxLines && count > 0) {  // truncated: end the last line with "..."
    char* last = lines_[count - 1];
    size_t keep = strlen(last);
    while (keep > 0 && (keep + 3 >= kLineBytes || f.advance(last, keep) + f.advance("...", 3) > maxWidth)) {
      keep--;
      while (keep > 0 && (uint8_t(last[keep]) & 0xC0) == 0x80) keep--;  // whole characters only
    }
    strcpy(last + keep, "...");
  }
  return count;
}

void CardLayer::draw(gfx::Canvas& c) const {
  const gfx::Rect& b = bounds();
  const gfx::Color accent = kind_ == engine::CardKind::Prompt ? kPromptAccent : kNotifyAccent;
  const int radius = 5 * titleScale_, pad = 5 * titleScale_, gap = 3 * titleScale_;
  c.fillRoundRect(b, radius, accent);
  c.fillRoundRect(b.inflate(-std::max(1, titleScale_)), radius - 1, kCardFill);

  int y = b.y + pad;
  titleFace_.draw(c, title_, b.x + pad, y, accent);
  y += titleFace_.height() + gap;
  for (int i = 0; i < lineCount_; i++) {
    bodyFace_.draw(c, lines_[i], b.x + pad, y, kBodyText);
    y += bodyFace_.height() + bodyFace_.lineGap();
  }

  const Face& bf = buttonFace_;
  for (int i = 0; i < optionCount_; i++) {
    gfx::Rect r = buttons_[i];
    r.x = int16_t(r.x + b.x);
    r.y = int16_t(r.y + b.y);
    bool primary = i == 0;
    gfx::Color fill = primary ? accent : kButtonFill;
    if (i == pressed_) fill = gfx::mix(fill, kWhite, 16);
    c.fillRoundRect(r, 4 * bf.scale, fill);
    int tw = bf.width(options_[i]);
    int th = bf.height();
    bf.draw(c, options_[i], r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2 + bf.scale,
            primary ? kPrimaryText : kButtonText);
  }
}

}  // namespace ui
