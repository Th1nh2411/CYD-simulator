#pragma once

#include <cstddef>
#include <cstdint>

#include "app/gfx/compositor.h"
#include "app/ui/touch_gesture.h"
#include "hal/touch.h"

namespace ui {

constexpr int kTouchCalibrationZ = 2000;  // above everything View draws (its top layer is 900)

// Corrects a touch point with cal and keeps it on the screen
void applyTouchCalibration(const hal::TouchCalibration& cal, int screenW, int screenH, int& x, int& y);

// The same correction for the screen turned a quarter clockwise quarterTurns
// times from the width x height frame cal was measured in: each turn takes
// (x, y) to (height-1-y, x), as drivers::RotatedTouch does. Four turns give
// cal back, so a calibration is stored in the board's own frame and survives
// a change of screen rotation.
hal::TouchCalibration rotateTouchCalibration(hal::TouchCalibration cal, int quarterTurns, int width, int height);

// The same correction for the screen mirrored left to right, (x, y) to
// (width-1-x, y); its own inverse
hal::TouchCalibration mirrorTouchCalibration(hal::TouchCalibration cal, int width);

// {"ax":..,"bx":..,"ay":..,"by":..,"slop":..,"hold":..} (stored as one NVS / localStorage string)
size_t touchCalibrationToJson(const hal::TouchCalibration& cal, char* out, size_t size);
bool touchCalibrationFromJson(const char* json, hal::TouchCalibration& cal);  // false: cal unchanged

// Full-screen touch calibration, one of three kinds picked in the settings menu,
// each ending in Save / Again / Cancel buttons:
// Touch: one target at a time; every tap refits screen = a * raw + b per axis
// (least squares; offset only until the taps spread), and shows the raw tap
// (red) and the corrected one (green). Once two rounds of nine taps no longer
// move the fit, the buttons come up, hit with the new fit.
// Long press: quick taps, then deliberate long presses on a cross; the
// long-press time goes between the slowest tap and the quickest hold.
// Drag: presses on a cross measure how far a still finger wanders; the drag
// threshold goes above the most it did.
// The board's button cancels (the way out when the touch is too far off to hit
// Cancel). Touches come in raw (uncalibrated); the platform collects the result
// when state() leaves Running.
class TouchCalibrationScreen {
 public:
  enum class State : uint8_t { Off, Running, Saved, Cancelled };
  enum class Kind : uint8_t { Touch, LongPress, Drag };

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH);
  void start(const hal::TouchCalibration& current, Kind kind);  // current: kept on Cancel
  void onButton();  // the board's button
  // Every frame while running: the raw sample and the gesture made of it
  void onTouch(bool touching, int rawX, int rawY, const TouchGesture& gesture, const TouchGesture::Event& g,
               uint32_t nowMs);
  // Hides the screen after Saved / Cancelled has been handled
  void finish();

  State state() const { return state_; }
  bool active() const { return state_ == State::Running; }
  const hal::TouchCalibration& result() const { return result_; }

 private:
  struct AxisFit {
    double n = 0, m = 0, mm = 0, t = 0, mt = 0;
    double a = 1, b = 0;
    void add(double measured, double target);
    double apply(double measured) const { return a * measured + b; }
  };

  class Screen : public gfx::Layer {
   public:
    void show(const gfx::Rect& screen) {
      setBounds(screen);
      setVisible(true);
      invalidate();
    }
    void hide() { setVisible(false); }
    void changed() { invalidate(); }
    const TouchCalibrationScreen* owner = nullptr;

   protected:
    void draw(gfx::Canvas& c) const override;
  };

  static constexpr int kTargets = 9, kMaxTaps = 120, kTrace = 96;
  void target(int i, int& x, int& y) const;
  void pressTarget(int& x, int& y) const;  // the cross of the long press and drag calibrations
  gfx::Rect button(int i) const;           // Save, Again, Cancel
  void restart();
  void onTap(int rawX, int rawY);  // a tap while fitting the touch
  void hit(int x, int y);
  void trackPress(bool touching, int rawX, int rawY, bool pressed, uint32_t now);
  void pressEnded();
  bool done() const;  // enough samples to save
  int suggestedLongPress() const;
  int suggestedSlop() const;
  void drawTouch(gfx::Canvas& c) const;
  void drawLongPress(gfx::Canvas& c) const;
  void drawDrag(gfx::Canvas& c) const;
  void drawButtons(gfx::Canvas& c) const;

  Screen screen_;
  int width_ = 0, height_ = 0;
  State state_ = State::Off;
  Kind mode_ = Kind::Touch;
  hal::TouchCalibration previous_, result_;

  // Touch
  AxisFit fx_, fy_;
  int16_t mx_[kMaxTaps], my_[kMaxTaps], tx_[kMaxTaps], ty_[kMaxTaps];
  int taps_ = 0, next_ = 0;
  double rms_ = 0;
  double roundStart_[4] = {1, 0, 1, 0};
  bool converged_ = false;
  int lastRawX_ = -1, lastRawY_ = -1, lastX_ = -1, lastY_ = -1;

  // Long press and drag: the press under way (raw px) and its path on screen
  bool pressing_ = false, onCross_ = false;
  int downX_ = 0, downY_ = 0, wander_ = 0;
  uint32_t downMs_ = 0, touchMs_ = 0, heldMs_ = 0;
  int16_t traceX_[kTrace], traceY_[kTrace];
  int traceN_ = 0;
  int quick_ = 0, quickMax_ = 0, holds_ = 0, holdMin_ = 0;  // long press: taps then holds, in ms
  int stills_ = 0, stillMax_ = 0;  // drag: presses on the cross, and the most one wandered
};

}  // namespace ui
