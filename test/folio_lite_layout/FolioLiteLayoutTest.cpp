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
