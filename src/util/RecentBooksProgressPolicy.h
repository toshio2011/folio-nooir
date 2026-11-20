#pragma once

#include <cstdint>

namespace RecentBooksProgressPolicy {

struct Value {
  uint8_t percent = 0;
  bool finished = false;
};

// BookState is the current authority when present. This deliberately lets an
// explicit reset or Mark Unfinished override an older RecentBooks value.
constexpr Value resolve(const uint8_t recentPercent, const bool hasBookState,
                        const uint8_t statePercent, const bool stateFinished) {
  if (!hasBookState) {
    const uint8_t percent = recentPercent > 100 ? 100 : recentPercent;
    return {percent, percent >= 100};
  }
  if (stateFinished) return {100, true};

  // A non-finished BookState must never render a complete progress badge,
  // even if a stale or older persisted row contains 100 percent.
  const uint8_t percent = statePercent > 99 ? 99 : statePercent;
  return {percent, false};
}

}  // namespace RecentBooksProgressPolicy
