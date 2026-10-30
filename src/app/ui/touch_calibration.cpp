#include "app/ui/touch_calibration.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "app/gfx/font.h"

namespace ui {

namespace {
const gfx::Color kBackground = gfx::rgb(14, 16, 22);
const gfx::Color kEdge = gfx::rgb(70, 74, 86);
const gfx::Color kTarget = gfx::rgb(255, 255, 255);
const gfx::Color kText = gfx::rgb(226, 228, 236);
const gfx::Color kHint = gfx::rgb(255, 214, 90);
const gfx::Color kGood = gfx::rgb(90, 230, 120);
const gfx::Color kRaw = gfx::rgb(255, 64, 64);
const gfx::Color kButton = gfx::rgb(52, 58, 74);
const gfx::Color kSave = gfx::rgb(40, 140, 80);

constexpr int kRoundTaps = 9;
constexpr double kSettledPx = 1.5;  // largest fit change over a round to call it settled
constexpr double kScaleSpreadPx = 60;  // fit the scale too once the taps spread this much

// Plausible saved values: anything else would leave gestures unusable
constexpr int kMinSlopPx = 4, kMaxSlopPx = 80, kMinSavedHoldMs = 200, kMaxSavedHoldMs = 2000;
constexpr int kStillMaxPx = 48;  // a press on the cross wandering further was a drag: not counted

// Long press: this many taps, then holds; a "tap" this long or a "hold" this
// short was a slip and is not counted
constexpr int kQuickPresses = 8, kHoldPresses = 5;
constexpr int kQuickLimitMs = 1000, kHoldLeastMs = 200;
constexpr int kHoldMarginMs = 100, kMinHoldMs = 250, kMaxHoldMs = 1500;

// Drag: this many presses on the cross
constexpr int kStillPresses = 10;
constexpr int kSuggestMinPx = 10, kSuggestMaxPx = 40;

int roundInt(double v) { return int(std::lround(v)); }

// Thin cross with a gap in the middle, so the finger has a clear point to aim at
void drawCross(gfx::Canvas& c, int x, int y, int scale) {
  int arm = 8 * scale + 8, gap = 2 * scale + 1;
  c.fillRect(gfx::Rect(x - arm, y, arm - gap, 1), kTarget);
  c.fillRect(gfx::Rect(x + gap + 1, y, arm - gap, 1), kTarget);
  c.fillRect(gfx::Rect(x, y - arm, 1, arm - gap), kTarget);
  c.fillRect(gfx::Rect(x, y + gap + 1, 1, arm - gap), kTarget);
  c.fillRect(gfx::Rect(x, y, 1, 1), kTarget);
}

void drawFrame(gfx::Canvas& c, const gfx::Rect& r, gfx::Color color) {
  c.fillRect(gfx::Rect(r.x, r.y, r.w, 1), color);
  c.fillRect(gfx::Rect(r.x, r.y + r.h - 1, r.w, 1), color);
  c.fillRect(gfx::Rect(r.x, r.y, 1, r.h), color);
  c.fillRect(gfx::Rect(r.x + r.w - 1, r.y, 1, r.h), color);
}

void drawLabel(gfx::Canvas& c, const gfx::Rect& b, const char* label, gfx::Color fill, gfx::Color text, int scale) {
  const gfx::Font& f = gfx::kFontKo11;
  c.fillRoundRect(b, 6, fill);
  int tw = gfx::Canvas::textWidth(f, label, scale);
  c.drawText(f, label, b.x + (b.w - tw) / 2, b.y + (b.h - f.height * scale) / 2, text, scale);
}
}  // namespace

void applyTouchCalibration(const hal::TouchCalibration& cal, int screenW, int screenH, int& x, int& y) {
  x = std::clamp(roundInt(cal.ax * x + cal.bx), 0, screenW - 1);
  y = std::clamp(roundInt(cal.ay * y + cal.by), 0, screenH - 1);
}

hal::TouchCalibration rotateTouchCalibration(hal::TouchCalibration cal, int quarterTurns, int width, int height) {
  for (int i = 0; i < (quarterTurns & 3); i++) {
    // Raw and corrected points alike: x' = height-1-y, y' = x
    const hal::TouchCalibration c = cal;
    cal.ax = c.ay;
    cal.bx = float(height - 1) * (1 - c.ay) - c.by;
    cal.ay = c.ax;
    cal.by = c.bx;
    std::swap(width, height);
  }
  return cal;
}

hal::TouchCalibration mirrorTouchCalibration(hal::TouchCalibration cal, int width) {
  // Raw and corrected points alike: x' = width-1-x
  cal.bx = float(width - 1) * (1 - cal.ax) - cal.bx;
  return cal;
}

size_t touchCalibrationToJson(const hal::TouchCalibration& cal, char* out, size_t size) {
  JsonDocument doc;
  doc["ax"] = cal.ax;
  doc["bx"] = cal.bx;
  doc["ay"] = cal.ay;
  doc["by"] = cal.by;
  doc["slop"] = cal.slopPx;
  doc["hold"] = cal.longPressMs;
  size_t n = serializeJson(doc, out, size);
  return n < size ? n : 0;
}

bool touchCalibrationFromJson(const char* json, hal::TouchCalibration& cal) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["ax"].is<float>() || !doc["bx"].is<float>() || !doc["ay"].is<float>() ||
      !doc["by"].is<float>()) {
    return false;
  }
  // Saved before the gestures were calibrated: they keep the current values
  hal::TouchCalibration c{doc["ax"].as<float>(), doc["bx"].as<float>(), doc["ay"].as<float>(), doc["by"].as<float>(),
                          doc["slop"] | cal.slopPx, doc["hold"] | cal.longPressMs};
  // A broken value would make the screen untouchable, so only plausible ones are taken
  auto plausible = [](float a, float b) { return a > 0.5f && a < 2.0f && std::fabs(b) < 400; };
  if (!plausible(c.ax, c.bx) || !plausible(c.ay, c.by) || c.slopPx < kMinSlopPx || c.slopPx > kMaxSlopPx ||
      c.longPressMs < kMinSavedHoldMs || c.longPressMs > kMaxSavedHoldMs) {
    return false;
  }
  cal = c;
  return true;
}

