#pragma once

#include <cstddef>
#include <cstdint>

namespace page_cache_validation {

// Every serialized page element has a one-byte tag and at least the fixed
// horizontal-rule payload. The page always ends with a two-byte footnote count.
constexpr size_t MIN_SERIALIZED_ELEMENT_BYTES =
    sizeof(uint8_t) + sizeof(int16_t) * 2 + sizeof(uint16_t) + sizeof(uint8_t);

inline bool elementCountFits(const uint16_t count, const size_t remainingBytes) {
  if (remainingBytes < sizeof(uint16_t)) return false;
  return count <= (remainingBytes - sizeof(uint16_t)) / MIN_SERIALIZED_ELEMENT_BYTES;
}

// For a length-prefixed string, remainingBytes starts immediately after the
// length field. trailingBytes are fields that must remain after the string.
inline bool stringLengthFits(const uint32_t length, const size_t remainingBytes, const size_t trailingBytes) {
  return remainingBytes >= trailingBytes && static_cast<size_t>(length) <= remainingBytes - trailingBytes;
}

}  // namespace page_cache_validation
