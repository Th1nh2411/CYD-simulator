#pragma once

#include <cstdint>

#include "app/engine/world.h"
#include "app/gfx/compositor.h"

// Layers of the pixel monster's world: the field or battle backdrop, sprites,
// the message box (dialog or battle menu), name plates, particles and the
// screen transitions. Pixel-art style: solid colors, no blending.
namespace ui::monster {

// Tiled field, or the battle backdrop with its two platforms. Opaque, full screen.
class SceneLayer : public gfx::Layer {
 public:
  static constexpr int kMaxCols = 20, kMaxRows = 15;
  enum class Mode : uint8_t { Field, Battle };

  void configure(const gfx::Rect& screen, int tileScale, const gfx::Color* tilePalette, uint32_t seed);
  void setMode(Mode m);
  Mode mode() const { return mode_; }
  // Battle platforms (ellipse centers and radii, px) and colors
  void setPlatforms(const gfx::Rect& foe, const gfx::Rect& mine);

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  void drawField(gfx::Canvas& c) const;
  void drawBattle(gfx::Canvas& c) const;

  Mode mode_ = Mode::Field;
  int scale_ = 3, cols_ = 0, rows_ = 0;
  const gfx::Color* palette_ = nullptr;
  uint8_t tiles_[kMaxRows][kMaxCols] = {};
  gfx::Rect foe_, mine_;  // platform ellipses' bounding boxes
};

// One palette sprite, optionally with a small shadow under its feet. Up in
// the air, the shadow stays `air` px lower on the ground, and is smaller.
class SpriteLayer : public gfx::Layer {
 public:
  void show(const gfx::IndexedImage& img, const gfx::Color* palette, int x, int y, int scale, bool flipX,
            bool shadow = false, int air = 0);
  void hide() { setVisible(false); }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  gfx::Rect shadowRect() const;

  const gfx::IndexedImage* img_ = nullptr;
  const gfx::Color* palette_ = nullptr;
  int x_ = 0, y_ = 0, scale_ = 1, air_ = 0;
  bool flip_ = false, shadow_ = false;
};

// Message box at the bottom of the screen. Text types out character by
// character; in battle the right side holds the moves (the prompt's options).
class BoxLayer : public gfx::Layer {
 public:
  static constexpr int kMaxLines = 4, kLineBytes = 96;

  void place(const gfx::Rect& r);
  // Title (accent color, first line, may be empty) and body, wrapped to the text area
  void setMessage(const char* title, const char* body);
  void setTyped(int chars);  // characters shown so far
  int length() const { return length_; }
  void setMore(bool on);  // blinking "next" arrow
  void setOptions(const char (*options)[25], int count);  // 0 = no menu
  void setPressed(int option);
  int optionAt(int x, int y) const;  // screen coords, -1 if none

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  gfx::Rect textArea() const;
  gfx::Rect optionRect(int i) const;
  gfx::Rect lineRect(int line) const;
  void rewrap();

  char title_[kLineBytes] = "", body_[200] = "";
  char lines_[kMaxLines][kLineBytes] = {};
  int lineCount_ = 0, lineLength_[kMaxLines] = {}, length_ = 0, typed_ = 0;
  bool titled_ = false, more_ = false;
  char options_[engine::kMaxOptions][25] = {};
  int optionCount_ = 0, pressed_ = -1;
};

// Battle name plate: name, level and an HP bar
class PlateLayer : public gfx::Layer {
 public:
  void place(const gfx::Rect& r);
  void set(const char* name, int level, float hp);  // hp 0..1

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  char name_[49] = "";
  int level_ = 1;
  int hpPx_ = 0;  // filled bar width, px
};

// A move's particles flying from the monster to its target (sparks, leaves, bubbles)
class FxLayer : public gfx::Layer {
 public:
  static constexpr int kMax = 24;
  struct Spark {
    float x = 0, y = 0, vx = 0, vy = 0, life = 0, age = 0;
    uint8_t size = 2;
    bool round = false;
    gfx::Color color = 0;
  };

  void spawn(const Spark& s);
  void step(float dt);
  void clear();
  bool active() const { return count_ > 0; }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  void updateBounds();
  Spark sparks_[kMax];
  int count_ = 0;
};

// Screen transitions over everything: white flashes, then closing/opening bars
class WipeLayer : public gfx::Layer {
 public:
  void configure(const gfx::Rect& screen);
  void flash(bool on);
  void bars(float closed);  // 0 = open .. 1 = screen covered
  void off();
  bool on() const { return visible(); }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  bool flash_ = false;
  int barsPx_ = 0;  // covered width per bar
};

}  // namespace ui::monster