void TouchCalibrationScreen::AxisFit::add(double measured, double target) {
  n++, m += measured, mm += measured * measured, t += target, mt += measured * target;
  double var = n * mm - m * m;
  if (n >= 4 && var > n * n * kScaleSpreadPx * kScaleSpreadPx) {
    a = (n * mt - m * t) / var;
    b = (t - a * m) / n;
  } else {
    a = 1;
    b = (t - m) / n;
  }
}

void TouchCalibrationScreen::attach(gfx::Compositor& comp, int z, int screenW, int screenH) {
  width_ = screenW;
  height_ = screenH;
  screen_.owner = this;
  screen_.hide();
  comp.add(&screen_, z);
}

void TouchCalibrationScreen::start(const hal::TouchCalibration& current, Kind kind) {
  previous_ = result_ = current;
  mode_ = kind;
  restart();
  state_ = State::Running;
  screen_.show(gfx::Rect(0, 0, width_, height_));
}

void TouchCalibrationScreen::onButton() {
  if (state_ == State::Running) state_ = State::Cancelled;
}

void TouchCalibrationScreen::finish() {
  screen_.hide();
  state_ = State::Off;
}

void TouchCalibrationScreen::restart() {
  fx_ = AxisFit();
  fy_ = AxisFit();
  taps_ = next_ = 0;
  rms_ = 0;
  roundStart_[0] = 1, roundStart_[1] = 0, roundStart_[2] = 1, roundStart_[3] = 0;
  converged_ = false;
  lastRawX_ = lastRawY_ = lastX_ = lastY_ = -1;
  pressing_ = onCross_ = false;
  wander_ = traceN_ = 0;
  heldMs_ = 0;
  quick_ = quickMax_ = holds_ = holdMin_ = stills_ = stillMax_ = 0;
  screen_.changed();
}

