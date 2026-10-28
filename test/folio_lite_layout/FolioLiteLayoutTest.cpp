#include <gtest/gtest.h>

#include "src/util/FolioLiteLayoutPolicy.h"

TEST(FolioLiteLayout, MapsCarouselAndSpineChoicesIntoLiteLayouts) {
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(FolioLiteLayoutPolicy::FOLIO_LAYOUT_THREE_COVER_CAROUSEL),
            FolioLiteLayoutPolicy::LITE_LAYOUT_THREE_COVER_CAROUSEL);
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(FolioLiteLayoutPolicy::FOLIO_LAYOUT_FIVE_COVER_CAROUSEL),
            FolioLiteLayoutPolicy::LITE_LAYOUT_FIVE_COVER_CAROUSEL);
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(FolioLiteLayoutPolicy::FOLIO_LAYOUT_SPINE),
            FolioLiteLayoutPolicy::LITE_LAYOUT_SPINE);
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(FolioLiteLayoutPolicy::FOLIO_LAYOUT_TWO_ROW_SPINE),
            FolioLiteLayoutPolicy::LITE_LAYOUT_TWO_ROW_SPINE);
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(FolioLiteLayoutPolicy::FOLIO_LAYOUT_GRID),
            FolioLiteLayoutPolicy::LITE_LAYOUT_TWO_ROW_SPINE);
}
TEST(FolioLiteLayout, LegacyAndInvalidLayoutsMapToAnAvailableLiteChoice) {
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(FolioLiteLayoutPolicy::FOLIO_LAYOUT_THREE_COVERS),
            FolioLiteLayoutPolicy::LITE_LAYOUT_THREE_COVER_CAROUSEL);
  EXPECT_EQ(FolioLiteLayoutPolicy::fromFolioLayout(255), FolioLiteLayoutPolicy::LITE_LAYOUT_FIVE_COVER_CAROUSEL);
}

