#include "app/ui/blob/blob_animator.h"

#include <algorithm>
#include <cmath>

namespace ui::blob {

namespace {
constexpr float kPi = 3.14159265f;

// In body radii, so behaviour is independent of screen size
constexpr float kBreathe = 0.03f;
constexpr float kHover = 0.035f;  // idle float amplitude
constexpr float kPokeSpeed = 2.2f;
constexpr float kFlinchSquash = 2.2f;  // squeeze speed of a flinch (R/s)
constexpr Expression kCarriedFace = Expression::Surprised;  // while carried around by a finger
constexpr float kThudSquash = 0.18f;  // impact squash speed, of the arrival speed (a light "thud")
constexpr float kGlideFeel = 0.12f;  // share of glide acceleration the head surface feels

constexpr uint32_t kBlinkMs = 140;
constexpr uint32_t kMorphMs = 160;
constexpr uint32_t kSurprisedMs = 900;
constexpr uint32_t kBounceEveryMs = 520;
constexpr float kBounceOmega = 13.0f, kBounceZeta = 0.38f;

// Tail: a point on a spring behind the glide path; its lag is the tail length.
// Very soft while gliding so the head races ahead and the back cannot keep up;
// stiff and well damped once stopped so the tail rushes in and merges without
// overshooting into the head.
constexpr float kTailOmegaGlide = 4.0f, kTailZetaGlide = 0.45f;
constexpr float kTailOmegaSettle = 10.0f, kTailZetaSettle = 0.85f;
constexpr float kTailAhead = 0.15f;  // after a stop, how much a tail that swung past the center still shows
constexpr float kTailMax = 3.0f;  // R
// Droplets pinch off long tails, then drift back and merge
constexpr float kDropSpawnTail = 1.6f;  // R
constexpr float kDropRadius = 0.15f;    // R
constexpr float kDropOmega = 4.5f, kDropZeta = 0.55f;
constexpr uint32_t kDropLifeMs = 2500, kDropGapMs = 350;

// Carried by a finger: a stiff spring (substeps keep it stable at 30 fps). The
// pull a fast finger gives is far above a glide's: what the body feels is capped.
constexpr float kHoldOmega = 28.0f, kHoldZeta = 0.8f;
constexpr int kHoldSteps = 4;
constexpr float kHoldFeelMax = 120.0f;  // R/s^2
constexpr float kHoldMoving = 1.5f;     // R/s: faster than this the tail trails as in a glide
// Pulled instead (not carried): the spot where the drag started, and
// the surface round it, follow the finger. Out it goes freely at first, then
// less and less up to kPullOut (R); in and sideways less, up to kPushIn and
// kPullSide, so the outline neither folds over nor goes through the body.
constexpr float kPullOut = 0.8f, kPushIn = 0.45f, kPullSide = 0.8f;
constexpr float kRootPull = 0.06f;  // R: grabbed near the middle, a pull this long picks the side
// Let go, the pull goes back this fast (s), so the surface flows back rather
// than snapping
constexpr float kSnapBackS = 0.12f;

// Accessory secondary motion (angular spring driven by body acceleration)
constexpr float kSwayK = 70.0f, kSwayC = 7.0f, kSwayGain = 0.22f, kSwayMax = 0.7f;
constexpr float kLurch = 0.8f;  // forward whip of accessories on a sudden stop
// Tilt: how far the body slides per unit of tilt (R), the sloshy spring that
// carries it there, the share of that spring's acceleration the surface feels,
// and the steady downhill pull on the surface (R/s^2 per unit of tilt)
constexpr float kTiltSlide = 1.0f;
constexpr float kTiltOmega = 6.0f, kTiltZeta = 0.35f;
constexpr float kTiltFeel = 0.8f;
constexpr float kTiltPull = 30.0f;
constexpr float kTiltLook = 0.15f;  // tilt past which the eyes look down the slope

float ease(float t) { return t >= 1 ? 1 : 1 - (1 - t) * (1 - t) * (1 - t); }
float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
}  // namespace

void BlobAnimator::begin(const CharacterConfig& cfg, float radius, uint32_t seed) {
  rng_ = seed ? seed : 1;
  configure(cfg, radius);
  face_.eye = cfg.eyes[size_t(Expression::Normal)];
  morphFrom_ = face_.eye;
}

void BlobAnimator::configure(const CharacterConfig& cfg, float radius) {
  eyes_ = cfg.eyes;
  motion_ = cfg.motion;
  body_.setParams(cfg.physics);
  if (!started_ || fabsf(radius - radius_) > 0.01f) {
    radius_ = radius;
    body_.reset(radius);
  }
  // The body flows into a newly chosen silhouette rather than snapping
  float rest[FluidBody::kNodes];
  bodyRestOffsets(BodyShape(cfg.bodyShape), cfg.aspect, radius_, rest, FluidBody::kNodes);
  body_.setRestShape(rest);
  for (size_t i = 0; i < kSlotCount; i++) slotAngles_[i] = slotAngle(Slot(i), cfg.accessories.slots[i]);
  morphFrom_ = face_.eye;  // glide to any edited eye shape
  morphStartMs_ = lastMs_;
}

void BlobAnimator::setAnchor(float x, float y) {
  anchorX_ = x;
  anchorY_ = y;
}

void BlobAnimator::setAttention(bool active, float x, float y) {
  attention_ = active;
  attentionX_ = x;
  attentionY_ = y;
}

void BlobAnimator::overrideExpression(int expression, uint32_t untilMs) {
  override_ = expression;
  overrideUntilMs_ = untilMs;
}

float BlobAnimator::randf() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(rng_ & 0xFFFFFF) / float(0x1000000);
}

