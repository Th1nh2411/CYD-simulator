#pragma once

#include <cstddef>
#include <cstdint>

#include "app/gfx/canvas.h"
#include "app/ui/expression.h"

namespace ui::blob {

// Parametric eye, drawn as an anti-aliased distance field. Sizes are fractions
// of the body radius; angles are radians for the right eye, mirrored for the
// left. Any two shapes interpolate, so expression changes morph smoothly.
struct EyeShape {
  float w = 0.30f, h = 0.44f;  // full width / height
  float square = 0.0f;         // 0 = ellipse .. 1 = rounded rectangle
  float corner = 0.35f;        // rounded-rect corner radius, of the smaller half side
  float tilt = 0.0f;           // whole-eye rotation
  float lidTop = 0.0f;         // 0..1 of the height covered by the upper lid
  float lidAngle = 0.0f;       // upper lid slant: + lowers the inner corner (angry), - the outer (sad)
  float lidBottom = 0.0f;      // 0..1 covered from below (cheerful squint)
  float arc = 0.0f;            // 0 = solid .. 1 = thin "^" arc carved from below
  float shine = 1.0f;          // catchlight size, 0 = none
  float browRaise = 0.0f;      // eyebrow lift for this expression, of radius
  float browAngle = 0.0f;      // eyebrow slant: + lowers the inner end (angry), - raises it (sad)
};

EyeShape lerp(const EyeShape& a, const EyeShape& b, float t);
bool nearlyEqual(const EyeShape& a, const EyeShape& b);

// One shape per expression. Plain data: new sets can come from the character
// converter or the settings page without code changes.
struct EyeSet {
  const char* name;
  EyeShape shapes[kExpressionCount];
  const EyeShape& operator[](Expression e) const { return shapes[size_t(e)]; }
};

int eyeSetCount();
const EyeSet& eyeSet(int index);  // 0 = default
int findEyeSet(const char* name);  // -1 if unknown

struct EyeParams {
  float cx = 0, cy = 0;      // eye center, px
  float radius = 1;          // body radius, px
  float squashX = 1, squashY = 1;
  bool mirror = false;       // true for the left eye
  float blink = 0;           // 0 open .. 1 closed
  float gazeX = 0, gazeY = 0;  // -1..1, also moves the catchlight inside the eye
  float shineTravel = 0.3f;    // catchlight movement, of the eye's half extents
  gfx::Color color = 0, shine = 0;
};

void drawEye(gfx::Canvas& c, const EyeShape& s, const EyeParams& p);

}  // namespace ui::blob
