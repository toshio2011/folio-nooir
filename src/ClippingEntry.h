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

  // Word range on the paginated EPUB page.  Older clipping files do not have
  // these fields; ClipFile marks those entries as range-less and the reader
  // falls back to matching their saved text.  Keeping the range makes a new
  // highlight stable after CSS/font reflow as long as the passage remains on
  // the same page.
  uint16_t firstWord = 0;
  uint16_t lastWord = 0;
  bool hasWordRange = false;
};
