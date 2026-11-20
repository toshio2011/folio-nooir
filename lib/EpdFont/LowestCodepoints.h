#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace SdFontAdvancePolicy {

// Keep a sorted, unique set of the lowest `capacity` codepoints in a caller-
// owned array. The array never grows, which lets the SD-font advance scan keep
// its temporary allocation bounded independently of paragraph length.
inline bool retainLowestUnique(uint32_t* values, size_t& count, const size_t capacity, const uint32_t value) {
  if (!values || capacity == 0) return false;

  const uint32_t* const begin = values;
  const uint32_t* const end = values + count;
  const uint32_t* const position = std::lower_bound(begin, end, value);
  const size_t index = static_cast<size_t>(position - begin);
  if (position != end && *position == value) return false;
  if (count == capacity && index == count) return false;

  if (count < capacity) {
    for (size_t cursor = count; cursor > index; --cursor) values[cursor] = values[cursor - 1];
    values[index] = value;
    ++count;
    return true;
  }

  for (size_t cursor = count - 1; cursor > index; --cursor) values[cursor] = values[cursor - 1];
  values[index] = value;
  return true;
}

}  // namespace SdFontAdvancePolicy
