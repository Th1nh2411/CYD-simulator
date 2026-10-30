#include "app/ui/settings_menu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/gfx/font.h"

namespace ui {

namespace {
const gfx::Color kBackground = gfx::rgb(14, 16, 22);
const gfx::Color kEdge = gfx::rgb(70, 74, 86);
const gfx::Color kText = gfx::rgb(226, 228, 236);
const gfx::Color kHint = gfx::rgb(255, 214, 90);
const gfx::Color kGood = gfx::rgb(90, 230, 120);
const gfx::Color kButton = gfx::rgb(52, 58, 74);

constexpr int kInfoLines = 3;

void copy(char* dst, size_t size, const char* src) { snprintf(dst, size, "%s", src ? src : ""); }

void drawFrame(gfx::Canvas& c, const gfx::Rect& r, gfx::Color color) {
  c.fillRect(gfx::Rect(r.x, r.y, r.w, 1), color);
  c.fillRect(gfx::Rect(r.x, r.y + r.h - 1, r.w, 1), color);
  c.fillRect(gfx::Rect(r.x, r.y, 1, r.h), color);
  c.fillRect(gfx::Rect(r.x + r.w - 1, r.y, 1, r.h), color);
}
}  // namespace

void SettingsMenu::attach(gfx::Compositor& comp, int z, int screenW, int screenH) {
  width_ = screenW;
  height_ = screenH;
  screen_.owner = this;
  screen_.hide();
  comp.add(&screen_, z);
}

void SettingsMenu::open(bool hasTouch) {
  count_ = 0;
  if (hasTouch) {
    items_[count_++] = Action::TouchCal;
    items_[count_++] = Action::LongPressCal;
    items_[count_++] = Action::DragCal;
  }
  items_[count_++] = Action::ToggleAp;
  items_[count_++] = Action::Close;
  selected_ = 0;
  open_ = true;
  screen_.show(gfx::Rect(0, 0, width_, height_));
}

void SettingsMenu::close() {
  open_ = false;
  screen_.hide();
}

void SettingsMenu::setAp(bool on, bool locked) {
  if (on == apOn_ && locked == apLocked_) return;
  apOn_ = on;
  apLocked_ = locked;
  if (open_) screen_.changed();
}

void SettingsMenu::setApInfo(const char* ssid, const char* password, const char* address) {
  copy(ssid_, sizeof(ssid_), ssid);
  copy(password_, sizeof(password_), password);
  copy(address_, sizeof(address_), address);
  if (open_ && apOn_) screen_.changed();
}

SettingsMenu::Action SettingsMenu::onTap(int x, int y) {
  if (!open_) return Action::None;
  for (int i = 0; i < count_; i++) {
    if (!row(i).inflate(4).contains(x, y)) continue;
    selected_ = i;
    screen_.changed();
    return items_[i];
  }
  return Action::None;
}

SettingsMenu::Action SettingsMenu::onButton(bool longPress) {
  if (!open_) return Action::None;
  if (longPress) return items_[selected_];
  selected_ = (selected_ + 1) % count_;
  screen_.changed();
  return Action::None;
}

gfx::Rect SettingsMenu::row(int i) const {
  const gfx::Font& f = gfx::kFontKo11;
  const int scale = width_ >= 480 ? 2 : 1;
  // Title and hint on top, room for the AP details at the bottom, the rows share what is left
  const int top = height_ / 16 + (f.height + 4) * scale + (f.height + 3) + 4;
  const int avail = height_ - (kInfoLines * (f.height + 3) + 8) - top;
  const int gap = std::max(4, height_ / 48);
  const int h = std::min(height_ / 7, (avail - gap * (count_ - 1)) / count_);
  const int w = width_ * 3 / 4;
  return gfx::Rect((width_ - w) / 2, top + i * (h + gap), w, h);
}

void SettingsMenu::label(int i, char* out, size_t size) const {
  const int number = i + 1;
  switch (items_[i]) {
    case Action::TouchCal: snprintf(out, size, "%d. 터치 보정", number); break;
    case Action::LongPressCal: snprintf(out, size, "%d. 롱 프레스 보정", number); break;
    case Action::DragCal: snprintf(out, size, "%d. 드래그 보정", number); break;
    case Action::ToggleAp:
      snprintf(out, size, "%d. AP 모드 %s%s", number, apOn_ ? "켜짐" : "꺼짐", apLocked_ ? " (필수)" : "");
      break;
    default: snprintf(out, size, "닫기"); break;
  }
}

void SettingsMenu::Screen::draw(gfx::Canvas& c) const {
  const SettingsMenu& s = *owner;
  const gfx::Font& f = gfx::kFontKo11;
  const int scale = s.width_ >= 480 ? 2 : 1;
  c.fillRect(bounds(), kBackground);
  drawFrame(c, gfx::Rect(0, 0, s.width_, s.height_), kEdge);

  const int x = s.width_ / 16;
  int y = s.height_ / 16;
  c.drawText(f, "설정", x, y, kHint, scale);
  y += (f.height + 4) * scale;
  c.drawText(f, "BOOT: 짧게 = 다음, 길게 = 선택", x, y, kText);

  for (int i = 0; i < s.count_; i++) {
    const gfx::Rect r = s.row(i);
    char text[48];
    s.label(i, text, sizeof(text));
    c.fillRoundRect(r, 6, kButton);
    if (i == s.selected_) drawFrame(c, r.inflate(2), kHint);
    int tw = gfx::Canvas::textWidth(f, text, scale);
    c.drawText(f, text, r.x + (r.w - tw) / 2, r.y + (r.h - f.height * scale) / 2, kText, scale);
  }

  if (s.apOn_) {
    const int lineH = f.height + 3;
    int iy = s.height_ - kInfoLines * lineH - 6;
    char line[64];
    snprintf(line, sizeof(line), "Wi-Fi: %s", s.ssid_);
    c.drawText(f, line, x, iy, kGood);
    snprintf(line, sizeof(line), "비밀번호: %s", s.password_);
    c.drawText(f, line, x, iy + lineH, kGood);
    snprintf(line, sizeof(line), "주소: %s", s.address_);
    c.drawText(f, line, x, iy + 2 * lineH, kGood);
  }
}

}  // namespace ui
