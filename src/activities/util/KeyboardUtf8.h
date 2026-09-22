#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace keyboard_utf8 {

inline bool isContinuationByte(const char value) {
  return (static_cast<uint8_t>(value) & 0xC0u) == 0x80u;
}

// Return the greatest valid UTF-8 end offset at or before pos. The scan is
// only over the current codepoint's continuation bytes, never the full text.
inline std::size_t endAtOrBefore(const std::string& text, std::size_t pos) {
  if (pos >= text.length()) return text.length();
  while (pos > 0 && isContinuationByte(text[pos])) --pos;
  return pos;
}

inline std::size_t nextBoundary(const std::string& text, std::size_t pos) {
  if (pos >= text.length()) return text.length();
  ++pos;
  while (pos < text.length() && isContinuationByte(text[pos])) ++pos;
  return pos;
}

}  // namespace keyboard_utf8
