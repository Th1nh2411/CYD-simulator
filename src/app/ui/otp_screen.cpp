#include "app/ui/otp_screen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/gfx/font.h"

namespace ui {

namespace {
const gfx::Color kBackground = gfx::rgb(14, 16, 22);
const gfx::Color kEdge = gfx::rgb(70, 74, 86);
const gfx::Color kText = gfx::rgb(226, 228, 236);
const gfx::Color kDim = gfx::rgb(140, 146, 160);
const gfx::Color kError = gfx::rgb(255, 110, 120);
const gfx::Color kButton = gfx::rgb(52, 58, 74);
const gfx::Color kPressed = gfx::rgb(90, 170, 255);
const gfx::Color kOk = gfx::rgb(60, 170, 100);

constexpr int kMargin = 8, kGap = 6;

void drawFrame(gfx::Canvas& c, const gfx::Rect& r, gfx::Color color) {
  c.fillRect(gfx::Rect(r.x, r.y, r.w, 1), color);
  c.fillRect(gfx::Rect(r.x, r.y + r.h - 1, r.w, 1), color);
  c.fillRect(gfx::Rect(r.x, r.y, 1, r.h), color);
  c.fillRect(gfx::Rect(r.x + r.w - 1, r.y, 1, r.h), color);
}

const char* const kKeyLabels[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK"};

}  // namespace

void OtpScreen::attach(gfx::Compositor& comp, int z, int screenW, int screenH) {
  width_ = screenW;
  height_ = screenH;
  screen_.owner = this;
  screen_.hide();
  comp.add(&screen_, z);
}

void OtpScreen::open(const char* deviceNo, const char* message) {
  snprintf(deviceNo_, sizeof(deviceNo_), "%s", deviceNo ? deviceNo : "");
  snprintf(message_, sizeof(message_), "%s", message ? message : "");
  digits_[0] = '\0';
  pressed_ = -1;
  open_ = true;
  screen_.show(gfx::Rect(0, 0, width_, height_));
}

void OtpScreen::close() {
  open_ = false;
  screen_.hide();
}

int OtpScreen::keypadTop() const { return std::min(118, height_ / 2); }

gfx::Rect OtpScreen::key(int i) const {
  const int top = keypadTop(), rows = 4, cols = 3;
  const int h = (height_ - top - kMargin - (rows - 1) * kGap) / rows;
  const int w = std::min((width_ - 2 * kMargin - (cols - 1) * kGap) / cols, 3 * h);
  const int left = (width_ - (cols * w + (cols - 1) * kGap)) / 2;
  return gfx::Rect(left + (i % cols) * (w + kGap), top + (i / cols) * (h + kGap), w, h);
}

gfx::Rect OtpScreen::digitBox(int i) const {
  const int w = std::min(34, (width_ - 2 * kMargin) / kDigits - kGap), h = 34;
  const int left = (width_ - (kDigits * w + (kDigits - 1) * kGap)) / 2;
  return gfx::Rect(left + i * (w + kGap), keypadTop() - h - 10, w, h);
}

gfx::Rect OtpScreen::closeBox() const { return gfx::Rect(width_ - 46, 4, 42, 22); }

OtpScreen::Action OtpScreen::onTap(int x, int y) {
  if (!open_) return Action::None;
  if (closeBox().inflate(4).contains(x, y)) {
    close();
    return Action::Cancel;
  }
  for (int i = 0; i < kKeys; i++) {
    if (!key(i).inflate(kGap / 2).contains(x, y)) continue;
    pressed_ = i;
    const size_t n = strlen(digits_);
    if (i == 9) {
      if (n) digits_[n - 1] = '\0';
    } else if (i == 11) {
      screen_.changed();
      if (n == kDigits) {
        close();
        return Action::Submit;
      }
      return Action::None;
    } else if (n < kDigits) {
      digits_[n] = kKeyLabels[i][0];
      digits_[n + 1] = '\0';
    }
    screen_.changed();
    return Action::None;
  }
  return Action::None;
}

void OtpScreen::Screen::draw(gfx::Canvas& c) const {
  const OtpScreen& o = *owner;
  const gfx::Font& f = gfx::kFontKo11;
  c.fillRect(c.area(), kBackground);
  c.drawText(f, "AI Buddy 로그인", kMargin, 8, kText);
  char line[64];
  snprintf(line, sizeof(line), "기기 %s · 백오피스에 나온 6자리 OTP", o.deviceNo_);
  if (gfx::Canvas::textWidth(f, line) > o.width_ - 2 * kMargin) snprintf(line, sizeof(line), "%s · 6자리 OTP", o.deviceNo_);
  c.drawText(f, line, kMargin, 28, kDim);
  if (o.message_[0]) c.drawText(f, o.message_, kMargin, 46, kError);

  const gfx::Rect x = o.closeBox();
  drawFrame(c, x, kEdge);
  c.drawText(f, "닫기", x.x + (x.w - gfx::Canvas::textWidth(f, "닫기")) / 2, x.y + 4, kText);

  const size_t n = strlen(o.digits_);
  for (int i = 0; i < kDigits; i++) {
    const gfx::Rect b = o.digitBox(i);
    drawFrame(c, b, int(n) == i ? kPressed : kEdge);
    if (i < int(n)) {
      const char d[2] = {o.digits_[i], '\0'};
      c.drawText(d, b.x + (b.w - gfx::Canvas::textWidth(d, 3)) / 2, b.y + (b.h - gfx::Canvas::textHeight(3)) / 2,
                 kText, 3);
    }
  }
  for (int i = 0; i < kKeys; i++) {
    const gfx::Rect k = o.key(i);
    const bool ready = i == 11 && n == size_t(kDigits);
    c.fillRect(k, i == o.pressed_ ? kPressed : ready ? kOk : kButton);
    const char* label = kKeyLabels[i];
    c.drawText(label, k.x + (k.w - gfx::Canvas::textWidth(label, 2)) / 2,
               k.y + (k.h - gfx::Canvas::textHeight(2)) / 2, kText, 2);
  }
}

}  // namespace ui
