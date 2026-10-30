#pragma once

#include <cstdint>

#include "app/engine/world.h"
#include "app/gfx/compositor.h"
#include "app/gfx/rle_image.h"
#include "app/gfx/span_image.h"
#include "app/ui/heroine/heroine_portrait.h"

namespace ui::heroine {

// A pre-rendered picture covering the screen (her room at some hour), centered
class BackdropLayer : public gfx::Layer {
 public:
  void configure(const gfx::Rect& screen) { setBounds(screen); }
  void show(const gfx::RleImage& img);

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  const gfx::RleImage* img_ = nullptr;
};

// Her portrait, painted beforehand in a few head tilts (tools/rig/portrait.py),
// with up to two frames over it (the eyes or the whole face, and the mouth).
// On top of that only whole rows move: her head and shoulders rise and sink
// (breathing, nodding) over her chest, whose rows stretch or close up under
// them. Redrawn when the pose changes, or a frame's area when it does.
class PortraitLayer : public gfx::Layer {
 public:
  struct Pose {
    int x = 0, y = 0;  // screen position of the picture's top-left corner
    int tilt = 0;      // head tilt level, -kHeroineTiltMax .. kHeroineTiltMax
    int lift = 0;      // px her head and shoulders rise (< 0: sink)
    bool operator==(const Pose& o) const { return x == o.x && y == o.y && tilt == o.tilt && lift == o.lift; }
  };

  void configure(const gfx::Color* palette, const gfx::Rect& screen);  // dithered, as gfx::SpanPalette
  void setPose(const Pose& p);
  const Pose& pose() const { return pose_; }
  void setFrames(int face, int mouth);  // frame indices, -1 = the pose's own
  void refresh() { invalidate(); }     // palette changed
  // On her picture, by the pose's silhouette
  bool contains(int x, int y) const;

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  int pictureRow(int screenY) const;  // picture row shown on a screen row
  gfx::Rect onScreen() const;         // where the pose draws
  gfx::Rect onScreen(int frame) const;
  const PortraitPatch* patch(int frame) const;

  gfx::SpanPalette palette_;
  Pose pose_;
  int face_ = -1, mouth_ = -1;
  bool posed_ = false;
};

// Visual-novel dialog box along the bottom of the screen: translucent panel,
// name plate on its top edge, text typing out line by line, a bobbing "next"
// arrow, and for prompts a row of choice buttons inside the box under the text.
class DialogLayer : public gfx::Layer {
 public:
  static constexpr int kMaxLines = 4, kLineBytes = 96;

  // Resting place of the box, colors and the speaker on the name plate
  void configure(const gfx::Rect& box, gfx::Color accent, const char* speaker);
  void setDrop(int dy);  // slid down by dy px (hidden below the screen when large)
  // Title (accent color, first line, may be empty) and body. Room for the
  // choices leaves two lines of text instead of four.
  void setMessage(const char* title, const char* body, bool withChoices);
  void setTyped(int chars);
  int length() const { return length_; }
  void setMore(bool on, int lift);  // blinking "next" arrow, lifted by px
  void setChoices(const char (*options)[25], int count);  // 0 = none
  void showChoices(float shown);  // 0 .. 1 rising into place
  void setPressed(int option);    // -1 = none
  int choiceAt(int x, int y) const;  // screen coords, -1 if none

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  gfx::Rect box() const;
  gfx::Rect plate() const;
  gfx::Rect lineRect(int line) const;
  gfx::Rect choiceRect(int i) const;
  gfx::Rect choicesArea() const;
  gfx::Rect moreRect() const;
  void place();
  void rewrap();

  gfx::Rect rest_;
  int drop_ = 0;
  gfx::Color accent_ = 0;
  char speaker_[25] = "";
  int speakerW_ = 0;

  char title_[kLineBytes] = "", body_[200] = "";
  char lines_[kMaxLines][kLineBytes] = {};
  int lineCount_ = 0, lineLength_[kMaxLines] = {}, length_ = 0, typed_ = 0;
  bool titled_ = false, roomForChoices_ = false;
  bool more_ = false;
  int moreLift_ = 0;

  char choices_[engine::kMaxOptions][25] = {};
  int choiceCount_ = 0, pressed_ = -1;
  float choicesShown_ = 0;
};

}  // namespace ui::heroine
