#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Fixed-size bookkeeping for immutable SD-font interval tables. It owns no
// storage; the font loader remains responsible for the arrays themselves.
template <size_t Capacity>
class SharedIntervalTableRegistry {
 public:
  static_assert(Capacity > 0 && Capacity <= 32, "registry mask supports 1..32 entries");

  struct Entry {
    const void* table = nullptr;
    uint32_t count = 0;
    bool bmp16 = false;
    uint16_t references = 0;
  };

  uint32_t candidates(uint32_t count) const {
    uint32_t mask = 0;
    for (size_t i = 0; i < Capacity; ++i) {
      if (entries_[i].table && entries_[i].count == count && entries_[i].references < UINT16_MAX) {
        mask |= uint32_t{1} << i;
      }
    }
    return mask;
  }

  const Entry& at(size_t index) const { return entries_[index]; }

  template <typename MatchRecord>
  void filterCandidatesAt(uint32_t& candidates, size_t recordIndex, MatchRecord matches) const {
    for (size_t i = 0; i < Capacity && candidates != 0; ++i) {
      if ((candidates & (uint32_t{1} << i)) != 0 && !matches(entries_[i], recordIndex)) {
        candidates &= ~(uint32_t{1} << i);
      }
    }
  }

  bool retain(size_t index) {
    Entry& entry = entries_[index];
    if (!entry.table || entry.references == 0 || entry.references == UINT16_MAX) return false;
    ++entry.references;
    return true;
  }

  // Returns false when all slots are occupied. The caller can keep the table
  // private in that case; sharing is an optimization, never a load requirement.
  bool add(const void* table, uint32_t count, bool bmp16) {
    if (!table) return false;
    for (Entry& entry : entries_) {
      if (entry.table) continue;
      entry = Entry{table, count, bmp16, 1};
      return true;
    }
    return false;
  }

  // Returns true if the caller should free the table. Unregistered tables are
  // private allocations and are also freed by their caller.
  bool release(const void* table) {
    if (!table) return true;
    for (Entry& entry : entries_) {
      if (entry.table != table) continue;
      if (entry.references > 1) {
        --entry.references;
        return false;
      }
      entry = Entry{};
      return true;
    }
    return true;
  }

  uint16_t references(const void* table) const {
    if (!table) return 0;
    for (const Entry& entry : entries_) {
      if (entry.table == table) return entry.references;
    }
    return 0;
  }

 private:
  std::array<Entry, Capacity> entries_{};
};