void BlobAnimator::enterMood(uint32_t now) {
  bounceSpeed_ = 0;
  bounceEveryMs_ = 0;
  if (mood_ == engine::Mood::Happy) {
    bounceSpeed_ = motion_.bounce * radius_;
    bounceEveryMs_ = kBounceEveryMs;
  }
  nextBounceMs_ = now;
}

void BlobAnimator::startGlide(uint32_t now) {
  glide_.active = true;
  glide_.x0 = baseX_;
  glide_.y0 = baseY_;
  glide_.x1 = anchorX_;
  glide_.y1 = anchorY_;
  glide_.startMs = now;
  float dist = sqrtf((glide_.x1 - glide_.x0) * (glide_.x1 - glide_.x0) +
                     (glide_.y1 - glide_.y0) * (glide_.y1 - glide_.y0));
  if (dist > 0.5f) {
    glideDirX_ = (glide_.x1 - glide_.x0) / dist;
    glideDirY_ = (glide_.y1 - glide_.y0) / dist;
  }
  glide_.durationS = motion_.glideMs / 1000.0f * clampf(0.6f + 0.4f * dist / (2 * radius_), 0.6f, 1.5f);
}

void BlobAnimator::update(engine::Mood mood, uint32_t moodSinceMs, uint32_t now) {
  float dt = started_ ? std::min(0.05f, float(int32_t(now - lastMs_)) / 1000.0f) : 0.0f;
  if (dt < 0) dt = 0;
  lastMs_ = now;
  started_ = true;
  if (!placed_) {
    baseX_ = tailX_ = anchorX_;
    baseY_ = tailY_ = anchorY_;
    placed_ = true;
  }

  if (mood != mood_ || moodSinceMs != moodSince_) {
    mood_ = mood;
    moodSince_ = moodSinceMs;
    enterMood(now);
  }
  int32_t sinceMood = int32_t(now - moodSince_);
  uint32_t inMood = sinceMood > 0 ? uint32_t(sinceMood) : 0;

  // A new anchor starts a glide from wherever the body is now (once let go, back home)
  float goalX = glide_.active ? glide_.x1 : baseX_, goalY = glide_.active ? glide_.y1 : baseY_;
  if (!held_ && (fabsf(anchorX_ - goalX) > 0.5f || fabsf(anchorY_ - goalY) > 0.5f)) startGlide(now);

  // Glide: s(u) blends smoothstep (soft stop) with u^2 (arrives at speed); thud picks the mix.
  // The head barely feels the glide (stays round); the tail and accessories carry the motion.
  float gax = 0, gay = 0;
  if (held_ && !stretch_) followFinger(dt, gax, gay);
  if (glide_.active) {
    const float D = glide_.durationS, k = clampf(motion_.thud, 0, 1);
    const float dx = glide_.x1 - glide_.x0, dy = glide_.y1 - glide_.y0;
    float u = float(now - glide_.startMs) / 1000.0f / D;
    if (u >= 1) {
      // Arrival: dead stop. Squash along the motion, accessories whip forward
      float speedFactor = 2 * k / D;
      float vx = dx * speedFactor, vy = dy * speedFactor;
      float speed = sqrtf(vx * vx + vy * vy);
      baseX_ = glide_.x1;
      baseY_ = glide_.y1;
      glide_.active = false;
      if (speed > 1) {
        body_.squash(atan2f(dy, dx), kThudSquash * std::min(speed, 6 * radius_));
        for (size_t i = 0; i < kSlotCount; i++) {
          float tx = -sinf(slotAngles_[i]), ty = cosf(slotAngles_[i]);
          swayVel_[i] += (vx * tx + vy * ty) / radius_ * kLurch;
        }
      }
    } else {
      float s = (1 - k) * (3 * u * u - 2 * u * u * u) + k * u * u;
      float s2 = (1 - k) * (6 - 12 * u) + 2 * k;  // second derivative: felt by surface and tail
      baseX_ = glide_.x0 + dx * s;
      baseY_ = glide_.y0 + dy * s;
      gax = dx * s2 / (D * D);
      gay = dy * s2 / (D * D);
    }
  }

  // Happy bounces: velocity kicks on a vertical offset spring (felt fully: jelly bounce)
  float bay = 0;
  if (bounceSpeed_ > 0 && int32_t(now - nextBounceMs_) >= 0) {
    bounceVel_ -= bounceSpeed_;
    if (dt > 0) bay -= bounceSpeed_ / dt;  // the kick itself is felt by the surface
    if (bounceEveryMs_) nextBounceMs_ = now + bounceEveryMs_;
    else bounceSpeed_ = 0;
  }
  float ab = -kBounceOmega * kBounceOmega * bounce_ - 2 * kBounceZeta * kBounceOmega * bounceVel_;
  bounceVel_ += ab * dt;
  bounce_ += bounceVel_ * dt;
  bay += ab;

  // Tilt: a soft spring slides the body downhill; its acceleration and a
  // steady downhill pull reach the surface. Settled back level, it drops out.
  float tax = 0, tay = 0;
  if (tilting_) {
    const float h = dt * 0.5f;  // two substeps, as the tail
    for (int i = 0; i < 2; i++) {
      const float tx = tiltX_ * kTiltSlide * radius_, ty = tiltY_ * kTiltSlide * radius_;
      const float sax = kTiltOmega * kTiltOmega * (tx - slideX_) - 2 * kTiltZeta * kTiltOmega * slideVx_;
      const float say = kTiltOmega * kTiltOmega * (ty - slideY_) - 2 * kTiltZeta * kTiltOmega * slideVy_;
      slideVx_ += sax * h;
      slideVy_ += say * h;
      slideX_ += slideVx_ * h;
      slideY_ += slideVy_ * h;
      tax = sax * kTiltFeel - tiltX_ * kTiltPull * radius_;
      tay = say * kTiltFeel - tiltY_ * kTiltPull * radius_;
    }
    const float still = 0.02f * radius_;
    if (tiltX_ == 0 && tiltY_ == 0 && fabsf(slideX_) < still && fabsf(slideY_) < still &&
        fabsf(slideVx_) < still && fabsf(slideVy_) < still) {
      tilting_ = false;
      slideX_ = slideY_ = slideVx_ = slideVy_ = 0;
    } else {
      busyUntilMs_ = now + 700;
    }
  }

  bool idle = mood_ == engine::Mood::Idle && !glide_.active && !held_;
  float hover = idle ? kHover * radius_ * sinf(float(now % 3400) * (2 * kPi / 3400.0f)) : 0.0f;
  px_ = baseX_;
  py_ = baseY_ + bounce_ + hover;
  if (tilting_) {
    px_ += slideX_;
    py_ += slideY_;
  }

  float tail = 0, tailAngle = 0;
  updateTail(dt, now, tail, tailAngle);
  pullOut(dt);
  const bool pulled = dragX_ * dragX_ + dragY_ * dragY_ > 0.25f;
  updateDroplets(dt, now);
  bool dropsActive = drops_[0].active || drops_[1].active;
  // Fast motion keeps the frame rate up a little past its end so the settle wobble stays smooth
  if (!idle || fabsf(bounceVel_) > 0.5f * radius_ || tail > 0.05f * radius_ || dropsActive || pulled) {
    busyUntilMs_ = now + 700;
  }

  float breathe = 0;
  if (idle) {
    breathe = kBreathe * radius_ * sinf(float(inMood % 3200) * (2 * kPi / 3200.0f));
    if (int32_t(now - nextRippleMs_) >= 0) {
      if (nextRippleMs_) body_.poke(randf() * 2 * kPi, -0.6f * radius_);
      nextRippleMs_ = now + 4000 + uint32_t(randf() * 5000);
    }
  }

  FluidBody::Input in;
  in.cy = py_;
  in.ax = gax * kGlideFeel;
  in.ay = gay * kGlideFeel + bay;
  if (tilting_) {
    in.ax += tax;
    in.ay += tay;
  }
  in.gravity = 0;
  in.groundY = 1e9f;
  in.breathe = breathe;
  in.tail = tail;
  in.tailAngle = tailAngle;
  // Pulled, the body keeps its area. Carried and on the way back home, the
  // tail comes on top of the body so the body keeps its size (the layout's
  // glides keep their look).
  if (returning_ && !glide_.active && tail < 0.05f * radius_) returning_ = false;
  in.dragX = dragX_;
  in.dragY = dragY_;
  in.dragAngle = pullRoot_;
  in.keepArea = pulled;
  in.tailApart = held_ || returning_;
  body_.step(dt, in);

  updateFace(now, inMood, dt);
  updateAccessories(dt, gax, gay + bay, now);
}