void TouchCalibrationScreen::target(int i, int& x, int& y) const {
  // Corners, middle and centre, in an order that jumps across the screen
  static constexpr uint8_t kOrder[kTargets][2] = {{0, 0}, {1, 1}, {2, 2}, {2, 0}, {0, 2},
                                                  {1, 0}, {0, 1}, {2, 1}, {1, 2}};
  static constexpr int kFracX[3] = {1, 6, 11}, kFracY[3] = {1, 4, 7};  // in 12ths / 8ths
  x = width_ * kFracX[kOrder[i][0]] / 12;
  y = height_ * kFracY[kOrder[i][1]] / 8;
}

void TouchCalibrationScreen::pressTarget(int& x, int& y) const {
  x = width_ / 4;
  y = height_ * 9 / 16;
}

gfx::Rect TouchCalibrationScreen::button(int i) const {
  int w = width_ / 4, h = height_ / 7, gap = width_ / 32;
  int x0 = (width_ - 3 * w - 2 * gap) / 2;
  int y = mode_ == Kind::Touch ? height_ * 3 / 5 : height_ - h - height_ / 20;
  return gfx::Rect(x0 + i * (w + gap), y, w, h);
}

void TouchCalibrationScreen::onTouch(bool touching, int rawX, int rawY, const TouchGesture& gesture,
                                     const TouchGesture::Event& g, uint32_t now) {
  if (state_ != State::Running) return;
  if (mode_ == Kind::LongPress || mode_ == Kind::Drag) trackPress(touching, rawX, rawY, gesture.pressed(), now);
  if (g.kind != TouchGesture::Kind::Tap) return;
  if (mode_ == Kind::Touch) {
    onTap(g.x, g.y);
    return;
  }
  // The buttons, hit through the calibration in use
  int x = g.x, y = g.y;
  applyTouchCalibration(previous_, width_, height_, x, y);
  hit(x, y);
}

void TouchCalibrationScreen::onTap(int rawX, int rawY) {
  lastRawX_ = rawX, lastRawY_ = rawY;
  lastX_ = roundInt(fx_.apply(rawX)), lastY_ = roundInt(fy_.apply(rawY));
  if (converged_) {
    hit(lastX_, lastY_);
    screen_.changed();
    return;
  }

  int tx, ty;
  target(next_, tx, ty);
  int reach = width_ / 8;  // farther than this from the target is a slip, not a calibration sample
  if (std::abs(rawX - tx) > reach || std::abs(rawY - ty) > reach || taps_ >= kMaxTaps) {
    screen_.changed();
    return;
  }
  mx_[taps_] = int16_t(rawX), my_[taps_] = int16_t(rawY), tx_[taps_] = int16_t(tx), ty_[taps_] = int16_t(ty);
  taps_++;
  fx_.add(rawX, tx);
  fy_.add(rawY, ty);
  lastX_ = roundInt(fx_.apply(rawX)), lastY_ = roundInt(fy_.apply(rawY));

  double sum = 0;
  for (int i = 0; i < taps_; i++) {
    double dx = fx_.apply(mx_[i]) - tx_[i], dy = fy_.apply(my_[i]) - ty_[i];
    sum += dx * dx + dy * dy;
  }
  rms_ = std::sqrt(sum / taps_);

  if (taps_ % kRoundTaps == 0) {  // a full round: has the fit settled?
    // Scale changes count as the shift they cause halfway across the screen
    double moved = std::max({std::fabs(fx_.b - roundStart_[1]), std::fabs(fy_.b - roundStart_[3]),
                             std::fabs(fx_.a - roundStart_[0]) * width_ / 2,
                             std::fabs(fy_.a - roundStart_[2]) * height_ / 2});
    converged_ = taps_ >= 2 * kRoundTaps && moved < kSettledPx;
    roundStart_[0] = fx_.a, roundStart_[1] = fx_.b, roundStart_[2] = fy_.a, roundStart_[3] = fy_.b;
  }
  next_ = (next_ + 1) % kTargets;
  screen_.changed();
}

