#pragma once

#include <atomic>
#include <cstdint>

namespace core {

// Lock-free "latest value" exchange between exactly one writer thread and one
// reader thread. The writer never blocks; the reader always sees the most
// recently published value and never a half-written one.
template <typename T>
class TripleBuffer {
 public:
  // Writer: fill back(), then publish().
  T& back() { return buf_[back_]; }
  void publish() {
    uint32_t prev = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
    back_ = prev & kIndexMask;
  }

  // Reader: update() swaps in the newest value if there is one; front() reads it.
  bool update() {
    if (!(middle_.load(std::memory_order_acquire) & kFresh)) return false;
    uint32_t prev = middle_.exchange(front_, std::memory_order_acq_rel);
    front_ = prev & kIndexMask;
    return true;
  }
  const T& front() const { return buf_[front_]; }

 private:
  static constexpr uint32_t kFresh = 0x80, kIndexMask = 0x03;
  T buf_[3]{};
  uint32_t back_ = 0, front_ = 1;
  std::atomic<uint32_t> middle_{2};
};

}  // namespace core