void BlobAnimator::updateTail(float dt, uint32_t now, float& tail, float& angle) {
  // The tail follows the glide path only (not bounces or idle hover), so it
  // appears when the body travels to a new spot and nowhere else.
  // Two substeps keep the soft spring stable at 30 fps.
  const bool travelling = glide_.active || heldMoving_;
  const float w = travelling ? kTailOmegaGlide : kTailOmegaSettle;
  const float z = travelling ? kTailZetaGlide : kTailZetaSettle, h = dt * 0.5f;
  for (int i = 0; i < 2; i++) {
    float ax = w * w * (baseX_ - tailX_) - 2 * z * w * tailVx_;
    float ay = w * w * (baseY_ - tailY_) - 2 * z * w * tailVy_;
    tailVx_ += ax * h;
    tailVy_ += ay * h;
    tailX_ += tailVx_ * h;
    tailY_ += tailVy_ * h;
  }
  float dx = tailX_ - baseX_, dy = tailY_ - baseY_;
  float lag = sqrtf(dx * dx + dy * dy);
  tail = std::min(kTailMax * radius_, lag * motion_.drop);
  angle = atan2f(dy, dx);
  // Once stopped, a tail swinging past the center must not bulge the head's front
  if (!travelling && dx * glideDirX_ + dy * glideDirY_ > 0) tail *= kTailAhead;

  // A long tail pinches a droplet off its tip
  if (motion_.splash > 0.5f && tail > kDropSpawnTail * radius_ && int32_t(now - lastDropMs_) > int32_t(kDropGapMs)) {
    for (Droplet& d : drops_) {
      if (d.active) continue;
      float reach = body_.restRadiusAt(angle) + tail * 1.08f;
      d.active = true;
      d.x = px_ + reach * cosf(angle);
      d.y = py_ + reach * sinf(angle);
      d.vx = tailVx_;
      d.vy = tailVy_;
      d.r = kDropRadius * radius_;
      d.bornMs = now;
      lastDropMs_ = now;
      break;
    }
  }
}

