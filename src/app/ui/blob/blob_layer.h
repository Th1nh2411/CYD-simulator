#pragma once

#include "app/gfx/compositor.h"
#include "app/ui/blob/blob_animator.h"
#include "app/ui/blob/blob_style.h"

namespace ui::blob {

// Draws the fluid body as an anti-aliased, translucent polygon (scanline
// spans with 4 vertical subsamples), plus highlight, cheeks and eyes.
// Rebuilds geometry only when something visibly moved.
class BlobLayer : public gfx::Layer {
 public:
  void setConfig(const CharacterConfig& cfg);
  void update(const BlobAnimator& anim);
  const gfx::Rect& bodyRect() const { return bodyBox_; }

 protected:
  void draw(gfx::Canvas& c) const override;

 private:
  static constexpr int kPoly = 2 * FluidBody::kNodes;
  static constexpr int kSub = 4;  // vertical subsamples per pixel row
  static constexpr int kMaxRows = 256;

  struct Ellipse {
    float x = 0, y = 0, rx = 1, ry = 1;
  };

  void buildSpans();
  void drawBody(gfx::Canvas& c) const;
  void drawSoftEllipse(gfx::Canvas& c, const Ellipse& e, gfx::Color color, uint32_t alpha) const;
  bool rowInterior(int y, float& left, float& right) const;

  BlobStyle style_;
  Accessories accessories_;
  BrowConfig brows_;
  AccessoryMotion accMotion_;
  PrimList backPrims_, frontPrims_;
  float px_[kPoly] = {}, py_[kPoly] = {};
  float minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0;
  int top_ = 0, rows_ = 0;
  float spanL_[kMaxRows * kSub], spanR_[kMaxRows * kSub];

  EyeParams eyes_[2];
  Ellipse cheeks_[2], highlight_;
  Face face_;
  gfx::Rect bodyBox_;
  bool built_ = false, dirtyStyle_ = false, hadDroplets_ = false;
};

}  // namespace ui::blob
