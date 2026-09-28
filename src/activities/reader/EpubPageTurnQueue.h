#pragma once

#include <atomic>
#include <cstdint>

// A deliberately tiny reader-input queue. The signed value is the number of
// unexecuted page turns: opposite input cancels a turn that is still pending,
// which makes rapid reversals deterministic without storing page objects or
// allocating from the reader loop. The render task only observes the value;
// page-state mutation remains on the activity task when RenderLock is free.
class EpubPageTurnQueue final {
 public:
  static constexpr int8_t MAX_PENDING_TURNS = 5;

  bool enqueue(const bool forward) {
    int8_t current = pending_.load(std::memory_order_relaxed);
    for (;;) {
      const int delta = forward ? 1 : -1;
      const int next = static_cast<int>(current) + delta;
      if (next > MAX_PENDING_TURNS || next < -MAX_PENDING_TURNS) return false;
      const int8_t nextValue = static_cast<int8_t>(next);
      if (pending_.compare_exchange_weak(current, nextValue, std::memory_order_release,
                                          std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  bool take(bool& forward) {
    int8_t current = pending_.load(std::memory_order_acquire);
    while (current != 0) {
      const int step = current > 0 ? 1 : -1;
      const int8_t next = static_cast<int8_t>(static_cast<int>(current) - step);
      if (pending_.compare_exchange_weak(current, next, std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        forward = step > 0;
        return true;
      }
    }
    return false;
  }

  bool hasPending() const { return pending_.load(std::memory_order_acquire) != 0; }

  void clear() { pending_.store(0, std::memory_order_release); }

 private:
  std::atomic<int8_t> pending_{0};
};
