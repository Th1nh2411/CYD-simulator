#include "app/ui/blob/body_shape.h"

#include <cmath>
#include <cstring>

namespace ui::blob {

namespace {

constexpr float kPi = 3.14159265f;
const char* const kNames[] = {"round", "egg", "mochi", "drop", "squircle", "cat", "bean"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(BodyShape::Count), "shape names");

// Shortest signed angle a - b
float angleDiff(float a, float b) { return atan2f(sinf(a - b), cosf(a - b)); }

float ellipseR(float c, float s, float ax, float ay) {
  return 1.0f / sqrtf((c / ax) * (c / ax) + (s / ay) * (s / ay));
}

float bump(float theta, float at, float width) {
  float d = angleDiff(theta, at) / width;
  return expf(-d * d);
}

// Unscaled radius factor; y down, so sin < 0 is the top
float profile(BodyShape shape, float theta) {
  float c = cosf(theta), s = sinf(theta);
  float up = s < 0 ? -s : 0.0f, down = s > 0 ? s : 0.0f;
  switch (shape) {
    case BodyShape::Egg:
      return ellipseR(c, s, 0.95f, 1.08f) * (1.0f + 0.07f * s);
    case BodyShape::Mochi:
      return ellipseR(c, s, 1.2f, 0.84f) * (1.0f - 0.06f * down * down * down * down);
    case BodyShape::Drop: {
      float tip = up * up * up * up * up * up * up;
      return 1.0f + 0.55f * tip;
    }
    case BodyShape::Squircle: {
      float c4 = c * c * c * c, s4 = s * s * s * s;
      return 1.0f / sqrtf(sqrtf(c4 + s4));
    }
    case BodyShape::Cat:
      return 1.0f + 0.30f * (bump(theta, -kPi / 2 - 0.62f, 0.16f) + bump(theta, -kPi / 2 + 0.62f, 0.16f));
    case BodyShape::Bean:
      return ellipseR(c, s, 1.12f, 0.92f) * (1.0f - 0.08f * bump(theta, -kPi / 2, 0.35f));
    case BodyShape::Round:
    default:
      return 1.0f;
  }
}

}  // namespace

int bodyShapeCount() { return int(BodyShape::Count); }

const char* bodyShapeName(BodyShape s) { return kNames[size_t(s) % size_t(BodyShape::Count)]; }

int findBodyShape(const char* name) {
  for (int i = 0; i < bodyShapeCount(); i++) {
    if (strcmp(kNames[i], name) == 0) return i;
  }
  return -1;
}

void bodyRestOffsets(BodyShape shape, float aspect, float radius, float* out, int n) {
  const float ax = sqrtf(aspect), ay = 1.0f / ax;
  float sumR2 = 0;
  for (int i = 0; i < n; i++) {
    float theta = 2 * kPi * float(i) / float(n);
    float r = profile(shape, theta) * ellipseR(cosf(theta), sinf(theta), ax, ay);
    out[i] = r;
    sumR2 += r * r;
  }
  // Match the area of the unit circle: area ~ 0.5 * sum(r^2) * dTheta
  float area = 0.5f * sumR2 * (2 * kPi / float(n));
  float k = sqrtf(kPi / area);
  for (int i = 0; i < n; i++) out[i] = (out[i] * k - 1.0f) * radius;
}

}  // namespace ui::blob