void TouchCalibrationScreen::hit(int x, int y) {
  // Touch calibration only brings its buttons up once settled
  if (mode_ == Kind::Touch && !converged_) return;
  int b = -1;
  for (int i = 0; i < 3; i++) {
    if (button(i).inflate(4).contains(x, y)) b = i;
  }
  if (b == 1) restart();
  if (b == 2) state_ = State::Cancelled;
  if (b != 0 || !done()) return;
  result_ = previous_;
  if (mode_ == Kind::Touch) {
    result_.ax = float(fx_.a), result_.bx = float(fx_.b), result_.ay = float(fy_.a), result_.by = float(fy_.b);
  } else if (mode_ == Kind::LongPress) {
    result_.longPressMs = suggestedLongPress();
  } else {
    result_.slopPx = suggestedSlop();
  }
  state_ = State::Saved;
}

bool TouchCalibrationScreen::done() const {
  switch (mode_) {
    case Kind::Touch: return converged_;
    case Kind::LongPress: return holds_ >= kHoldPresses;
    case Kind::Drag: return stills_ >= kStillPresses;
  }
  return false;
}

void TouchCalibrationScreen::trackPress(bool touching, int rawX, int rawY, bool pressed, uint32_t now) {
  bool changed = false;
  if (touching) {
    int x = rawX, y = rawY;
    applyTouchCalibration(previous_, width_, height_, x, y);
    if (!pressing_) {
      int tx, ty, reach = width_ / 8;
      pressTarget(tx, ty);
      pressing_ = true;
      onCross_ = std::abs(x - tx) <= reach && std::abs(y - ty) <= reach;
      downX_ = rawX, downY_ = rawY;
      downMs_ = now;
      wander_ = traceN_ = 0;
      changed = true;
    }
    touchMs_ = now;
    // Same measure as the gesture's: the larger axis distance from where it landed
    const bool drag = mode_ == Kind::Drag;  // only the drag shows the press as it goes
    int wander = std::max({wander_, std::abs(rawX - downX_), std::abs(rawY - downY_)});
    if (wander != wander_) changed |= drag;
    wander_ = wander;
    bool apart = traceN_ == 0 ||
                 std::max(std::abs(x - traceX_[traceN_ - 1]), std::abs(y - traceY_[traceN_ - 1])) >= 3;
    if (apart && traceN_ < kTrace) {
      traceX_[traceN_] = int16_t(x), traceY_[traceN_] = int16_t(y);
      traceN_++;
      changed |= drag;
    }
  }
  // The press ends when the gesture lets go: a long press's release makes no event
  if (pressing_ && !pressed) {
    pressing_ = false;
    heldMs_ = touchMs_ - downMs_;
    if (onCross_ && wander_ <= kStillMaxPx) pressEnded();
    changed = true;
  }
  if (changed) screen_.changed();
}

void TouchCalibrationScreen::pressEnded() {
  const int ms = int(heldMs_);
  if (mode_ == Kind::Drag) {
    if (stills_ < kStillPresses) {
      stills_++;
      stillMax_ = std::max(stillMax_, wander_);
    }
  } else if (quick_ < kQuickPresses) {
    if (ms < kQuickLimitMs) {
      quick_++;
      quickMax_ = std::max(quickMax_, ms);
    }
  } else if (holds_ < kHoldPresses && ms >= kHoldLeastMs) {
    holdMin_ = holds_ ? std::min(holdMin_, ms) : ms;
    holds_++;
  }
}

int TouchCalibrationScreen::suggestedLongPress() const {
  // Halfway between the slowest tap and the quickest hold, and well past the taps
  int ms = std::max((quickMax_ + holdMin_) / 2, quickMax_ + kHoldMarginMs);
  return std::clamp(ms, kMinHoldMs, kMaxHoldMs);
}

