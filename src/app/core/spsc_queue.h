#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace core {

// Lock-free bounded queue for exactly one producer thread and one consumer
// thread. N must be a power of two; holds up to N - 1 items.
template <typename T, size_t N>
class SpscQueue {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");

 public:
  bool push(const T& v) {
    uint32_t h = head_.load(std::memory_order_relaxed);
    uint32_t next = (h + 1) & (N - 1);
    if (next == tail_.load(std::memory_order_acquire)) return false;  // full
    buf_[h] = v;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;  // empty
    out = buf_[t];
    tail_.store((t + 1) & (N - 1), std::memory_order_release);
    return true;
  }

 private:
  T buf_[N]{};
  std::atomic<uint32_t> head_{0}, tail_{0};
};

}  // namespace core
