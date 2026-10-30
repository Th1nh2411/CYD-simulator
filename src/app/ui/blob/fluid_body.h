#pragma once

#include <cstdint>

namespace ui::blob {

// Soft-body ring: kNodes radial offsets around a center. Each node is a damped
// spring toward the rest silhouette and is coupled to its neighbours (surface
// tension), so a poke travels around the surface as a ripple. Area is kept
// constant (a squash bulges the sides, a long tail shrinks the head) and an
// optional floor cannot be crossed. Gravity and the center's acceleration act
// on the surface. Units: screen pixels and seconds. No drawing here.
class FluidBody {
 public:
  static constexpr int kNodes = 64;

  struct Params {
    float stiffness = 160.0f;  // pull toward rest shape, 1/s^2
    float damping = 5.0f;      // 1/s
    float tension = 900.0f;    // neighbour coupling, 1/s^2
    float volumeKeep = 0.5f;   // fraction of the area error corrected per substep
    float inertia = 0.7f;      // response to gravity and center acceleration
    float maxAccel = 6000.0f;  // clamp for acceleration spikes, px/s^2
  };

  struct Input {
    float cy = 0;          // center y
    float ax = 0, ay = 0;  // center acceleration, y down
    float gravity = 0;     // y down
    float groundY = 1e9f;  // floor
    float breathe = 0;     // signed amplitude of the slow tall/wide cycle
    float tail = 0;        // tail extension beyond the outline (px), tapering to a point
    float tailAngle = 0;   // radians, y down
    // The tail comes on top of the body instead of out of it: the rest keeps
    // its own area, so a tail streaming behind (or settling) does not resize it
    bool tailApart = false;
    // A finger pulling the spot of the surface at dragAngle by (dragX, dragY):
    // the outline follows it there, less and less further round the body
    float dragX = 0, dragY = 0, dragAngle = 0;
    // The whole body keeps its rest area: pulled out it thins elsewhere,
    // pushed in it bulges
    bool keepArea = false;
  };

  void reset(float radius);
  void setParams(const Params& p) { p_ = p; }
  // Rest outline as per-node offsets from the radius (px); nullptr = circle
  void setRestShape(const float* offsets);
  void step(float dt, const Input& in);
  // Radial velocity kick centered on an angle (radians, y down); negative = inward
  void poke(float angle, float speed);
  // Impact along an axis: in along it, out across it (volume-neutral squash)
  void squash(float angle, float speed);

  float radius() const { return radius_; }
  float offset(int i) const { return d_[(i + kNodes) % kNodes]; }
  float restOffset(int i) const { return rest_[(i + kNodes) % kNodes]; }
  // Surface distance from the center at any angle, interpolated between nodes
  float radiusAt(float angle) const;
  // Same for the rest outline (ignores tail and wobble)
  float restRadiusAt(float angle) const;
  static float nodeAngle(int i);

 private:
  void dragOutline(const Input& in, float* target) const;
  // head: nodes that keep the area (all when null)
  void substep(float h, const Input& in, const float* target, float targetArea, const bool* head, float ax,
               float ay);

  Params p_;
  float radius_ = 1;
  float restArea_ = 3.14159265f;
  float d_[kNodes] = {}, v_[kNodes] = {}, rest_[kNodes] = {};
  float cos_[kNodes] = {}, sin_[kNodes] = {}, cos2_[kNodes] = {};
};

}  // namespace ui::blob
