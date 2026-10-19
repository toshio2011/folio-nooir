#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Compact, activity-scoped negative cache indexed by the source book list.
// A bit per entry avoids retrying a missing cached thumbnail after the
// carousel probe's small working set has moved on to another page.
class ThumbnailMissMemo {
 public:
  void reset(const size_t entryCount) {
    entryCount_ = entryCount;
    words_.assign(entryCount / 64 + (entryCount % 64 != 0), 0);
  }

  bool contains(const size_t index) const {
    return index < entryCount_ && (words_[index / 64] & (uint64_t{1} << (index % 64))) != 0;
  }

  void markMissing(const size_t index) {
    if (index >= entryCount_) return;
    words_[index / 64] |= uint64_t{1} << (index % 64);
  }

 private:
  size_t entryCount_ = 0;
  std::vector<uint64_t> words_;
};
