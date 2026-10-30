#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace core {

// Charge of a single LiPo cell in percent from its voltage in millivolts: a table of general LiPo
// values with its top moved to what the JC3248W535 measured as full (4.07 V at rest after charging,
// docs/HANDOFF). Not measured on the cell, an estimate; a loaded cell reads lower, a charging one
// higher, so the number is only as good as the moment it is read at.
struct BatteryPoint {
  uint16_t mv;
  uint8_t percent;
};
constexpr BatteryPoint kBatteryCurve[] = {{3300, 0},  {3600, 5},  {3700, 15},  {3750, 25}, {3800, 40},
                                          {3850, 55}, {3950, 70}, {4050, 92}, {4070, 100}};
constexpr size_t kBatteryCurvePoints = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);

// Linear between the table's points, 0 below and 100 above it
constexpr int batteryPercent(int mv) {
  if (mv <= kBatteryCurve[0].mv) return 0;
  for (size_t i = 1; i < kBatteryCurvePoints; i++) {
    if (mv <= kBatteryCurve[i].mv) {
      const BatteryPoint lo = kBatteryCurve[i - 1], hi = kBatteryCurve[i];
      const int span = hi.mv - lo.mv;
      return lo.percent + ((hi.percent - lo.percent) * (mv - lo.mv) + span / 2) / span;  // rounded
    }
  }
  return 100;
}

// Median of the last N values; with fewer than N so far, of those
template <size_t N>
class MedianOfLast {
  static_assert(N >= 1 && N % 2 == 1, "N must be odd");

 public:
  void add(int v) {
    buf_[next_] = v;
    next_ = (next_ + 1) % N;
    if (count_ < N) count_++;
  }

  size_t count() const { return count_; }

  // The middle value, or the upper middle one for an even count; 0 before the first value
  int median() const {
    if (!count_) return 0;
    int sorted[N];
    std::copy(buf_, buf_ + count_, sorted);
    std::sort(sorted, sorted + count_);
    return sorted[count_ / 2];
  }

 private:
  int buf_[N] = {};
  size_t next_ = 0;
  size_t count_ = 0;
};

}  // namespace core
