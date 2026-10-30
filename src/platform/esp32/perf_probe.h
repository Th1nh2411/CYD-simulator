#pragma once

#include <cstdint>

// Per-core CPU load from idle-hook counts, calibrated against an idle system.
// Keeps the idle task spinning instead of sleeping, so it is a development
// tool: enable with -DPERF_PROBE=1.
namespace perf {

// Call before the app tasks start; blocks for calibrateMs
void begin(uint32_t calibrateMs);
// Load per core in percent since the previous call
void sample(float load[2]);

}  // namespace perf
