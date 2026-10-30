#pragma once

#include <cstddef>
#include <cstdint>

#include "app/gfx/canvas.h"
#include "app/ui/blob/eye.h"
#include "app/ui/blob/fluid_body.h"

namespace ui::blob {

// Attachment points. Back items are drawn behind the (translucent) body.
enum class Slot : uint8_t { Head, Back, Left, Right, Count };
constexpr size_t kSlotCount = size_t(Slot::Count);

enum class HeadItem : uint8_t { None, Antenna, Sprout, Bow, Ears, Horns, Crown, Count };
enum class BackItem : uint8_t { None, Wings, Fin, Spikes, Count };
enum class HandItem : uint8_t { None, Arm, Heart, Star, Flag, Balloon, Count };

struct AccessorySlot {
  uint8_t type = 0;  // HeadItem / BackItem / HandItem depending on the slot
  gfx::Color color = 0, color2 = 0;
  float size = 1.0f;   // scale
  float angle = 0.0f;  // degrees along the outline from the slot's home position
};

struct Accessories {
  AccessorySlot slots[kSlotCount];
  Accessories();
  AccessorySlot& operator[](Slot s) { return slots[size_t(s)]; }
  const AccessorySlot& operator[](Slot s) const { return slots[size_t(s)]; }
};

const char* slotName(Slot s);
int itemCount(Slot s);
const char* itemName(Slot s, int type);
int findItem(Slot s, const char* name);  // -1 if unknown
// Rest angle on the outline (radians, y down) including the configured offset
float slotAngle(Slot s, const AccessorySlot& cfg);

// Live secondary motion, from the animator
struct AccessoryMotion {
  float sway[kSlotCount] = {};  // radians, inertia lag
  float flap = 0;               // wing beat, radians
  float wave = 0;               // hand wave, radians
};

// Procedural geometry: a few anti-aliased SDF primitives in screen space
struct Prim {
  enum Kind : uint8_t { Circle, Ellipse, Capsule, Triangle, Heart, Star } kind = Circle;
  gfx::Color color = 0;
  uint8_t alpha = 32;
  float v[8] = {};
  gfx::Rect box;
};

struct PrimList {
  static constexpr int kMax = 28;
  Prim items[kMax];
  int count = 0;
  gfx::Rect bounds;
  void clear() {
    count = 0;
    bounds = gfx::Rect();
  }
  Prim* add();
};

struct BodyFrame {
  float cx = 0, cy = 0, radius = 1;
  const FluidBody* body = nullptr;
  gfx::Color bodyColor = 0;
  uint8_t bodyOpacity = 32;
};

// Rebuilds the primitives for all slots; back items go to `back`, the rest to `front`
void buildAccessories(const Accessories& acc, const AccessoryMotion& m, const BodyFrame& f,
                      PrimList& back, PrimList& front);
void paintPrims(gfx::Canvas& c, const PrimList& prims);
// Free-standing circle (e.g. a droplet that pinched off the body); updates bounds
void addCircle(PrimList& L, float x, float y, float r, gfx::Color color, uint8_t alpha);

enum class BrowStyle : uint8_t { None, Line, Arch, Thick, Dot, Count };

struct BrowConfig {
  uint8_t style = uint8_t(BrowStyle::None);
  gfx::Color color = gfx::rgb(22, 24, 44);
  float thickness = 0.035f;  // of radius
  float length = 1.1f;       // of the eye's half width
  float lift = 0.07f;        // gap above the eye, of radius
};

const char* browStyleName(BrowStyle s);
int findBrowStyle(const char* name);  // -1 if unknown
int browStyleCount();

// Eyebrows above both eyes; pose (raise, slant) comes from the eye shape
void buildBrows(const BrowConfig& b, const EyeShape& s, const EyeParams eyes[2], PrimList& out);

}  // namespace ui::blob
