#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>

#include "src/util/CoverStackGeometry.h"

namespace {

template <size_t N>
void expectUniqueVisibleBooks(const std::array<CoverStackSlot, N>& slots, const size_t itemCount,
                              const size_t expectedCount) {
  std::set<size_t> indexes;
  for (const auto& slot : slots) {
    if (!slot.valid) continue;
    ASSERT_LT(slot.itemIndex, itemCount);
    indexes.insert(slot.itemIndex);
  }
  EXPECT_EQ(indexes.size(), expectedCount);
}

template <typename Layout>
void checkCounts(Layout layout, const size_t maxVisible) {
  for (size_t itemCount : {size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{5}, size_t{6}, size_t{8}}) {
    const size_t expectedCount = std::min(itemCount, maxVisible);
    const size_t selectedCount = std::max<size_t>(1, itemCount);
    for (size_t selected = 0; selected < selectedCount; ++selected) {
      const auto slots = layout(itemCount, selected);
      expectUniqueVisibleBooks(slots, itemCount, expectedCount);
      if (itemCount == 0) {
        for (const auto& slot : slots) EXPECT_FALSE(slot.valid);
      }
    }
  }
}

}  // namespace

TEST(CarouselGeometry, FiveCardLayoutUsesOnlyDistinctBooksAtEveryCount) {
  checkCounts(
      [](const size_t itemCount, const size_t selected) {
        return CoverStackGeometry::layout(480, itemCount, selected, 400);
      },
      5);
}

TEST(CarouselGeometry, ThreeCardLayoutUsesOnlyDistinctBooksAtEveryCount) {
  checkCounts(
      [](const size_t itemCount, const size_t selected) {
        return CoverStackGeometry::layoutThree(480, itemCount, selected, 400);
      },
      3);
}

TEST(CarouselGeometry, FolioFiveCardLayoutUsesOnlyDistinctBooksAtEveryCount) {
  checkCounts(
      [](const size_t itemCount, const size_t selected) {
        return CoverStackGeometry::layoutFolioShelf(480, 200, 400, itemCount, selected, false);
      },
      5);
}

TEST(CarouselGeometry, FolioThreeCardLayoutUsesOnlyDistinctBooksAtEveryCount) {
  checkCounts(
      [](const size_t itemCount, const size_t selected) {
        return CoverStackGeometry::layoutFolioShelf(480, 200, 400, itemCount, selected, true);
      },
      3);
}

TEST(CarouselGeometry, OneBookIsCenteredWithoutPerspectiveNeighbors) {
  const auto five = CoverStackGeometry::layout(480, 1, 0, 400);
  const auto three = CoverStackGeometry::layoutThree(480, 1, 0, 400);
  for (const auto& slots : {five, three}) {
    ASSERT_TRUE(slots[4].valid);
    EXPECT_EQ(slots[4].itemIndex, 0u);
    for (size_t i = 0; i < 4; ++i) EXPECT_FALSE(slots[i].valid);
  }
}

TEST(CarouselGeometry, NavigationWrapsOnlyWhenTheCollectionHasEnoughBooks) {
  for (size_t itemCount : {size_t{3}, size_t{5}, size_t{6}, size_t{8}}) {
    const auto five = CoverStackGeometry::layout(480, itemCount, itemCount - 1, 400);
    const auto three = CoverStackGeometry::layoutThree(480, itemCount, itemCount - 1, 400);
    expectUniqueVisibleBooks(five, itemCount, std::min<size_t>(5, itemCount));
    expectUniqueVisibleBooks(three, itemCount, std::min<size_t>(3, itemCount));
  }
}
