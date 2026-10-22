#pragma once

#include <cstddef>
#include <cstdint>

namespace RecentBooksViewPolicy {

template <typename Range, typename Predicate>
size_t countVisible(const Range& books, Predicate isVisible) {
  size_t count = 0;
  for (const auto& book : books) {
    if (isVisible(book)) ++count;
  }
  return count;
}

template <typename Range, typename Predicate>
size_t rawIndexForVisible(const Range& books, const size_t visibleIndex, Predicate isVisible) {
  size_t visible = 0;
  size_t raw = 0;
  for (const auto& book : books) {
    if (isVisible(book)) {
      if (visible == visibleIndex) return raw;
      ++visible;
    }
    ++raw;
  }
  return static_cast<size_t>(-1);
}

inline bool tabIncludes(const uint8_t tab, const bool finished) {
  return tab == 0 || (tab == 1 && !finished) || (tab == 2 && finished);
}

}  // namespace RecentBooksViewPolicy
