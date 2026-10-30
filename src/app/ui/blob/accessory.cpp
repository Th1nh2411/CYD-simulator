#include "app/ui/blob/accessory.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/gfx/sdf.h"

namespace ui::blob {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
using gfx::sdf::V2;

const char* const kSlotNames[kSlotCount] = {"head", "back", "left", "right"};
const char* const kHeadNames[] = {"none", "antenna", "sprout", "bow", "ears", "horns", "crown"};
const char* const kBackNames[] = {"none", "wings", "fin", "spikes"};
const char* const kHandNames[] = {"none", "arm", "heart", "star", "flag", "balloon"};
const char* const kBrowNames[] = {"none", "line", "arch", "thick", "dot"};
static_assert(sizeof(kBrowNames) / sizeof(kBrowNames[0]) == size_t(BrowStyle::Count), "brow names");
static_assert(sizeof(kHeadNames) / sizeof(kHeadNames[0]) == size_t(HeadItem::Count), "head names");
static_assert(sizeof(kBackNames) / sizeof(kBackNames[0]) == size_t(BackItem::Count), "back names");
static_assert(sizeof(kHandNames) / sizeof(kHandNames[0]) == size_t(HandItem::Count), "hand names");

const char* const* namesOf(Slot s, int& count) {
  switch (s) {
    case Slot::Head:
      count = int(HeadItem::Count);
      return kHeadNames;
    case Slot::Back:
      count = int(BackItem::Count);
      return kBackNames;
    default:
      count = int(HandItem::Count);
      return kHandNames;
  }
}

// Local frame on the outline: u along the tangent (mirrored for left-side
// parts), v outward along the surface normal.
struct Frame {
  float ox = 0, oy = 0, ux = 0, uy = 0, vx = 0, vy = 0;
  V2 at(float u, float v) const { return {ox + u * ux + v * vx, oy + u * uy + v * vy}; }
  V2 dir(float u, float v) const { return {u * ux + v * vx, u * uy + v * vy}; }
};

Frame frameAt(const BodyFrame& f, float angle, float inset, float mirror, float rotate) {
  // Follow the wobbling surface, but not out along a stretched tail
  float r = std::min(f.body->radiusAt(angle), 1.25f * f.body->restRadiusAt(angle)) - inset;
  Frame fr;
  fr.ox = f.cx + r * cosf(angle);
  fr.oy = f.cy + r * sinf(angle);
  float a = angle + rotate;
  fr.vx = cosf(a);
  fr.vy = sinf(a);
  fr.ux = -sinf(a) * mirror;
  fr.uy = cosf(a) * mirror;
  return fr;
}

gfx::Rect boxOf(float x0, float y0, float x1, float y1) {
  int l = int(floorf(x0)) - 1, t = int(floorf(y0)) - 1;
  return gfx::Rect(l, t, int(ceilf(x1)) + 2 - l, int(ceilf(y1)) + 2 - t);
}

void circle(PrimList& L, V2 c, float r, gfx::Color col, uint8_t alpha = 32) {
  Prim* p = L.add();
  if (!p) return;
  p->kind = Prim::Circle;
  p->color = col;
  p->alpha = alpha;
  p->v[0] = c.x, p->v[1] = c.y, p->v[2] = r;
  p->box = boxOf(c.x - r, c.y - r, c.x + r, c.y + r);
}

// rx runs along axis
void ellipse(PrimList& L, V2 c, float rx, float ry, V2 axis, gfx::Color col, uint8_t alpha = 32) {
  Prim* p = L.add();
  if (!p) return;
  float n = gfx::sdf::len(axis.x, axis.y);
  float ca = n > 0 ? axis.x / n : 1, sa = n > 0 ? axis.y / n : 0;
  p->kind = Prim::Ellipse;
  p->color = col;
  p->alpha = alpha;
  p->v[0] = c.x, p->v[1] = c.y, p->v[2] = 1.0f / rx, p->v[3] = 1.0f / ry;
  p->v[4] = ca, p->v[5] = sa, p->v[6] = std::min(rx, ry);
  // Exact bounds of the rotated ellipse
  float hw = sqrtf(rx * rx * ca * ca + ry * ry * sa * sa), hh = sqrtf(rx * rx * sa * sa + ry * ry * ca * ca);
  p->box = boxOf(c.x - hw, c.y - hh, c.x + hw, c.y + hh);
}

void capsule(PrimList& L, V2 a, V2 b, float r, gfx::Color col) {
  Prim* p = L.add();
  if (!p) return;
  p->kind = Prim::Capsule;
  p->color = col;
  p->v[0] = a.x, p->v[1] = a.y, p->v[2] = b.x, p->v[3] = b.y, p->v[4] = r;
  p->box = boxOf(std::min(a.x, b.x) - r, std::min(a.y, b.y) - r, std::max(a.x, b.x) + r,
                 std::max(a.y, b.y) + r);
}

void tri(PrimList& L, V2 a, V2 b, V2 c, gfx::Color col) {
  Prim* p = L.add();
  if (!p) return;
  p->kind = Prim::Triangle;
  p->color = col;
  p->v[0] = a.x, p->v[1] = a.y, p->v[2] = b.x, p->v[3] = b.y, p->v[4] = c.x, p->v[5] = c.y;
  p->box = boxOf(std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::max({a.x, b.x, c.x}),
                 std::max({a.y, b.y, c.y}));
}

void heart(PrimList& L, V2 c, float size, gfx::Color col) {
  Prim* p = L.add();
  if (!p) return;
  p->kind = Prim::Heart;
  p->color = col;
  p->v[0] = c.x, p->v[1] = c.y, p->v[2] = size;
  p->box = boxOf(c.x - 0.65f * size, c.y - 0.65f * size, c.x + 0.65f * size, c.y + 0.55f * size);
}

void star(PrimList& L, V2 c, float r, gfx::Color col) {
  Prim* p = L.add();
  if (!p) return;
  p->kind = Prim::Star;
  p->color = col;
  p->v[0] = c.x, p->v[1] = c.y, p->v[2] = r, p->v[3] = 0.42f;
  p->box = boxOf(c.x - r, c.y - r, c.x + r, c.y + r);
}

void buildHead(const AccessorySlot& cfg, float sway, const BodyFrame& f, PrimList& L) {
  const float s = cfg.size * f.radius, theta = slotAngle(Slot::Head, cfg);
  switch (HeadItem(cfg.type)) {
    case HeadItem::Antenna: {
      Frame fr = frameAt(f, theta, 0.06f * s, 1, sway);
      capsule(L, fr.at(0, -0.05f * s), fr.at(0, 0.40f * s), 0.03f * s, cfg.color2);
      circle(L, fr.at(0, 0.48f * s), 0.095f * s, cfg.color);
      break;
    }
    case HeadItem::Sprout: {
      Frame fr = frameAt(f, theta, 0.06f * s, 1, sway);
      capsule(L, fr.at(0, -0.05f * s), fr.at(0, 0.20f * s), 0.028f * s, cfg.color2);
      ellipse(L, fr.at(-0.12f * s, 0.27f * s), 0.14f * s, 0.065f * s, fr.dir(-0.88f, 0.47f), cfg.color);
      ellipse(L, fr.at(0.12f * s, 0.27f * s), 0.14f * s, 0.065f * s, fr.dir(0.88f, 0.47f), cfg.color);
      break;
    }
    case HeadItem::Bow: {
      Frame fr = frameAt(f, theta, 0.04f * s, 1, sway);
      V2 knot = fr.at(0, 0.05f * s);
      tri(L, knot, fr.at(-0.30f * s, 0.20f * s), fr.at(-0.30f * s, -0.10f * s), cfg.color);
      tri(L, knot, fr.at(0.30f * s, 0.20f * s), fr.at(0.30f * s, -0.10f * s), cfg.color);
      circle(L, knot, 0.075f * s, cfg.color2);
      break;
    }
    case HeadItem::Ears:
      for (float side : {-1.0f, 1.0f}) {
        Frame fr = frameAt(f, theta + side * 0.62f, 0.05f * s, 1, sway);
        tri(L, fr.at(-0.15f * s, -0.04f * s), fr.at(0.15f * s, -0.04f * s), fr.at(0, 0.36f * s), cfg.color);
        tri(L, fr.at(-0.075f * s, 0.02f * s), fr.at(0.075f * s, 0.02f * s), fr.at(0, 0.24f * s), cfg.color2);
      }
      break;
    case HeadItem::Horns:
      for (float side : {-1.0f, 1.0f}) {
        Frame fr = frameAt(f, theta + side * 0.52f, 0.05f * s, 1, sway);
        tri(L, fr.at(-0.085f * s, -0.04f * s), fr.at(0.085f * s, -0.04f * s),
            fr.at(side * 0.06f * s, 0.32f * s), cfg.color);
      }
      break;
    case HeadItem::Crown: {
      Frame fr = frameAt(f, theta, 0.06f * s, 1, sway);
      const float tips[3][2] = {{-0.21f, 0.30f}, {0.0f, 0.36f}, {0.21f, 0.30f}};
      tri(L, fr.at(-0.27f * s, 0.02f * s), fr.at(-0.11f * s, 0.02f * s), fr.at(tips[0][0] * s, tips[0][1] * s), cfg.color);
      tri(L, fr.at(-0.12f * s, 0.02f * s), fr.at(0.12f * s, 0.02f * s), fr.at(tips[1][0] * s, tips[1][1] * s), cfg.color);
      tri(L, fr.at(0.11f * s, 0.02f * s), fr.at(0.27f * s, 0.02f * s), fr.at(tips[2][0] * s, tips[2][1] * s), cfg.color);
      capsule(L, fr.at(-0.25f * s, 0.03f * s), fr.at(0.25f * s, 0.03f * s), 0.055f * s, cfg.color);
      circle(L, fr.at(0, 0.12f * s), 0.04f * s, cfg.color2);
      for (const auto& t : tips) circle(L, fr.at(t[0] * s, t[1] * s), 0.03f * s, cfg.color2);
      break;
    }
    default:
      break;
  }
}

void buildBack(const AccessorySlot& cfg, float sway, float flap, const BodyFrame& f, PrimList& L) {
  const float s = cfg.size * f.radius, theta = slotAngle(Slot::Back, cfg);
  switch (BackItem(cfg.type)) {
    case BackItem::Wings:
      for (float side : {1.0f, -1.0f}) {
        float right = theta + 1.05f;
        float a = side > 0 ? right : kPi - right;  // mirror across the vertical axis
        Frame fr = frameAt(f, a, 0.10f * s, side, sway + flap * side);
        ellipse(L, fr.at(0, 0.28f * s), 0.32f * s, 0.15f * s, fr.dir(-0.25f, 1.0f), cfg.color);
        ellipse(L, fr.at(0.10f * s, 0.17f * s), 0.22f * s, 0.10f * s, fr.dir(0.2f, 1.0f), cfg.color2);
      }
      break;
    case BackItem::Fin: {
      Frame fr = frameAt(f, theta, 0.08f * s, 1, sway);
      tri(L, fr.at(-0.20f * s, -0.06f * s), fr.at(0.20f * s, -0.06f * s), fr.at(-0.08f * s, 0.42f * s), cfg.color);
      tri(L, fr.at(-0.10f * s, 0.0f), fr.at(0.10f * s, 0.0f), fr.at(-0.05f * s, 0.26f * s), cfg.color2);
      break;
    }
    case BackItem::Spikes:
      for (int k = -1; k <= 1; k++) {
        Frame fr = frameAt(f, theta + float(k) * 0.55f, 0.05f * s, 1, sway);
        float h = k == 0 ? 0.30f * s : 0.24f * s;
        tri(L, fr.at(-0.10f * s, -0.05f * s), fr.at(0.10f * s, -0.05f * s), fr.at(0, h), cfg.color);
      }
      break;
    default:
      break;
  }
}

// Arms go behind the body (they emerge from it); held items in front
void buildHand(Slot slot, const AccessorySlot& cfg, float sway, float wave, const BodyFrame& f,
               PrimList& back, PrimList& front) {
  if (HandItem(cfg.type) == HandItem::None) return;
  const float side = slot == Slot::Right ? 1.0f : -1.0f;
  const float s = cfg.size * f.radius, theta = slotAngle(slot, cfg);
  Frame fr = frameAt(f, theta, 0.08f * s, side, sway - wave * side);  // wave lifts the arm
  ellipse(back, fr.at(0, 0.10f * s), 0.16f * s, 0.095f * s, fr.dir(0, 1), f.bodyColor, f.bodyOpacity);

  V2 hand = fr.at(0, 0.24f * s);
  float swing = sinf(sway) * 0.3f * s;  // held items lag a little
  switch (HandItem(cfg.type)) {
    case HandItem::Heart:
      heart(front, {hand.x + swing * 0.5f, hand.y - 0.17f * s}, 0.34f * s, cfg.color);
      break;
    case HandItem::Star:
      star(front, {hand.x + swing * 0.5f, hand.y - 0.17f * s}, 0.19f * s, cfg.color);
      break;
    case HandItem::Flag: {
      V2 top{hand.x + swing * 0.3f, hand.y - 0.64f * s};
      capsule(front, {hand.x, hand.y + 0.04f * s}, top, 0.022f * s, cfg.color2);
      tri(front, top, {top.x, top.y + 0.26f * s}, {top.x + side * 0.32f * s, top.y + 0.13f * s}, cfg.color);
      break;
    }
    case HandItem::Balloon: {
      V2 b{hand.x + swing * 1.2f, hand.y - 0.92f * s};
      capsule(front, hand, {b.x, b.y + 0.20f * s}, 0.013f * s, cfg.color2);
      ellipse(front, b, 0.21f * s, 0.17f * s, {0, 1}, cfg.color);
      tri(front, {b.x, b.y + 0.19f * s}, {b.x - 0.04f * s, b.y + 0.25f * s}, {b.x + 0.04f * s, b.y + 0.25f * s},
          cfg.color);
      break;
    }
    default:
      break;
  }
}

}  // namespace

