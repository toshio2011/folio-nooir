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
constexpr uint8_t LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES = 5;

// Lite's hero/synopsis/status block must end before the global statistics
// strip. Keep the shelf height independent of synopsis text and shrink only
// when the device viewport cannot fit the preferred height.
constexpr int LITE_CAROUSEL_TOP = 122;
constexpr int LITE_CAROUSEL_SYNOPSIS_GAP = 10;
constexpr int LITE_CAROUSEL_STATUS_TOP_INSET = 6;
constexpr int LITE_CAROUSEL_STATUS_PAGINATION_GAP = 6;
constexpr int LITE_CAROUSEL_PAGINATION_HEIGHT = 18;
constexpr int LITE_CAROUSEL_PAGINATION_STATS_GAP = 4;
constexpr int LITE_CAROUSEL_PREFERRED_HERO_HEIGHT = 424;
constexpr int LITE_CAROUSEL_MIN_HERO_HEIGHT = 320;
constexpr int LITE_CAROUSEL_MAX_HERO_HEIGHT = 466;

struct AuthorTextUpdate {
  bool clearPrevious = false;
  bool drawCurrent = false;
};

constexpr AuthorTextUpdate authorTextUpdate(const bool selectionChanged, const bool hasAuthor) {
  return AuthorTextUpdate{selectionChanged, hasAuthor};
}

constexpr int paginationTop(const int statsTop) {
  return statsTop - LITE_CAROUSEL_PAGINATION_HEIGHT - LITE_CAROUSEL_PAGINATION_STATS_GAP;
}

constexpr int carouselHeroHeight(const int lowerContentBottom, const int synopsisLineHeight) {
  const int lineHeight = synopsisLineHeight > 0 ? synopsisLineHeight : 1;
  const int synopsisHeight = LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES * lineHeight;
  // Reserve distinct synopsis, status, pagination, and global-stat regions.
  const int statusHeight = lineHeight + LITE_CAROUSEL_STATUS_TOP_INSET;
  const int available = lowerContentBottom - LITE_CAROUSEL_TOP - LITE_CAROUSEL_SYNOPSIS_GAP -
                        synopsisHeight - statusHeight - LITE_CAROUSEL_STATUS_PAGINATION_GAP -
                        LITE_CAROUSEL_PAGINATION_HEIGHT - LITE_CAROUSEL_PAGINATION_STATS_GAP;
  if (available <= 0) return 1;
  const int boundedPreferred = LITE_CAROUSEL_PREFERRED_HERO_HEIGHT < LITE_CAROUSEL_MAX_HERO_HEIGHT
                                   ? LITE_CAROUSEL_PREFERRED_HERO_HEIGHT
                                   : LITE_CAROUSEL_MAX_HERO_HEIGHT;
  const int height = available < boundedPreferred ? available : boundedPreferred;
  return available < LITE_CAROUSEL_MIN_HERO_HEIGHT ? height :
         (height < LITE_CAROUSEL_MIN_HERO_HEIGHT ? LITE_CAROUSEL_MIN_HERO_HEIGHT : height);
}

constexpr bool canPatchSpineSelection(const bool previousFrameMatches, const bool selectionChanged,
                                      const bool sameTab, const bool noOverlay, const bool noBackgroundWork) {
  return previousFrameMatches && selectionChanged && sameTab && noOverlay && noBackgroundWork;
}

constexpr bool canDrawPopupOverRetainedShelf(const bool popupActive, const bool sameShelfFrame,
                                             const bool noBackgroundWork, const bool retainedFrameIsShelf) {
  return popupActive && sameShelfFrame && noBackgroundWork && retainedFrameIsShelf;
}

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