int TouchCalibrationScreen::suggestedSlop() const {
  // Half again the most a still finger wandered, and a little more
  return std::clamp((3 * stillMax_ + 1) / 2 + 2, kSuggestMinPx, kSuggestMaxPx);
}

void TouchCalibrationScreen::Screen::draw(gfx::Canvas& c) const {
  const TouchCalibrationScreen& s = *owner;
  c.fillRect(bounds(), kBackground);
  drawFrame(c, gfx::Rect(0, 0, s.width_, s.height_), kEdge);
  switch (s.mode_) {
    case Kind::Touch: s.drawTouch(c); break;
    case Kind::LongPress: s.drawLongPress(c); break;
    case Kind::Drag: s.drawDrag(c); break;
  }
}

void TouchCalibrationScreen::drawButtons(gfx::Canvas& c) const {
  static const char* const kLabels[3] = {"저장", "다시", "취소"};
  const int scale = width_ >= 480 ? 2 : 1;
  const bool ready = done();
  for (int i = 0; i < 3; i++) {
    bool save = i == 0;
    drawLabel(c, button(i), kLabels[i], save && ready ? kSave : kButton, save && !ready ? kEdge : kText, scale);
  }
}

void TouchCalibrationScreen::drawTouch(gfx::Canvas& c) const {
  const gfx::Font& f = gfx::kFontKo11;
  // Body text stays 1x so every line fits in half of a 320 px screen
  const int w = width_, h = height_, scale = w >= 480 ? 2 : 1, lineH = f.height + 3;
  char line[64];
  int tx = w / 2, ty = h / 2;
  if (!converged_) {
    target(next_, tx, ty);
    drawCross(c, tx, ty, scale);
  }

  // Status: in the half away from the target, or above the buttons once settled
  int x = converged_ ? w / 8 : (tx < w / 2 ? w / 2 + w / 24 : w / 24);
  int y = converged_ ? h / 8 : (ty < h / 2 ? h / 2 + h / 10 : h / 10);
  c.drawText(f, "터치 보정", x, y, kHint, scale);
  y += (f.height + 3) * scale;
  c.drawText(f, converged_ ? "안정됐어요" : "가운데를 누르세요", x, y, converged_ ? kGood : kText);
  y += lineH;
  snprintf(line, sizeof(line), "탭 %d", taps_);
  c.drawText(f, line, x, y, kText);
  y += lineH;
  snprintf(line, sizeof(line), "x = %.3f*X %+.1f", fx_.a, fx_.b);
  c.drawText(f, line, x, y, kText);
  y += lineH;
  snprintf(line, sizeof(line), "y = %.3f*Y %+.1f", fy_.a, fy_.b);
  c.drawText(f, line, x, y, kText);
  y += lineH;
  if (taps_ > 0) {
    snprintf(line, sizeof(line), "오차 %.1f px", rms_);
    c.drawText(f, line, x, y, kText);
  }

  if (converged_) drawButtons(c);

  // Last tap: where it was read (red) and where the current fit puts it (green)
  if (lastRawX_ >= 0) {
    int d = 2 * scale;
    c.fillRect(gfx::Rect(lastRawX_ - d, lastRawY_ - d, 2 * d + 1, 2 * d + 1), kRaw);
    c.fillRect(gfx::Rect(lastX_ - d, lastY_ - d, 2 * d + 1, 2 * d + 1), kGood);
  }
}

