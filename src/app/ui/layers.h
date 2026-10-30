#pragma once

#include "app/engine/world.h"
#include "app/gfx/compositor.h"

namespace ui {

class BackgroundLayer : public gfx::Layer {
 public:
  void configure(const gfx::Rect& screen, gfx::Color color);

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  gfx::Color color_ = 0;
};

// Single line of text with transparent background
class TextLayer : public gfx::Layer {
 public:
  void set(const char* text, int x, int y, gfx::Color color, int scale);

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  char text_[48] = "";
  gfx::Color color_ = 0;
  int scale_ = 1;
};

// "HH:MM" in the top-right corner, light with a dark shadow so it reads on any
// background. Hidden while the time is unknown.
class ClockLayer : public gfx::Layer {
 public:
  void configure(int screenW, int margin, int scale);
  void set(int minuteOfDay);  // -1 = unknown

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  char text_[6] = "";
  int screenW_ = 0, margin_ = 0, scale_ = 2, minute_ = -1;
};

// Card for a notification or a prompt: title, word-wrapped body and, for
// prompts, option buttons along the bottom. ASCII cards use the 5x7 font at
// the given scales; cards with other text (Korean) use the Korean font.
class CardLayer : public gfx::Layer {
 public:
  void setContent(const engine::CardContent& content, int width, int height, int titleScale,
                  int bodyScale);
  void setPosition(int x, int y);
  void setPressed(int option);  // -1 = none
  int optionAt(int x, int y) const;  // screen coords; -1 if no button there
  int width() const { return w_; }
  int height() const { return h_; }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  static constexpr int kMaxLines = 8;
  static constexpr int kLineBytes = 72;

  // The 5x7 font at a scale (font == nullptr), or a proportional font
  struct Face {
    const gfx::Font* font = nullptr;
    int scale = 1;
    int height() const;
    int lineGap() const { return font ? 1 : 2 * scale; }
    int advance(const char* s, size_t bytes) const;  // pen movement over s[0..bytes)
    int width(const char* s) const;                   // inked width
    void draw(gfx::Canvas& c, const char* s, int x, int y, gfx::Color color) const;
    // Bytes of the longest prefix (whole characters, at least one) that fits
    size_t fit(const char* s, int maxWidth, size_t maxBytes) const;
  };

  int wrap(const char* text, int maxWidth, int maxLines);

  engine::CardKind kind_ = engine::CardKind::None;
  char title_[kLineBytes] = "";
  char lines_[kMaxLines][kLineBytes] = {};
  int lineCount_ = 0;
  char options_[engine::kMaxOptions][sizeof(engine::CardContent::options[0])] = {};
  int optionCount_ = 0;
  gfx::Rect buttons_[engine::kMaxOptions];  // relative to the card origin
  int pressed_ = -1;
  int titleScale_ = 2;  // spacing follows the title scale
  Face titleFace_, bodyFace_, buttonFace_;
  int w_ = 0, h_ = 0, x_ = 0, y_ = 0;
};

}  // namespace ui