Accessories::Accessories() {
  AccessorySlot& head = slots[size_t(Slot::Head)];
  head.type = uint8_t(HeadItem::Sprout);
  head.color = gfx::rgb(120, 220, 120);
  head.color2 = gfx::rgb(70, 150, 80);
  AccessorySlot& back = slots[size_t(Slot::Back)];
  back.color = gfx::rgb(240, 248, 255);
  back.color2 = gfx::rgb(180, 215, 255);
  for (Slot hand : {Slot::Left, Slot::Right}) {
    slots[size_t(hand)].color = gfx::rgb(255, 90, 120);
    slots[size_t(hand)].color2 = gfx::rgb(200, 205, 225);
  }
}

Prim* PrimList::add() {
  if (count >= kMax) return nullptr;
  items[count] = Prim{};
  return &items[count++];
}

const char* slotName(Slot s) { return kSlotNames[size_t(s) % kSlotCount]; }

int itemCount(Slot s) {
  int n;
  namesOf(s, n);
  return n;
}

const char* itemName(Slot s, int type) {
  int n;
  const char* const* names = namesOf(s, n);
  return names[(type >= 0 && type < n) ? type : 0];
}

int findItem(Slot s, const char* name) {
  int n;
  const char* const* names = namesOf(s, n);
  for (int i = 0; i < n; i++) {
    if (strcmp(names[i], name) == 0) return i;
  }
  return -1;
}