void TouchCalibrationScreen::drawLongPress(gfx::Canvas& c) const {
  const gfx::Font& f = gfx::kFontKo11;
  const int w = width_, h = height_, scale = w >= 480 ? 2 : 1, lineH = f.height + 3;
  char line[64];
  int x = w / 24, y = h / 14;
  c.drawText(f, "롱 프레스 보정", x, y, kHint, scale);
  y += (f.height + 3) * scale;
  const bool tapping = quick_ < kQuickPresses;
  if (tapping) {
    snprintf(line, sizeof(line), "1) +를 평소처럼 톡 누르세요 (%d/%d)", quick_, kQuickPresses);
  } else {
    snprintf(line, sizeof(line), "2) +를 길게 꾹 눌렀다 떼세요 (%d/%d)", std::min(holds_, kHoldPresses),
             kHoldPresses);
  }
  c.drawText(f, line, x, y, done() ? kText : kGood);
  y += lineH;
  if (!tapping && !done()) c.drawText(f, "   길게 누른다고 느낄 만큼", x, y, kText);
  const int statusY = y + 2 * lineH;

  int tx, ty;
  pressTarget(tx, ty);
  drawCross(c, tx, ty, scale);

  x = w / 2 + w / 24;
  y = statusY;
  if (heldMs_ > 0) {
    snprintf(line, sizeof(line), "방금 %u ms", unsigned(heldMs_));
    c.drawText(f, line, x, y, kText, scale);
  }
  y += (f.height + 3) * scale + lineH / 2;
  snprintf(line, sizeof(line), "톡: 가장 길게 %d ms", quickMax_);
  c.drawText(f, line, x, y, kText);
  y += lineH;
  if (holds_ > 0) {
    snprintf(line, sizeof(line), "꾹: 가장 짧게 %d ms", holdMin_);
    c.drawText(f, line, x, y, kText);
  }
  y += lineH;
  if (done()) {
    snprintf(line, sizeof(line), "기준 %d → %d ms", previous_.longPressMs, suggestedLongPress());
  } else {
    snprintf(line, sizeof(line), "기준 %d ms", previous_.longPressMs);
  }
  c.drawText(f, line, x, y, done() ? kGood : kText);
  drawButtons(c);
}

void TouchCalibrationScreen::drawDrag(gfx::Canvas& c) const {
  const gfx::Font& f = gfx::kFontKo11;
  const int w = width_, h = height_, scale = w >= 480 ? 2 : 1, lineH = f.height + 3;
  char line[64];
  int x = w / 24, y = h / 14;
  c.drawText(f, "드래그 보정", x, y, kHint, scale);
  y += (f.height + 3) * scale;
  snprintf(line, sizeof(line), "+를 움직이지 말고 눌렀다 떼세요 (%d/%d)", std::min(stills_, kStillPresses),
           kStillPresses);
  c.drawText(f, line, x, y, done() ? kText : kGood);
  y += lineH;
  c.drawText(f, "   톡, 꾹 섞어서", x, y, kText);
  const int statusY = y + 2 * lineH;

  // The cross inside the drag threshold: now (grey) and suggested (green)
  int tx, ty;
  pressTarget(tx, ty);
  auto threshold = [&](int slop, gfx::Color color) {
    int rx = roundInt(slop * previous_.ax), ry = roundInt(slop * previous_.ay);
    drawFrame(c, gfx::Rect(tx - rx, ty - ry, 2 * rx + 1, 2 * ry + 1), color);
  };
  threshold(previous_.slopPx, kEdge);
  if (stills_ > 0) threshold(suggestedSlop(), kGood);
  drawCross(c, tx, ty, scale);

  // Where the finger went on the last press
  for (int i = 0; i < traceN_; i++) c.fillRect(gfx::Rect(traceX_[i] - 1, traceY_[i] - 1, 3, 3), kRaw);

  x = w / 2 + w / 24;
  y = statusY;
  if (traceN_ > 0) {
    snprintf(line, sizeof(line), "흔들림 %d px", wander_);
    c.drawText(f, line, x, y, kText, scale);
  }
  y += (f.height + 3) * scale + lineH / 2;
  snprintf(line, sizeof(line), "가장 크게 %d px", stillMax_);
  c.drawText(f, line, x, y, kText);
  y += lineH;
  if (done()) {
    snprintf(line, sizeof(line), "기준 %d → %d px", previous_.slopPx, suggestedSlop());
  } else {
    snprintf(line, sizeof(line), "기준 %d px", previous_.slopPx);
  }
  c.drawText(f, line, x, y, done() ? kGood : kText);
  drawButtons(c);
}

}  // namespace ui