TEST(FolioLiteLayout, EveryLiteChoiceReturnsTheIntendedFolioRendererMode) {
  EXPECT_EQ(FolioLiteLayoutPolicy::toFolioLayout(FolioLiteLayoutPolicy::LITE_LAYOUT_THREE_COVER_CAROUSEL),
            FolioLiteLayoutPolicy::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
  EXPECT_EQ(FolioLiteLayoutPolicy::toFolioLayout(FolioLiteLayoutPolicy::LITE_LAYOUT_FIVE_COVER_CAROUSEL),
            FolioLiteLayoutPolicy::FOLIO_LAYOUT_FIVE_COVER_CAROUSEL);
  EXPECT_EQ(FolioLiteLayoutPolicy::toFolioLayout(FolioLiteLayoutPolicy::LITE_LAYOUT_SPINE),
            FolioLiteLayoutPolicy::FOLIO_LAYOUT_SPINE);
  EXPECT_EQ(FolioLiteLayoutPolicy::toFolioLayout(FolioLiteLayoutPolicy::LITE_LAYOUT_TWO_ROW_SPINE),
            FolioLiteLayoutPolicy::FOLIO_LAYOUT_TWO_ROW_SPINE);
  EXPECT_EQ(FolioLiteLayoutPolicy::toFolioLayout(FolioLiteLayoutPolicy::LITE_LAYOUT_THREE_ROW_SPINE),
            FolioLiteLayoutPolicy::FOLIO_LAYOUT_THREE_ROW_SPINE);
  EXPECT_EQ(FolioLiteLayoutPolicy::toFolioLayout(255),
            FolioLiteLayoutPolicy::FOLIO_LAYOUT_FIVE_COVER_CAROUSEL);
}

TEST(FolioLiteLayout, SynopsisSpaceIsReservedAtTheSameHeightForEveryBook) {
  EXPECT_EQ(FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES, 5);
}

TEST(FolioLiteLayout, LiteTitleAuthorHeaderAddsThreePixelsAndClearsTheShelfBoundary) {
  EXPECT_EQ(FolioLiteLayoutPolicy::liteBookAuthorTop(),
            FolioLiteLayoutPolicy::LITE_BOOK_TITLE_TOP + FolioLiteLayoutPolicy::LITE_BOOK_TITLE_HEIGHT + 3);
  EXPECT_EQ(FolioLiteLayoutPolicy::liteBookHeaderBottom(), 117);
  EXPECT_EQ(FolioLiteLayoutPolicy::liteBookHeaderToShelfGap(), 5);
  EXPECT_LE(FolioLiteLayoutPolicy::liteBookHeaderBottom(), FolioLiteLayoutPolicy::LITE_CAROUSEL_TOP);
  EXPECT_EQ(FolioLiteLayoutPolicy::LITE_BOOK_TITLE_HEIGHT, 24);
  EXPECT_EQ(FolioLiteLayoutPolicy::LITE_BOOK_AUTHOR_HEIGHT, 18);
}

TEST(FolioLiteLayout, HeroSizeIsFixedForSynopsisAvailabilityAndStatusStaysAboveSummary) {
  constexpr int statsTop = 700;
  constexpr int lineHeight = 18;
  const int withSynopsis = FolioLiteLayoutPolicy::carouselHeroHeight(statsTop, lineHeight);
  const int withoutSynopsis = FolioLiteLayoutPolicy::carouselHeroHeight(statsTop, lineHeight);
  EXPECT_EQ(withSynopsis, 424);
  EXPECT_EQ(withoutSynopsis, withSynopsis);

  const int reservedBottom = FolioLiteLayoutPolicy::LITE_CAROUSEL_TOP + withSynopsis +
                             FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_GAP +
                             FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES * lineHeight +
                             lineHeight + FolioLiteLayoutPolicy::LITE_CAROUSEL_STATUS_TOP_INSET;
  const int paginationTop = FolioLiteLayoutPolicy::paginationTop(statsTop);
  EXPECT_LE(reservedBottom + FolioLiteLayoutPolicy::LITE_CAROUSEL_STATUS_PAGINATION_GAP, paginationTop);
  EXPECT_EQ(paginationTop + FolioLiteLayoutPolicy::LITE_CAROUSEL_PAGINATION_HEIGHT +
                FolioLiteLayoutPolicy::LITE_CAROUSEL_PAGINATION_STATS_GAP,
            statsTop);
}

TEST(FolioLiteLayout, HeroShrinksToFitShortViewportsWithoutCrossingStatistics) {
  constexpr int lineHeight = 18;
  constexpr int statsTop = 660;
  const int heroHeight = FolioLiteLayoutPolicy::carouselHeroHeight(statsTop, lineHeight);
  EXPECT_EQ(heroHeight, 386);
  const int reservedBottom = FolioLiteLayoutPolicy::LITE_CAROUSEL_TOP + heroHeight +
                             FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_GAP +
                             FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES * lineHeight +
                             lineHeight + FolioLiteLayoutPolicy::LITE_CAROUSEL_STATUS_TOP_INSET;
  EXPECT_EQ(reservedBottom + FolioLiteLayoutPolicy::LITE_CAROUSEL_STATUS_PAGINATION_GAP +
                FolioLiteLayoutPolicy::LITE_CAROUSEL_PAGINATION_HEIGHT +
                FolioLiteLayoutPolicy::LITE_CAROUSEL_PAGINATION_STATS_GAP,
            statsTop);
  EXPECT_GT(FolioLiteLayoutPolicy::carouselHeroHeight(560, lineHeight), 0);
}

TEST(FolioLiteLayout, AuthorChangesAlwaysClearTheFixedRegionAndOnlyDrawAvailableText) {
  const auto firstAuthor = FolioLiteLayoutPolicy::authorTextUpdate(true, true);
  const auto missingAuthor = FolioLiteLayoutPolicy::authorTextUpdate(true, false);
  const auto differentLengthAuthor = FolioLiteLayoutPolicy::authorTextUpdate(true, true);
  EXPECT_TRUE(firstAuthor.clearPrevious);
  EXPECT_TRUE(firstAuthor.drawCurrent);
  EXPECT_TRUE(missingAuthor.clearPrevious);
  EXPECT_FALSE(missingAuthor.drawCurrent);
  EXPECT_TRUE(differentLengthAuthor.clearPrevious);
  EXPECT_TRUE(differentLengthAuthor.drawCurrent);
}

TEST(FolioLiteLayout, SpineArtworkCanBePatchedOnlyForAnUnchangedSelectionFrame) {
  EXPECT_TRUE(FolioLiteLayoutPolicy::canPatchSpineSelection(true, true, true, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canPatchSpineSelection(false, true, true, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canPatchSpineSelection(true, false, true, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canPatchSpineSelection(true, true, false, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canPatchSpineSelection(true, true, true, false, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canPatchSpineSelection(true, true, true, true, false));
}

TEST(FolioLiteLayout, PopupCanReuseOnlyAnUnchangedShelfWithoutBackgroundWork) {
  EXPECT_TRUE(FolioLiteLayoutPolicy::canDrawPopupOverRetainedShelf(true, true, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canDrawPopupOverRetainedShelf(false, true, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canDrawPopupOverRetainedShelf(true, false, true, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canDrawPopupOverRetainedShelf(true, true, false, true));
  EXPECT_FALSE(FolioLiteLayoutPolicy::canDrawPopupOverRetainedShelf(true, true, true, false));
}