float slotAngle(Slot s, const AccessorySlot& cfg) {
  float off = cfg.angle * kDeg;
  switch (s) {
    case Slot::Head:
    case Slot::Back:
      return -kPi / 2 + off;
    case Slot::Right:
      return 0.30f + off;
    case Slot::Left:
      return kPi - 0.30f - off;
    default:
      return 0;
  }
}

void buildAccessories(const Accessories& acc, const AccessoryMotion& m, const BodyFrame& f,
                      PrimList& back, PrimList& front) {
  back.clear();
  front.clear();
  buildBack(acc[Slot::Back], m.sway[size_t(Slot::Back)], m.flap, f, back);
  buildHand(Slot::Left, acc[Slot::Left], m.sway[size_t(Slot::Left)], m.wave, f, back, front);
  buildHand(Slot::Right, acc[Slot::Right], m.sway[size_t(Slot::Right)], m.wave, f, back, front);
  buildHead(acc[Slot::Head], m.sway[size_t(Slot::Head)], f, front);
  for (PrimList* L : {&back, &front}) {
    for (int i = 0; i < L->count; i++) L->bounds = L->bounds.unite(L->items[i].box);
  }
}

void addCircle(PrimList& L, float x, float y, float r, gfx::Color color, uint8_t alpha) {
  circle(L, {x, y}, r, color, alpha);
  if (L.count) L.bounds = L.bounds.unite(L.items[L.count - 1].box);
}

