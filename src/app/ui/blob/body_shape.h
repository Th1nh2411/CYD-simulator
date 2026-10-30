#pragma once

#include <cstdint>

namespace ui::blob {

// Rest silhouette of the fluid body (the "head" shape). The soft body springs
// back to this outline; motion stretches it, then it returns.
enum class BodyShape : uint8_t { Round, Egg, Mochi, Drop, Squircle, Cat, Bean, Count };

int bodyShapeCount();
const char* bodyShapeName(BodyShape s);
int findBodyShape(const char* name);  // -1 if unknown

// Per-node rest offsets (px) from the base radius, for n nodes evenly spaced
// from angle 0 (y down). aspect = width / height. Every shape is scaled to the
// area of a circle of radius R, so the size setting means the same for all.
void bodyRestOffsets(BodyShape s, float aspect, float radius, float* out, int n);

}  // namespace ui::blob
