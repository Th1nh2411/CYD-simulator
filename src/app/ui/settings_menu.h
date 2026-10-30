#pragma once

#include <cstdint>

#include "app/gfx/compositor.h"

namespace ui {

constexpr int kSettingsMenuZ = 1990;  // below the touch calibration screens it hands over to

// Full-screen settings menu: the touch calibrations (boards with a touch panel
// only), the AP mode (the device's own Wi-Fi for the settings page) and Close.
// Picked by touch, with corrected screen points, or with the board's button: a
// press moves the highlight, a long press picks it. That works on a board
// without touch too, and when the touch is too far off to hit anything.
// The platform acts on the Action; it owns the AP, so the row shows what it
// says through setAp().
class SettingsMenu {
 public:
  enum class Action : uint8_t { None, TouchCal, LongPressCal, DragCal, ToggleAp, Close };

  void attach(gfx::Compositor& comp, int z, int screenW, int screenH);
  void open(bool hasTouch);
  void close();
  bool active() const { return open_; }

  // on: the AP is up. locked: it cannot be turned off (no home Wi-Fi saved, the AP is the only way in)
  void setAp(bool on, bool locked);
  // Shown under the rows while the AP is on
  void setApInfo(const char* ssid, const char* password, const char* address);

  Action onTap(int x, int y);
  Action onButton(bool longPress);

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
    const SettingsMenu* owner = nullptr;

   protected:
    void draw(gfx::Canvas& c) const override;
  };

  static constexpr int kMaxItems = 5;
  gfx::Rect row(int i) const;
  void label(int i, char* out, size_t size) const;

  Screen screen_;
  int width_ = 0, height_ = 0;
  bool open_ = false, apOn_ = false, apLocked_ = false;
  Action items_[kMaxItems] = {};
  int count_ = 0, selected_ = 0;
  char ssid_[24] = "", password_[24] = "", address_[32] = "";
};

}  // namespace ui
