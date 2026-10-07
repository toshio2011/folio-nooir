#pragma once

#include <cstddef>
#include <cstdint>

namespace memorypressure {

// This is a deliberately small ordered policy, not a registry of arbitrary
// cache callbacks. Level 1 remains subsystem-owned cleanup; the shared helper
// currently exposes only the safe, reconstructible Level 2 candidate below.
enum class ReclaimLevel : uint8_t { None = 0, Reconstructible = 2 };

enum class ReclaimResult : uint8_t {
  AlreadyEnough,
  Reclaimed,
  NoCandidate,
  NoGain,
  StillInsufficient,
  UnsafeNow,
};

struct ReclaimReport {
  ReclaimResult result = ReclaimResult::NoCandidate;
  size_t freeBefore = 0;
  size_t largestBefore = 0;
  size_t freeAfter = 0;
  size_t largestAfter = 0;

  bool memoryImproved() const {
    return freeAfter > freeBefore || largestAfter > largestBefore;
  }
};

// Synchronously releases known disposable caches, at most once per candidate,
// and stops once both requested headroom measures are met. Call only after the
// owning subsystem has released its own transient resources.
ReclaimReport reclaimForHeadroom(size_t targetFree, size_t targetLargest, const char* reason,
                                 ReclaimLevel allowedLevel);

}  // namespace memorypressure
