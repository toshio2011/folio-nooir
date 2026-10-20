#pragma once

#include <cstdint>

namespace FolioLiteLayoutPolicy {

constexpr uint8_t FOLIO_LAYOUT_GRID = 0;
constexpr uint8_t FOLIO_LAYOUT_THREE_COVERS = 1;
constexpr uint8_t FOLIO_LAYOUT_THREE_COVER_CAROUSEL = 2;
constexpr uint8_t FOLIO_LAYOUT_FIVE_COVER_CAROUSEL = 3;
constexpr uint8_t FOLIO_LAYOUT_SPINE = 4;
constexpr uint8_t FOLIO_LAYOUT_TWO_ROW_SPINE = 5;

constexpr uint8_t LITE_LAYOUT_THREE_COVER_CAROUSEL = 0;
constexpr uint8_t LITE_LAYOUT_FIVE_COVER_CAROUSEL = 1;
constexpr uint8_t LITE_LAYOUT_SPINE = 2;
// Value 3 used to be the Lite-only Grid option. Reuse it for the requested
// two-row shelf so old persisted keys remain valid without keeping Grid alive.
constexpr uint8_t LITE_LAYOUT_TWO_ROW_SPINE = 3;
constexpr uint8_t LITE_LAYOUT_THREE_ROW_SPINE = 4;
constexpr uint8_t FOLIO_LAYOUT_THREE_ROW_SPINE = 6;  // Lite-only renderer sentinel.

inline uint8_t fromFolioLayout(const uint8_t layout) {
  switch (layout) {
    case FOLIO_LAYOUT_THREE_COVER_CAROUSEL:
      return LITE_LAYOUT_THREE_COVER_CAROUSEL;
    case FOLIO_LAYOUT_FIVE_COVER_CAROUSEL:
      return LITE_LAYOUT_FIVE_COVER_CAROUSEL;
    case FOLIO_LAYOUT_SPINE:
      return LITE_LAYOUT_SPINE;
    case FOLIO_LAYOUT_TWO_ROW_SPINE:
      return LITE_LAYOUT_TWO_ROW_SPINE;
    case FOLIO_LAYOUT_GRID:
      // Older Lite settings projected the main Grid choice to the removed Lite
      // Grid option. Keep that migration on the new two-row shelf.
      return LITE_LAYOUT_TWO_ROW_SPINE;
    case FOLIO_LAYOUT_THREE_COVERS:
      return LITE_LAYOUT_THREE_COVER_CAROUSEL;
    default:
      return LITE_LAYOUT_FIVE_COVER_CAROUSEL;
  }
}

inline uint8_t toFolioLayout(const uint8_t liteLayout) {
  switch (liteLayout) {
    case LITE_LAYOUT_THREE_COVER_CAROUSEL:
      return FOLIO_LAYOUT_THREE_COVER_CAROUSEL;
    case LITE_LAYOUT_FIVE_COVER_CAROUSEL:
      return FOLIO_LAYOUT_FIVE_COVER_CAROUSEL;
    case LITE_LAYOUT_SPINE:
      return FOLIO_LAYOUT_SPINE;
    case LITE_LAYOUT_TWO_ROW_SPINE:
      return FOLIO_LAYOUT_TWO_ROW_SPINE;
    case LITE_LAYOUT_THREE_ROW_SPINE:
      return FOLIO_LAYOUT_THREE_ROW_SPINE;
    default:
      return FOLIO_LAYOUT_FIVE_COVER_CAROUSEL;
  }
}

}  // namespace FolioLiteLayoutPolicy