void BlobAnimator::updateDroplets(float dt, uint32_t now) {
  for (Droplet& d : drops_) {
    if (!d.active) continue;
    const float w = kDropOmega, z = kDropZeta;
    float ax = w * w * (px_ - d.x) - 2 * z * w * d.vx;
    float ay = w * w * (py_ - d.y) - 2 * z * w * d.vy;
    d.vx += ax * dt;
    d.vy += ay * dt;
    d.x += d.vx * dt;
    d.y += d.vy * dt;
    // Merge once it touches the body again: a small bump and ripple where it lands
    float dx = d.x - px_, dy = d.y - py_;
    float a = atan2f(dy, dx);
    if (sqrtf(dx * dx + dy * dy) < body_.radiusAt(a) || now - d.bornMs > kDropLifeMs) {
      d.active = false;
      body_.poke(a, 0.9f * radius_);
    }
  }
}

void BlobAnimator::updateAccessories(float dt, float ax, float ay, uint32_t now) {
  for (size_t i = 0; i < kSlotCount; i++) {
    float tx = -sinf(slotAngles_[i]), ty = cosf(slotAngles_[i]);
    float torque = -(ax * tx + ay * ty) / radius_ * kSwayGain;  // lags behind the acceleration
    float acc = -kSwayK * acc_.sway[i] - kSwayC * swayVel_[i] + torque;
    swayVel_[i] += acc * dt;
    acc_.sway[i] = clampf(acc_.sway[i] + swayVel_[i] * dt, -kSwayMax, kSwayMax);
  }
  bool moving = glide_.active || heldMoving_;
  flapPhase_ += dt * (moving ? 18.0f : 4.0f);
  acc_.flap = (moving ? 0.35f : 0.12f) * sinf(flapPhase_);
  float wave = mood_ == engine::Mood::Happy ? 0.35f * (0.5f + 0.5f * sinf(float(now % 450) * (2 * kPi / 450.0f))) : 0.0f;
  acc_.wave += (wave - acc_.wave) * std::min(1.0f, dt * 12.0f);
}

