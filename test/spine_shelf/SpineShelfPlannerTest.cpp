#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>

#include "src/util/SpineShelfPlanner.h"

namespace {

using SpineShelfPlanner::Book;
using SpineShelfPlanner::Page;
using SpineShelfPlanner::Rect;

constexpr Rect kWideShelf{6, 220, 788, 160};

std::array<Book, 10> books() {
  return {{{"book-0", "A short title", 0},
           {"book-1", "A much longer title that will be abbreviated", 23},
           {"book-2", "UTF-8 — café", 50},
           {"book-3", "Fourth", 100},
           {"book-4", "Fifth", 17},
           {"book-5", "Sixth", 42},
           {"book-6", "Seventh", 61},
           {"book-7", "Eighth", 77},
           {"book-8", "Ninth", 88},
           {"book-9", "Tenth", 99}}};
}

}  // namespace

TEST(SpineShelfPlanner, DimensionsAreDeterministicAndBottomAligned) {
  const auto input = books();
  const Page first = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, kWideShelf);
  const Page second = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, kWideShelf);
  ASSERT_EQ(first.itemCount, second.itemCount);
  ASSERT_GT(first.itemCount, 0);
  for (size_t i = 0; i < first.itemCount; ++i) {
    EXPECT_EQ(first.slots[i].itemIndex, second.slots[i].itemIndex);
    EXPECT_EQ(first.slots[i].rect.x, second.slots[i].rect.x);
    EXPECT_EQ(first.slots[i].rect.y, second.slots[i].rect.y);
    EXPECT_EQ(first.slots[i].rect.width, second.slots[i].rect.width);
    EXPECT_EQ(first.slots[i].rect.height, second.slots[i].rect.height);
    EXPECT_EQ(first.slots[i].rect.y + first.slots[i].rect.height,
              kWideShelf.y + kWideShelf.height - SpineShelfPlanner::SHELF_BOTTOM_GAP);
    EXPECT_GE(first.slots[i].rect.width, SpineShelfPlanner::MIN_BOOK_WIDTH);
    EXPECT_LE(first.slots[i].rect.width, SpineShelfPlanner::MAX_BOOK_WIDTH);
    EXPECT_GE(first.slots[i].rect.height, SpineShelfPlanner::MIN_BOOK_HEIGHT);
    EXPECT_LE(first.slots[i].rect.height, SpineShelfPlanner::MAX_BOOK_HEIGHT);
    EXPECT_GT(first.slots[i].rect.width, 0);
    EXPECT_GT(first.slots[i].rect.height, 0);
  }
}

TEST(SpineShelfPlanner, TallerAvailableRegionProducesBookLikeProportions) {
  const auto input = books();
  const Rect bounds{0, 0, 480, 280};
  const Page page = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(page.itemCount, 0);
  int tallest = 0;
  const int usableHeight = bounds.height - SpineShelfPlanner::SHELF_BOTTOM_GAP;
  for (size_t i = 0; i < page.itemCount; ++i) {
    tallest = std::max(tallest, page.slots[i].rect.height);
    EXPECT_GE(page.slots[i].rect.height, usableHeight * 74 / 100);
    EXPECT_LE(page.slots[i].rect.height, usableHeight * 90 / 100);
  }
  EXPECT_GE(tallest, usableHeight * 80 / 100);
}

TEST(SpineShelfPlanner, CentersPartialAndFullShelfCompositions) {
  const auto input = books();
  for (const Rect bounds : {Rect{0, 0, 300, 280}, Rect{0, 0, 620, 280}}) {
    const Page page = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, bounds);
    ASSERT_GT(page.itemCount, 0);
    int left = page.slots[0].rect.x;
    int right = page.slots[page.itemCount - 1].rect.x + page.slots[page.itemCount - 1].rect.width;
    if (page.plant.visible) {
      left = std::min(left, page.plant.rect.x);
      right = std::max(right, page.plant.rect.x + page.plant.rect.width);
    }
    EXPECT_LE(std::abs((left + right) - (bounds.x * 2 + bounds.width)), 2);
    EXPECT_GE(left, bounds.x);
    EXPECT_LE(right, bounds.x + bounds.width);
  }
}

