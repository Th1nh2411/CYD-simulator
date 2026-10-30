#pragma once

#include <cstdint>

#include "app/engine/events.h"

namespace platform {

// Test cards for a board's BOOT button and for a shake (and the simulator), in
// turn: English notification, English prompt, Korean notification, Korean
// prompt. n counts the cards shown so far; a prompt's ref is n + 1.
engine::Event sampleCard(uint32_t n);

}  // namespace platform
