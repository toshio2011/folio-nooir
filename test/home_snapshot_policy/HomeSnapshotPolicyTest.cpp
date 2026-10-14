#include <gtest/gtest.h>

#include <array>

#include "src/util/HomeSnapshotPolicy.h"

TEST(HomeSnapshotPolicy, SupportsBothPersistentGridSizes) {
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(3));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(8));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageSize(1));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageSize(4));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSizeMatches(3, 3));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSizeMatches(8, 8));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSizeMatches(3, 8));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSizeMatches(8, 3));
}

TEST(HomeSnapshotPolicy, PagesSelectionByTheActiveGridSize) {
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(0, 3), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(2, 3), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(3, 3), 3u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(7, 8), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(8, 8), 8u);
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(3, 0, 3, 6));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(5, 3, 3, 6));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(7, 0, 8, 9));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(9, 8, 8, 9));
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