TEST(SpineShelfPlanner, ShelfIsCenteredAndHasBoundedBookOverhang) {
  const auto input = books();
  const Rect bounds{6, 220, 788, 160};
  const Page page = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(page.itemCount, 0);
  const Rect shelf = SpineShelfPlanner::shelfRect(page, bounds);
  int occupiedLeft = page.slots[0].rect.x;
  int occupiedRight = page.slots[page.itemCount - 1].rect.x + page.slots[page.itemCount - 1].rect.width;
  if (page.plant.visible) {
    occupiedLeft = std::min(occupiedLeft, page.plant.rect.x);
    occupiedRight = std::max(occupiedRight, page.plant.rect.x + page.plant.rect.width);
  }
  EXPECT_EQ(shelf.y, bounds.y + bounds.height - SpineShelfPlanner::SHELF_BOTTOM_GAP);
  EXPECT_EQ(shelf.height, SpineShelfPlanner::SHELF_PLANK_HEIGHT);
  EXPECT_GE(shelf.height, 20);
  EXPECT_GE(occupiedLeft - shelf.x, SpineShelfPlanner::SHELF_OVERHANG_MIN);
  EXPECT_GE(shelf.x + shelf.width - occupiedRight, SpineShelfPlanner::SHELF_OVERHANG_MIN);
  EXPECT_LE(occupiedLeft - shelf.x, SpineShelfPlanner::SHELF_OVERHANG_MAX);
  EXPECT_LE(shelf.x + shelf.width - occupiedRight, SpineShelfPlanner::SHELF_OVERHANG_MAX);
  EXPECT_LE(std::abs((shelf.x * 2 + shelf.width) - (bounds.x * 2 + bounds.width)), 2);
  EXPECT_GE(shelf.x, bounds.x);
  EXPECT_LE(shelf.x + shelf.width, bounds.x + bounds.width);
}

TEST(SpineShelfPlanner, PacksLeftToRightAndPaginatesAtBoundaries) {
  const auto input = books();
  const Page first = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, Rect{0, 0, 180, 120});
  ASSERT_GT(first.itemCount, 0);
  EXPECT_EQ(first.pageNumber, 1);
  EXPECT_GT(first.pageCount, 1);
  for (size_t i = 1; i < first.itemCount; ++i) {
    EXPECT_GT(first.slots[i].rect.x, first.slots[i - 1].rect.x);
    EXPECT_LE(first.slots[i - 1].rect.x + first.slots[i - 1].rect.width + SpineShelfPlanner::BOOK_GAP,
              first.slots[i].rect.x);
  }
  const size_t next = SpineShelfPlanner::nextPageStart(input.data(), input.size(), 0, Rect{0, 0, 180, 120});
  ASSERT_GT(next, first.firstIndex);
  const Page nextPage = SpineShelfPlanner::pageForSelection(input.data(), input.size(), next, Rect{0, 0, 180, 120});
  EXPECT_EQ(nextPage.firstIndex, next);
  EXPECT_EQ(nextPage.pageNumber, 2);
  EXPECT_EQ(SpineShelfPlanner::previousPageStart(input.data(), input.size(), next, Rect{0, 0, 180, 120}), 0u);
}

TEST(SpineShelfPlanner, HitRectanglesMatchPlannerAndSelectionPages) {
  const auto input = books();
  const Page page = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 3, kWideShelf);
  ASSERT_GT(page.itemCount, 0);
  for (size_t i = 0; i < page.itemCount; ++i) {
    const auto& rect = page.slots[i].rect;
    EXPECT_TRUE(rect.contains(rect.x, rect.y));
    EXPECT_TRUE(rect.contains(rect.x + rect.width - 1, rect.y + rect.height - 1));
    EXPECT_FALSE(rect.contains(rect.x + rect.width, rect.y));
  }
  EXPECT_EQ(page.firstIndex, SpineShelfPlanner::pageForSelection(input.data(), input.size(), page.firstIndex, kWideShelf).firstIndex);
}

TEST(SpineShelfPlanner, TitlesAreUtf8SafeAndGracefullyOmittedWhenNarrow) {
  char output[40]{};
  const auto input = books();
  EXPECT_TRUE(SpineShelfPlanner::makeTitle(output, sizeof(output), input[2].title, Rect{0, 0, 40, 140}));
  EXPECT_NE(std::strstr(output, "caf"), nullptr);
  EXPECT_FALSE(SpineShelfPlanner::makeTitle(output, sizeof(output), input[1].title, Rect{0, 0, 24, 100}));
  const char malformed[] = "bad\xC3\x28";
  EXPECT_FALSE(SpineShelfPlanner::makeTitle(output, sizeof(output), malformed, Rect{0, 0, 40, 100}));
  const char replacement[] = "bad\xEF\xBF\xBD";
  EXPECT_FALSE(SpineShelfPlanner::makeTitle(output, sizeof(output), replacement, Rect{0, 0, 40, 100}));
}