const char* browStyleName(BrowStyle s) { return kBrowNames[size_t(s) % size_t(BrowStyle::Count)]; }

int browStyleCount() { return int(BrowStyle::Count); }

int findBrowStyle(const char* name) {
  for (int i = 0; i < browStyleCount(); i++) {
    if (strcmp(kBrowNames[i], name) == 0) return i;
  }
  return -1;
}

void buildBrows(const BrowConfig& b, const EyeShape& s, const EyeParams eyes[2], PrimList& out) {
  if (BrowStyle(b.style) == BrowStyle::None) return;
  for (int e = 0; e < 2; e++) {
    const EyeParams& p = eyes[e];
    const float R = p.radius;
    const float hw = s.w * R * 0.5f * p.squashX, hh = s.h * R * 0.5f * p.squashY;
    const float out_ = p.mirror ? -1.0f : 1.0f;  // +x away from the face center for this eye
    const float half = b.length * hw, thick = b.thickness * R;
    const float cx = p.cx, cy = p.cy - hh - (b.lift + s.browRaise) * R;
    // Positive slant lowers the inner end
    const float ca = cosf(s.browAngle), sa = sinf(s.browAngle);
    V2 inner{cx - out_ * half * ca, cy + half * sa};
    V2 outer{cx + out_ * half * ca, cy - half * sa};
    switch (BrowStyle(b.style)) {
      case BrowStyle::Line:
        capsule(out, inner, outer, thick, b.color);
        break;
      case BrowStyle::Thick:
        capsule(out, inner, outer, thick * 1.8f, b.color);
        break;
      case BrowStyle::Arch: {  // quadratic curve bulging up, as three segments
        float ux = outer.x - inner.x, uy = outer.y - inner.y;
        V2 mid{(inner.x + outer.x) * 0.5f + uy * 0.35f * out_, (inner.y + outer.y) * 0.5f - fabsf(ux) * 0.35f};
        V2 prev = inner;
        for (int k = 1; k <= 3; k++) {
          float t = float(k) / 3.0f, it = 1 - t;
          V2 q{it * it * inner.x + 2 * it * t * mid.x + t * t * outer.x,
               it * it * inner.y + 2 * it * t * mid.y + t * t * outer.y};
          capsule(out, prev, q, thick, b.color);
          prev = q;
        }
        break;
      }
      case BrowStyle::Dot:
        ellipse(out, {cx, cy}, half * 0.45f, thick * 1.6f, {outer.x - inner.x, outer.y - inner.y}, b.color);
        break;
      default:
        break;
    }
  }
  for (int i = 0; i < out.count; i++) out.bounds = out.bounds.unite(out.items[i].box);
}

