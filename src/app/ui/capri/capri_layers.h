#pragma once

#include <cstdint>

#include "app/engine/world.h"
#include "app/gfx/compositor.h"
#include "app/gfx/rle_image.h"
#include "app/gfx/span_image.h"
#include "app/ui/capri/capri_art.h"

namespace ui::capri {

// Night City's sky behind her: a dark gradient with faint scanlines, still
// stars, and now and then the satellite she dreams of reaching crossing it
class VoidLayer : public gfx::Layer {
 public:
  void configure(const gfx::Rect& screen, uint32_t seed);
  // Satellite position in screen px, or off (x < 0)
  void setSatellite(int x, int y);
  // The "TALK" chip in the bottom right corner (a tap asks her brain to talk), shown or not
  void setTalk(bool shown, bool lit);
  // The screen's left and right edges crackling red (something is about to grab them), 0 = calm
  void setEdges(uint32_t crackle);
  gfx::Rect talkRect() const;

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  static constexpr int kStars = 48;
  gfx::Color colorAt(int y) const;  // the sky behind a row
  gfx::Rect satRect() const;
  int16_t starX_[kStars] = {}, starY_[kStars] = {};
  uint8_t starLevel_[kStars] = {};
  int satX_ = -1, satY_ = 0;
  bool talk_ = false, talkLit_ = false;
  uint32_t edges_ = 0;
};

// Her figure: cels of one picture (capri_art kCel*), moved only by whole rows.
// Rows above her neck sway (her mane), bands of rows slide sideways when she
// glitches, and a tinted copy of some rows lags beside her (afterimage). A cel
// swap redraws only the rows where the two cels differ.
class FigureLayer : public gfx::Layer {
 public:
  static constexpr int kSlices = 3;
  struct Slice {
    int16_t y0 = 0, y1 = 0, dx = 0;  // picture rows [y0, y1) drawn dx px aside
  };
  struct Pose {
    int x = 0, y = 0;  // screen position of the picture's top-left corner
    int sway = 0;      // px her mane's top is pushed aside
    Slice slices[kSlices];
    int ghostDx = 0;   // 0 = no afterimage
    int16_t ghostY0 = 0, ghostY1 = 0;
    int cel = 0;
    int16_t rowsFrom = 0, rowsTo = 0x7FFF;  // picture rows drawn (her intro shows her hands first)
    bool operator==(const Pose& o) const;
  };

  // cels: kCelCount pictures of one size; changed: where each differs from cel 0
  void configure(const gfx::SpanImage* cels, const CelRows* changed, int neck, const gfx::SpanPalette& main,
                 const gfx::SpanPalette& ghost, const gfx::Rect& screen);
  void setPose(const Pose& p);
  const Pose& pose() const { return pose_; }
  void refresh() { invalidate(); }  // palette changed
  bool contains(int x, int y) const;  // on her or within a fingertip of her
  int width() const { return cels_ ? cels_[0].w : 0; }
  int height() const { return cels_ ? cels_[0].h : 0; }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  static constexpr int kReach = 48;  // farthest any row moves aside, px
  int shift(int row) const;
  gfx::Rect onScreen() const;

  const gfx::SpanImage& img() const { return cels_[pose_.cel]; }
  const gfx::SpanImage* cels_ = nullptr;
  const CelRows* changed_ = nullptr;
  int neck_ = 0;
  gfx::SpanPalette main_, ghost_;
  Pose pose_;
  bool posed_ = false;
};

// A comic close-up that flashes beside her: a picture in a chamfered white
// frame, sliding in as glitched bands
class CutinLayer : public gfx::Layer {
 public:
  void show(const gfx::RleImage& img, int x, int y);
  void setReveal(float u);  // 0 .. 1, bands settle into place
  void hide() { setVisible(false); }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  static constexpr int kBorder = 3, kChamfer = 14, kReach = 36;
  int bandDx(int row) const;
  const gfx::RleImage* img_ = nullptr;
  int x_ = 0, y_ = 0;
  int reveal_ = 0;  // 0 .. 4 steps
};

// Her message panel, cyberpunk style: a dark chamfered plate with scanlines and
// a colored border, a red bar down its left edge, her face in a cut frame, a
// state badge, "> sys:" with the card's title, then "CAPRI:" and her text
// decoding in letter by letter behind noise glyphs. Prompts get chamfered
// choice buttons along the bottom.
class PanelLayer : public gfx::Layer {
 public:
  static constexpr int kMaxLines = 7, kLineBytes = 96;

  // The largest box it may take; it shrinks to its text, centered in that box or
  // on its bottom edge
  void configure(const gfx::Rect& room, bool withFace, bool onBottom);
  void setStyle(gfx::Color accent, const char* badge, const gfx::RleImage* face);
  void setOpen(float u);  // 0 .. 1 as it transmits in (steps)
  // The sys line (may be empty) and her text; room for choices takes two lines
  void setMessage(const char* sys, const char* text, bool withChoices);
  void setTyped(int chars, uint32_t noise);  // noise: seed of the glyphs at the edge
  int length() const { return length_; }
  void setCursor(bool on);
  void setBadgeOn(bool on);
  void setChoices(const char (*options)[25], int count);
  void showChoices(float shown);
  void setPressed(int option);
  int choiceAt(int x, int y) const;
  const gfx::Rect& box() const { return box_; }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  int textX() const;
  int textTop() const;
  gfx::Rect lineRect(int line) const;
  gfx::Rect choiceRect(int i) const;
  gfx::Rect choicesArea() const;
  void rewrap();

  void place(int h);

  gfx::Rect room_, box_;
  bool withFace_ = true, onBottom_ = false;
  gfx::Color accent_ = 0;
  char badge_[12] = "";
  const gfx::RleImage* face_ = nullptr;
  int open_ = 4;  // steps of 4
  bool badgeOn_ = true, cursor_ = false;

  char sys_[kLineBytes] = "", text_[260] = "";
  char lines_[kMaxLines][kLineBytes] = {};
  int lineCount_ = 0, lineLength_[kMaxLines] = {}, length_ = 0, typed_ = 0;
  uint32_t noise_ = 0;
  bool roomForChoices_ = false;

  char choices_[engine::kMaxOptions][25] = {};
  int choiceCount_ = 0, pressed_ = -1;
  float choicesShown_ = 0;
};

}  // namespace ui::capri
