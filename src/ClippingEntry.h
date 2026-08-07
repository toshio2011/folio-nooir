#pragma once

#include <cstdint>
#include <string>

// A bounded EPUB text clipping.  Text is capped by ClipFile before it is
// persisted so a large selection cannot consume the reader's limited heap.
struct ClippingEntry {
  std::string text;
  float percentage = 0.0f;
  uint16_t spineIndex = 0;
  uint16_t page = 0;
  uint32_t dateKey = 0;
};
