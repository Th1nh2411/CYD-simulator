#pragma once

#include <cstdint>

#include "app/engine/world.h"
#include "app/ui/blob/accessory.h"
#include "app/ui/blob/blob_style.h"
#include "app/ui/blob/eye.h"
#include "app/ui/blob/fluid_body.h"

namespace ui::blob {

struct Face {
  float gazeX = 0, gazeY = 0;  // -1..1
  float blink = 0;             // 0 open .. 1 closed
  EyeShape eye;                // current (possibly mid-morph) shape
};

// A small drop that pinched off the tail; follows the body and merges back
struct Droplet {
  bool active = false;
  float x = 0, y = 0, vx = 0, vy = 0, r = 0;
  uint32_t bornMs = 0;
};

// Turns intent into motion. When the layout moves the anchor, the body
// glides there: it speeds up ("swoosh") stretched into a droplet with the
// tail behind, then stops with an impact squash ("thud") and the surface
// settles. Also runs bounces, breathing, blinking, gaze, expression morphs
// and the secondary motion of accessories. Purely presentational.
class BlobAnimator {
 public:
  void begin(const CharacterConfig& cfg, float radius, uint32_t seed);
  void configure(const CharacterConfig& cfg, float radius);

  void setAnchor(float x, float y);
  // Eyes follow a point while active (e.g. the card that just appeared)
  void setAttention(bool active, float x = 0, float y = 0);
  // Force an expression until untilMs (previews); pass -1 to clear
  void overrideExpression(int expression, uint32_t untilMs);

  void update(engine::Mood mood, uint32_t moodSinceMs, uint32_t nowMs);
  // Screen-space poke, e.g. a tap on the body
  void poke(float x, float y);
  // A quick small squeeze from above (it ducks), e.g. a long press on the body
  void flinch();
  // Grabbed by a finger at a point. Carried: the body follows the finger (held
  // where it was grabbed) and stretches behind it; let go, it glides back to
  // the anchor. Otherwise the body stays; the grabbed spot and the surface
  // round it follow the finger, out or in; let go, they go back.
  void grab(float x, float y, bool carry);
  void drag(float x, float y);
  void letGo();
  // Device tilt (Character::setTilt): the body slides downhill on a soft
  // spring, its surface leans that way and sloshes, the eyes look down the slope
  void setTilt(float x, float y);

  const FluidBody& body() const { return body_; }
  float centerX() const { return px_; }
  float centerY() const { return py_; }
  const Face& face() const { return face_; }
  const AccessoryMotion& accessoryMotion() const { return acc_; }
  static constexpr int kMaxDroplets = 2;
  const Droplet& droplet(int i) const { return drops_[i]; }
  // True while something fast is happening (glide, bounce, poke ripple);
  // idle loops (breathing, flapping, blinking) look fine at a lower frame rate
  bool busy(uint32_t nowMs) const { return int32_t(nowMs - busyUntilMs_) < 0; }

 private:
  struct Glide {
    bool active = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t startMs = 0;
    float durationS = 0.4f;
  };

  void enterMood(uint32_t now);
  void startGlide(uint32_t now);
  Expression pickExpression(uint32_t now, uint32_t inMood) const;
  void updateFace(uint32_t now, uint32_t inMood, float dt);
  void updateAccessories(float dt, float ax, float ay, uint32_t now);
  // Tail tip on a soft spring behind the center; returns tail length (px) and angle
  void updateTail(float dt, uint32_t now, float& tail, float& angle);
  void updateDroplets(float dt, uint32_t now);
  // Carried: the body on a stiff spring to the finger; returns the acceleration felt
  void followFinger(float dt, float& ax, float& ay);
  // Stretch drag: how far the spot where the drag started is pulled (or goes
  // back once let go)
  void pullOut(float dt);
  float randf();  // 0..1

  FluidBody body_;
  Face face_;
  AccessoryMotion acc_;
  float swayVel_[kSlotCount] = {};
  float slotAngles_[kSlotCount] = {};
  float flapPhase_ = 0;
  const EyeShape* eyes_ = nullptr;  // per-expression shapes, owned by the config
  MotionParams motion_;
  float radius_ = 1;

  float anchorX_ = 0, anchorY_ = 0;
  float baseX_ = 0, baseY_ = 0;  // glide position, before hover and bounce
  float px_ = 0, py_ = 0;        // final center
  Glide glide_;
  bool placed_ = false;
  bool held_ = false, heldMoving_ = false;  // carried by a finger, and moving with it
  bool stretch_ = false;                    // held in place, pulled out (not carried)
  float dragX_ = 0, dragY_ = 0;              // how far that spot is pulled (px)
  float pullRoot_ = 0;                      // where on the body it is pulled from (angle)
  bool rooted_ = false;                     // pullRoot_ is set: grabbed off the middle, or pulled once
  bool returning_ = false;                  // let go after being carried: gliding home
  float holdX_ = 0, holdY_ = 0;             // the finger
  float grabDx_ = 0, grabDy_ = 0;           // where it was grabbed, from the center
  float holdVx_ = 0, holdVy_ = 0;

  float tailX_ = 0, tailY_ = 0, tailVx_ = 0, tailVy_ = 0;  // lagging tail point
  // Tilt: until the first one, and again once it has settled, none of this
  // takes part in the arithmetic (a board without a sensor animates as before)
  bool tilting_ = false;
  float tiltX_ = 0, tiltY_ = 0;
  float slideX_ = 0, slideY_ = 0, slideVx_ = 0, slideVy_ = 0;  // offset from the glide position
  float glideDirX_ = 0, glideDirY_ = 0;                    // unit direction of the last glide
  Droplet drops_[kMaxDroplets];
  uint32_t lastDropMs_ = 0;

  float bounce_ = 0, bounceVel_ = 0;  // vertical offset spring for happy bounces
  float bounceSpeed_ = 0;
  uint32_t bounceEveryMs_ = 0, nextBounceMs_ = 0;
  engine::Mood mood_ = engine::Mood::Idle;
  uint32_t moodSince_ = 0xFFFFFFFFu;
  uint32_t lastMs_ = 0;
  uint32_t busyUntilMs_ = 0;
  bool started_ = false;

  bool attention_ = false;
  float attentionX_ = 0, attentionY_ = 0;
  int override_ = -1;
  uint32_t overrideUntilMs_ = 0;

  Expression expr_ = Expression::Normal;
  EyeShape morphFrom_;
  uint32_t morphStartMs_ = 0;

  uint32_t blinkStartMs_ = 0, nextBlinkMs_ = 0;
  uint32_t nextGazeMs_ = 0, nextRippleMs_ = 0;
  float gazeTargetX_ = 0, gazeTargetY_ = 0;
  uint32_t rng_ = 1;
};

}  // namespace ui::blob
