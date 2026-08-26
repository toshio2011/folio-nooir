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

  // Word range on the paginated EPUB page. Older clipping files do not have
  // these fields; ClipFile marks those entries as range-less and the reader
  // falls back to matching their saved text. The range is the fast path; after
  // CSS/font reflow the reader can relocate the passage using its saved text
  // and percentage hint.
  uint16_t firstWord = 0;
  uint16_t lastWord = 0;
  bool hasWordRange = false;

  // Aggregate style information for diagnostics and compatibility. The
  // renderer still resolves each word's exact style from the current EPUB
  // page (including split-bold/focus runs), so mixed-style clippings remain
  // correct after reflow and older files remain fully supported.
  uint8_t styleMask = 0;
  // Explicit bold metadata is kept separately from the highlight state.  The
  // field is intentionally optional in JSON so older clipping files continue
  // to load; ClipFile derives it from styleMask when the key is absent.
  bool bold = false;
};