Expression BlobAnimator::pickExpression(uint32_t now, uint32_t inMood) const {
  if (override_ >= 0 && int32_t(overrideUntilMs_ - now) > 0) return Expression(override_);
  if (held_ && !stretch_) return kCarriedFace;
  switch (mood_) {
    case engine::Mood::Happy:
      return Expression::Happy;
    case engine::Mood::Alert:
      return inMood < kSurprisedMs ? Expression::Surprised : Expression::Normal;
    case engine::Mood::Idle:
      break;
  }
  return Expression::Normal;
}

void BlobAnimator::updateFace(uint32_t now, uint32_t inMood, float dt) {
  Expression e = pickExpression(now, inMood);
  if (e != expr_) {
    morphFrom_ = face_.eye;
    morphStartMs_ = now;
    expr_ = e;
  }
  float m = std::min(1.0f, float(now - morphStartMs_) / kMorphMs);
  face_.eye = lerp(morphFrom_, eyes_[size_t(expr_)], ease(m));

  // Blinks on a random schedule, sometimes doubled; arcs (happy) do not blink
  if (int32_t(now - nextBlinkMs_) >= 0) {
    blinkStartMs_ = now;
    nextBlinkMs_ = now + (randf() < 0.15f ? 260 : 2200 + uint32_t(randf() * 3800));
  }
  uint32_t b = now - blinkStartMs_;
  float u = float(b) / kBlinkMs;
  face_.blink = (b < kBlinkMs && face_.eye.arc < 0.5f) ? 1.0f - fabsf(2 * u - 1) : 0.0f;

  if (attention_) {
    float dx = attentionX_ - px_, dy = attentionY_ - py_;
    float len = sqrtf(dx * dx + dy * dy);
    gazeTargetX_ = len > 1 ? 0.9f * dx / len : 0;
    gazeTargetY_ = len > 1 ? 0.9f * dy / len : 0;
  } else if (tilting_ && tiltX_ * tiltX_ + tiltY_ * tiltY_ > kTiltLook * kTiltLook) {
    const float len = sqrtf(tiltX_ * tiltX_ + tiltY_ * tiltY_);
    gazeTargetX_ = 0.9f * tiltX_ / len;
    gazeTargetY_ = 0.9f * tiltY_ / len;
  } else if (mood_ == engine::Mood::Idle) {
    if (int32_t(now - nextGazeMs_) >= 0) {
      bool center = randf() < 0.4f;
      gazeTargetX_ = center ? 0 : (randf() * 1.6f - 0.8f);
      gazeTargetY_ = center ? 0 : (randf() * 1.0f - 0.5f);
      nextGazeMs_ = now + 1200 + uint32_t(randf() * 2300);
    }
  } else {
    gazeTargetX_ = 0;
    gazeTargetY_ = -0.2f;
  }
  float k = std::min(1.0f, dt * 9.0f);
  face_.gazeX += (gazeTargetX_ - face_.gazeX) * k;
  face_.gazeY += (gazeTargetY_ - face_.gazeY) * k;
}

