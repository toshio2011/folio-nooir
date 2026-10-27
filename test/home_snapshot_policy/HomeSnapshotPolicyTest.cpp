#include <gtest/gtest.h>

#include <array>

#include "src/util/HomeSnapshotPolicy.h"

TEST(HomeSnapshotPolicy, SupportsBothPersistentGridSizes) {
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(3));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(8));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(1));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageSize(4));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageCapacity(0));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_ONE_ROW_MAX_PAGE_ITEMS));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_TWO_ROW_MAX_PAGE_ITEMS));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS + 1));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSizeMatches(3, 3));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSizeMatches(8, 8));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSizeMatches(3, 8));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSizeMatches(8, 3));
}

TEST(HomeSnapshotPolicy, PagesSelectionByTheActiveGridSize) {
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(0, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(17, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS), 17u);
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(17, 17, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS, 18));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(17, 16, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS, 18));
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(0, 3), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(2, 3), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(3, 3), 3u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(7, 8), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(8, 8), 8u);
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(3, 0, 3, 6));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(5, 3, 3, 6));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(7, 0, 8, 9));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(9, 8, 8, 9));
  EXPECT_TRUE(HomeSnapshotPolicy::pageRangeMatches(8, 5, 10, 20));
  EXPECT_TRUE(HomeSnapshotPolicy::pageRangeMatches(27, 18, 10, 40));
  EXPECT_FALSE(HomeSnapshotPolicy::pageRangeMatches(28, 18, 10, 40));
  EXPECT_TRUE(HomeSnapshotPolicy::pageRangeMatches(41, 0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS, 42));
  EXPECT_FALSE(HomeSnapshotPolicy::pageRangeMatches(42, 0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS, 43));
  EXPECT_FALSE(HomeSnapshotPolicy::pageRangeMatches(42, 0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS + 1, 43));
}

TEST(HomeSnapshotPolicy, RequiresExactThemeAndLayoutIdentity) {
  using HomeSnapshotPolicy::SnapshotIdentity;
  EXPECT_TRUE(HomeSnapshotPolicy::identityMatches(SnapshotIdentity{4, 0}, SnapshotIdentity{4, 0}));
  EXPECT_FALSE(HomeSnapshotPolicy::identityMatches(SnapshotIdentity{4, 0}, SnapshotIdentity{5, 0}));
  EXPECT_FALSE(HomeSnapshotPolicy::identityMatches(SnapshotIdentity{4, 0}, SnapshotIdentity{4, 1}));
}

TEST(HomeSnapshotPolicy, SeparatesStructuralRebuildFromDynamicPatch) {
  using HomeSnapshotPolicy::RestoreAction;
  constexpr std::array<size_t, 2> pageSizes{HomeSnapshotPolicy::THREE_COVER_PAGE_ITEMS,
                                            HomeSnapshotPolicy::FOUR_BY_TWO_PAGE_ITEMS};
  for (const size_t pageItems : pageSizes) {
    ASSERT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(pageItems));
    EXPECT_EQ(HomeSnapshotPolicy::classifyRestore(10, 11, 20, 20), RestoreAction::Rebuild);
    EXPECT_EQ(HomeSnapshotPolicy::classifyRestore(10, 10, 20, 21), RestoreAction::PatchDynamic);
    EXPECT_EQ(HomeSnapshotPolicy::classifyRestore(10, 10, 20, 20), RestoreAction::Restore);
  }
}
