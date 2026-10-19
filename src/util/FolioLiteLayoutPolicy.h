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
constexpr uint8_t LITE_LAYOUT_GRID = 3;

inline uint8_t fromFolioLayout(const uint8_t layout) {
  switch (layout) {
    case FOLIO_LAYOUT_THREE_COVER_CAROUSEL:
      return LITE_LAYOUT_THREE_COVER_CAROUSEL;
    case FOLIO_LAYOUT_FIVE_COVER_CAROUSEL:
      return LITE_LAYOUT_FIVE_COVER_CAROUSEL;
    case FOLIO_LAYOUT_SPINE:
    case FOLIO_LAYOUT_TWO_ROW_SPINE:
      return LITE_LAYOUT_SPINE;
    case FOLIO_LAYOUT_GRID:
    case FOLIO_LAYOUT_THREE_COVERS:
    default:
      return LITE_LAYOUT_GRID;
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
    case LITE_LAYOUT_GRID:
    default:
      return FOLIO_LAYOUT_GRID;
  }
}

}  // namespace FolioLiteLayoutPolicy