// One out-of-line dispatcher instead of a paint instantiation per kind: keeps
// code small and sidesteps an Xtensa GCC 8.4 internal compiler error that the
// fully inlined float code triggers at -O2.
__attribute__((noinline)) float primDist(const Prim& p, float x, float y) {
  namespace sdf = gfx::sdf;
  const float* v = p.v;
  switch (p.kind) {
    case Prim::Circle:
      return sdf::circle(x, y, v[0], v[1], v[2]);
    case Prim::Ellipse:
      return sdf::ellipse(x, y, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
    case Prim::Capsule:
      return sdf::capsule(x, y, {v[0], v[1]}, {v[2], v[3]}, v[4]);
    case Prim::Triangle:
      return sdf::triangle(x, y, {v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]});
    case Prim::Heart:
      return sdf::heart(x, y, v[0], v[1], v[2]);
    case Prim::Star:
      return sdf::star5(x, y, v[0], v[1], v[2], v[3]);
  }
  return 1e9f;
}

void paintPrims(gfx::Canvas& c, const PrimList& prims) {
  for (int i = 0; i < prims.count; i++) {
    const Prim& p = prims.items[i];
    if (!p.box.intersects(c.area())) continue;
    gfx::sdf::paint(c, p.box, p.color, p.alpha, [&p](float x, float y) { return primDist(p, x, y); });
  }
}

}  // namespace ui::blob