void BlobAnimator::poke(float x, float y) {
  body_.poke(atan2f(y - py_, x - px_), -kPokeSpeed * radius_);
  busyUntilMs_ = lastMs_ + 900;
}

void BlobAnimator::setTilt(float x, float y) {
  tiltX_ = x;
  tiltY_ = y;
  if (x != 0 || y != 0) tilting_ = true;
}

void BlobAnimator::flinch() {
  body_.squash(0.5f * kPi, kFlinchSquash * radius_);
  busyUntilMs_ = lastMs_ + 600;
}

void BlobAnimator::grab(float x, float y, bool carry) {
  held_ = true;
  stretch_ = !carry;
  glide_.active = false;  // caught mid-glide: from where it is
  grabDx_ = x - baseX_;
  grabDy_ = y - baseY_;
  holdX_ = x;
  holdY_ = y;
  holdVx_ = holdVy_ = 0;
  // Grabbed near the middle, the side is picked by the first pull instead
  rooted_ = grabDx_ * grabDx_ + grabDy_ * grabDy_ >= 0.0625f * radius_ * radius_;
  pullRoot_ = atan2f(grabDy_, grabDx_);
}

void BlobAnimator::drag(float x, float y) {
  holdX_ = x;
  holdY_ = y;
}

void BlobAnimator::letGo() {
  returning_ = held_ && !stretch_;
  held_ = heldMoving_ = stretch_ = false;
}

void BlobAnimator::followFinger(float dt, float& ax, float& ay) {
  const float w = kHoldOmega, z = kHoldZeta, h = dt / kHoldSteps;
  for (int i = 0; i < kHoldSteps; i++) {
    float sx = w * w * (holdX_ - grabDx_ - baseX_) - 2 * z * w * holdVx_;
    float sy = w * w * (holdY_ - grabDy_ - baseY_) - 2 * z * w * holdVy_;
    holdVx_ += sx * h;
    holdVy_ += sy * h;
    baseX_ += holdVx_ * h;
    baseY_ += holdVy_ * h;
    ax += sx / kHoldSteps;
    ay += sy / kHoldSteps;
  }
  float a = sqrtf(ax * ax + ay * ay), aMax = kHoldFeelMax * radius_;
  if (a > aMax) {
    ax *= aMax / a;
    ay *= aMax / a;
  }
  // Moving with the finger the tail trails as in a glide; stopped, it settles
  float v = sqrtf(holdVx_ * holdVx_ + holdVy_ * holdVy_);
  heldMoving_ = v > kHoldMoving * radius_;
  if (heldMoving_) {
    glideDirX_ = holdVx_ / v;
    glideDirY_ = holdVy_ / v;
  }
}

void BlobAnimator::pullOut(float dt) {
  if (!held_ || !stretch_) {  // let go (or never pulled): it goes back
    const float k = expf(-dt / kSnapBackS);
    dragX_ *= k;
    dragY_ *= k;
    if (dragX_ * dragX_ + dragY_ * dragY_ < 0.25f) dragX_ = dragY_ = 0;
    return;
  }
  const float fx = holdX_ - baseX_ - grabDx_, fy = holdY_ - baseY_ - grabDy_;  // the pull
  if (!rooted_ && fx * fx + fy * fy >= kRootPull * kRootPull * radius_ * radius_) {
    pullRoot_ = atan2f(fy, fx);
    rooted_ = true;
  }
  if (!rooted_) return;
  // The pull split into out (from the body) and sideways at that spot, each
  // giving less and less as it grows
  const float ca = cosf(pullRoot_), sa = sinf(pullRoot_);
  float out = fx * ca + fy * sa, side = fy * ca - fx * sa;
  const float outMost = (out > 0 ? kPullOut : kPushIn) * radius_, sideMost = kPullSide * radius_;
  out = outMost * tanhf(out / outMost);
  side = sideMost * tanhf(side / sideMost);
  dragX_ = out * ca - side * sa;
  dragY_ = out * sa + side * ca;
}

}  // namespace ui::blob
