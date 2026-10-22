#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
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

// Tiny path-keyed negative cache for prepared cover thumbnails. Unlike the
// activity-indexed memo above, it survives RecentBooksActivity recreation so a
// known-missing SD path is not probed again on every return to Home.
class ThumbnailPathMissMemo {
 public:
  static constexpr size_t CAPACITY = 32;

  bool contains(const char* path) const {
    const uint64_t identity = hashPath(path);
    if (identity == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const uint64_t cached : identities_) {
      if (cached == identity) return true;
    }
    return false;
  }

  void markMissing(const char* path) {
    const uint64_t identity = hashPath(path);
    if (identity == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const uint64_t cached : identities_) {
      if (cached == identity) return;
    }
    identities_[next_] = identity;
    next_ = (next_ + 1) % identities_.size();
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    identities_.fill(0);
    next_ = 0;
  }

 private:
  static uint64_t hashPath(const char* path) {
    if (path == nullptr || *path == '\0') return 0;
    uint64_t hash = 1469598103934665603ULL;
    for (const auto* cursor = reinterpret_cast<const uint8_t*>(path); *cursor != 0; ++cursor) {
      hash ^= *cursor;
      hash *= 1099511628211ULL;
    }
    return hash == 0 ? 1 : hash;
  }

  std::array<uint64_t, CAPACITY> identities_{};
  size_t next_ = 0;
  mutable std::mutex mutex_;
};
