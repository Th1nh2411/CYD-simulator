#pragma once

#include <cstdint>

#include "app/gfx/color.h"

// CPU cost of the drawing primitives, rendered into a scratch buffer with no
// display transfer. Portable, so different SoCs can be compared on equal terms.
namespace gfx::bench {

struct Result {
  const char* name;
  uint32_t pixels;  // output pixels touched over all iterations
  uint32_t us;
};

using ClockUs = uint32_t (*)();

constexpr int kSide = 128;  // scratch buffer is kSide x kSide pixels

// Returns the number of results written
int run(Color* buf, ClockUs now, Result* out, int maxResults);

}  // namespace gfx::bench
