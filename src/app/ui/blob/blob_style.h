#pragma once

#include <cstdint>

#include "app/gfx/color.h"
#include "app/ui/blob/accessory.h"
#include "app/ui/blob/body_shape.h"
#include "app/ui/blob/eye.h"
#include "app/ui/blob/fluid_body.h"

namespace ui::blob {

// Look of the character. Lengths are fractions of the body radius or of its
// current half extents.
struct BlobStyle {
  gfx::Color body = gfx::rgb(110, 200, 255);
  uint8_t opacity = 28;  // 0..32; below 32 the background shows through

  gfx::Color highlight = gfx::rgb(255, 255, 255);
  uint8_t highlightAlpha = 16;  // 0..32, 0 = off

  gfx::Color eye = gfx::rgb(22, 24, 44);
  gfx::Color shine = gfx::rgb(255, 255, 255);  // catchlight
  gfx::Color cheek = gfx::rgb(255, 120, 170);
  uint8_t cheekAlpha = 12;  // 0..32, 0 = off

  float eyeSpacing = 0.38f;  // of half width, from center
  float eyeHeight = -0.10f;  // of half height, from center (negative = up)
  float gazeReach = 0.10f;   // whole-eye movement with gaze, of radius
  float shineTravel = 0.30f; // catchlight movement inside the eye with gaze, of eye half size
};

// How the body moves between layout positions: a glide that speeds up
// ("swoosh") pulling a long pointed tail behind it, then a dead stop where
// the tail slams back in ("thud"). Long tails can pinch off small droplets.
struct MotionParams {
  float glideMs = 260.0f; // duration of a typical move
  float thud = 0.65f;     // 0 = soft ease-in-out stop .. 1 = arrives at full speed and slams
  float drop = 1.6f;      // tail stretch: how far the tail lags behind (0 = none)
  float splash = 0.0f;    // experimental: 1 = long tails pinch off small droplets
  float bounce = 3.2f;    // happy bounce takeoff, radii per second
};

// Everything that defines the character. Plain data: it is what the settings
// page edits and what the external character converter will produce.
struct CharacterConfig {
  float size = 0.21f;  // body radius, fraction of the smaller screen side
  uint8_t bodyShape = uint8_t(BodyShape::Round);
  float aspect = 1.0f;  // body width / height
  gfx::Color background = gfx::rgb(14, 16, 30);
  BlobStyle style;
  char eyeSetName[16] = "oval";
  EyeShape eyes[kExpressionCount];
  BrowConfig brows;
  Accessories accessories;
  FluidBody::Params physics;
  MotionParams motion;

  CharacterConfig() { useEyeSet(0); }
  void useEyeSet(int index);
};

}  // namespace ui::blob
