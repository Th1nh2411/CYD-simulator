#pragma once

#include <cstdint>

#include "app/gfx/compositor.h"

namespace ui {

constexpr int kOtpScreenZ = 1980;  // under the settings menu

// Full-screen keypad for the 6-digit OTP a device logs in with the first time
// (AI Buddy, flow B5): the backoffice shows the code next to the device it was
// given to. Shows why it is asked again (a failure code), takes the digits by
// touch and hands them to the platform on OK. Boards without touch take the
// code over serial or the settings page instead.
class OtpScreen {
 public:
  enum class Action : uint8_t { None, Submit, Cancel };

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH);
  // message: why it is asked again ("" the first time), shown in red
  void open(const char* deviceNo, const char* message);
  void close();
  bool active() const { return open_; }
  Action onTap(int x, int y);
  const char* code() const { return digits_; }  // the 6 digits, after Submit

 private:
  class Screen : public gfx::Layer {
   public:
    void show(const gfx::Rect& screen) {
      setBounds(screen);
      setVisible(true);
      invalidate();
    }
    void hide() { setVisible(false); }
    void changed() { invalidate(); }
    const OtpScreen* owner = nullptr;

   protected:
    void draw(gfx::Canvas& c) const override;
  };

  static constexpr int kDigits = 6, kKeys = 12;
  gfx::Rect key(int i) const;      // 0..8 = 1..9, 9 = back, 10 = 0, 11 = OK
  gfx::Rect closeBox() const;
  gfx::Rect digitBox(int i) const;
  int keypadTop() const;

  Screen screen_;
  int width_ = 0, height_ = 0;
  bool open_ = false;
  char deviceNo_[16] = "", message_[96] = "";
  char digits_[kDigits + 1] = "";
  int pressed_ = -1;
};

}  // namespace ui