TEST(SpineShelfPlanner, ArabicMetadataTitleStaysArabicAndOnlyEmptyMetadataUsesFilename) {
  char output[64]{};
  const char arabicTitle[] =
      "\xD8\xA7\xD9\x84\xD9\x82\xD8\xB1\xD8\xA2\xD9\x86 \xD8\xA7\xD9\x84\xD9\x83\xD8\xB1\xD9\x8A\xD9\x85";
  const Book arabicBook{"/books/quran.epub", arabicTitle, 0, ""};
  ASSERT_TRUE(SpineShelfPlanner::makeBookTitle(output, sizeof(output), arabicBook, Rect{0, 0, 42, 180}));
  EXPECT_EQ(std::strncmp(output, arabicTitle, 6), 0);
  EXPECT_EQ(std::strstr(output, "quran"), nullptr);
  EXPECT_EQ(std::strstr(output, "\xEF\xBF\xBD"), nullptr);

  const Book emptyMetadata{"/books/quran.epub", "", 0, ""};
  ASSERT_TRUE(SpineShelfPlanner::makeBookTitle(output, sizeof(output), emptyMetadata, Rect{0, 0, 42, 180}));
  EXPECT_STREQ(output, "quran");

  const char malformed[] = "bad\xC3\x28";
  const Book malformedMetadata{"/books/quran.epub", malformed, 0, ""};
  ASSERT_TRUE(SpineShelfPlanner::makeBookTitle(output, sizeof(output), malformedMetadata, Rect{0, 0, 42, 180}));
  EXPECT_STREQ(output, "quran");
}

TEST(SpineShelfPlanner, AuthorsFitOnlyWhenThereIsSafeVerticalRoom) {
  char output[32]{};
  EXPECT_TRUE(SpineShelfPlanner::makeAuthor(output, sizeof(output), "A. Writer", Rect{0, 0, 42, 180}));
  EXPECT_FALSE(SpineShelfPlanner::makeAuthor(output, sizeof(output), "A. Writer", Rect{0, 0, 30, 180}));
  const char malformed[] = "\xC3\x28writer";
  EXPECT_FALSE(SpineShelfPlanner::makeAuthor(output, sizeof(output), malformed, Rect{0, 0, 42, 180}));
  const char replacement[] = "\xEF\xBF\xBDwriter";
  EXPECT_FALSE(SpineShelfPlanner::makeAuthor(output, sizeof(output), replacement, Rect{0, 0, 42, 180}));
}

TEST(SpineShelfPlanner, BindingStylesAreStableAndBounded) {
  const auto input = books();
  for (const auto& book : input) {
    const auto first = SpineShelfPlanner::styleFor(book);
    const auto second = SpineShelfPlanner::styleFor(book);
    EXPECT_EQ(first, second);
    EXPECT_LE(static_cast<uint8_t>(first), static_cast<uint8_t>(SpineShelfPlanner::SpineStyle::Ornament));
  }
}

TEST(SpineShelfPlanner, TonesAreDeterministicAndBounded) {
  const auto input = books();
  bool sawWhite = false;
  bool sawLightGray = false;
  bool sawDarkGray = false;
  for (const auto& book : input) {
    const auto first = SpineShelfPlanner::toneFor(book);
    const auto second = SpineShelfPlanner::toneFor(book);
    EXPECT_EQ(first, second);
    EXPECT_LE(static_cast<uint8_t>(first), static_cast<uint8_t>(SpineShelfPlanner::SpineTone::DarkGray));
    sawWhite = sawWhite || first == SpineShelfPlanner::SpineTone::White;
    sawLightGray = sawLightGray || first == SpineShelfPlanner::SpineTone::LightGray;
    sawDarkGray = sawDarkGray || first == SpineShelfPlanner::SpineTone::DarkGray;
  }
  EXPECT_TRUE(sawWhite);
  EXPECT_TRUE(sawLightGray);
  EXPECT_TRUE(sawDarkGray);
}

TEST(SpineShelfPlanner, EmptyAndSmallCollectionsRemainBounded) {
  const auto input = books();
  const Page empty = SpineShelfPlanner::pageForSelection(input.data(), 0, 0, kWideShelf);
  EXPECT_EQ(empty.itemCount, 0);
  EXPECT_EQ(empty.pageCount, 0);
  const Page one = SpineShelfPlanner::pageForSelection(input.data(), 1, 0, kWideShelf);
  ASSERT_EQ(one.itemCount, 1);
  EXPECT_EQ(one.pageCount, 1);
  EXPECT_EQ(SpineShelfPlanner::nextPageStart(input.data(), 1, 0, kWideShelf), 0u);
  EXPECT_EQ(SpineShelfPlanner::previousPageStart(input.data(), 1, 0, kWideShelf), 0u);
}

TEST(SpineShelfPlanner, PlantPlacementIsDeterministicAndNeverOverlapsBooks) {
  const auto input = books();
  const Page first = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, kWideShelf);
  const Page second = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, kWideShelf);
  EXPECT_EQ(first.plant.visible, second.plant.visible);
  if (!first.plant.visible) return;
  EXPECT_EQ(first.plant.rect.x, second.plant.rect.x);
  EXPECT_EQ(first.plant.rect.y, second.plant.rect.y);
  for (size_t i = 0; i < first.itemCount; ++i) {
    const auto& bookRect = first.slots[i].rect;
    const bool separated = first.plant.rect.x + first.plant.rect.width <= bookRect.x ||
                           bookRect.x + bookRect.width <= first.plant.rect.x;
    EXPECT_TRUE(separated);
  }
}
