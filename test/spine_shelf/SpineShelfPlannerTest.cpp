#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "src/util/HomeSnapshotPolicy.h"
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

namespace {

template <size_t Count>
void captureThreeRowSnapshot(const std::array<Book, Count>& books,
                             const SpineShelfPlanner::ThreeRowPage& page,
                             std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY>& pathKeys,
                             std::array<HomeSnapshotPolicy::SnapshotRect,
                                        SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY>& rects) {
  pathKeys.fill(0);
  rects.fill({});
  for (size_t slot = 0; slot < page.itemCount; ++slot) {
    const auto& planned = page.slots[slot];
    const size_t localIndex = planned.itemIndex - page.firstIndex;
    pathKeys[localIndex] = books[planned.itemIndex].visualSeed + 1;
    rects[localIndex] = {static_cast<int16_t>(planned.rect.x), static_cast<int16_t>(planned.rect.y),
                         static_cast<int16_t>(planned.rect.width), static_cast<int16_t>(planned.rect.height)};
  }
}

}  // namespace

TEST(SpineShelfPlanner, TwentyTwoVariedWidthBooksRepackAfterBookFifteenMovesToRecentFront) {
  constexpr size_t bookCount = 22;
  std::array<std::array<char, 16>, bookCount> keys{};
  std::array<Book, bookCount> booksInRecentOrder{};
  for (size_t index = 0; index < bookCount; ++index) {
    std::snprintf(keys[index].data(), keys[index].size(), "varied-%02u", static_cast<unsigned>(index));
    booksInRecentOrder[index].key = keys[index].data();
    booksInRecentOrder[index].title = "Mixed width";
    booksInRecentOrder[index].visualSeed = static_cast<uint32_t>(index * 17 + 3);
  }

  constexpr Rect bounds{6, 220, 788, 160};
  const auto beforeMove = SpineShelfPlanner::threeRowPageForSelection(
      booksInRecentOrder.data(), bookCount, 15, bounds);
  ASSERT_EQ(beforeMove.firstIndex, 0u);
  ASSERT_EQ(beforeMove.itemCount, bookCount);
  std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> savedPaths{};
  std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentPaths{};
  std::array<HomeSnapshotPolicy::SnapshotRect, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> savedRects{};
  std::array<HomeSnapshotPolicy::SnapshotRect, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentRects{};
  captureThreeRowSnapshot(booksInRecentOrder, beforeMove, savedPaths, savedRects);

  const Book selected = booksInRecentOrder[15];
  for (size_t index = 15; index > 0; --index) booksInRecentOrder[index] = booksInRecentOrder[index - 1];
  booksInRecentOrder[0] = selected;
  const auto afterMove = SpineShelfPlanner::threeRowPageForSelection(
      booksInRecentOrder.data(), bookCount, 0, bounds);
  ASSERT_EQ(afterMove.firstIndex, 0u);
  ASSERT_EQ(afterMove.itemCount, bookCount);
  captureThreeRowSnapshot(booksInRecentOrder, afterMove, currentPaths, currentRects);

  EXPECT_FALSE(HomeSnapshotPolicy::threeRowPageStructureMatches(
      true, beforeMove.firstIndex == afterMove.firstIndex, beforeMove.itemCount, afterMove.itemCount,
      savedPaths, currentPaths, savedRects, currentRects));
  bool rowCenteringChanged = false;
  for (size_t slot = 0; slot < bookCount; ++slot) {
    rowCenteringChanged = rowCenteringChanged || savedRects[slot].x != currentRects[slot].x;
  }
  EXPECT_TRUE(rowCenteringChanged);

  // A rebuilt snapshot can be reused on subsequent reopen/return cycles while
  // the selected book remains first and the row geometry is unchanged.
  const auto rebuiltPaths = currentPaths;
  const auto rebuiltRects = currentRects;
  for (int cycle = 0; cycle < 2; ++cycle) {
    const auto reopened = SpineShelfPlanner::threeRowPageForSelection(
        booksInRecentOrder.data(), bookCount, 0, bounds);
    captureThreeRowSnapshot(booksInRecentOrder, reopened, currentPaths, currentRects);
    EXPECT_TRUE(HomeSnapshotPolicy::threeRowPageStructureMatches(
        true, afterMove.firstIndex == reopened.firstIndex, afterMove.itemCount, reopened.itemCount,
        rebuiltPaths, currentPaths, rebuiltRects, currentRects));
  }
}

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

TEST(SpineShelfPlanner, FocusFrameBoundsIncludeTheCompleteRightBorder) {
  const Rect finalSlot{74, 138, 46, 174};
  const auto frame = SpineShelfPlanner::focusFrameFor(finalSlot);

  ASSERT_TRUE(frame.valid());
  EXPECT_EQ(frame.left, finalSlot.x - 1);
  EXPECT_EQ(frame.right, finalSlot.x + finalSlot.width);
  EXPECT_EQ(frame.top, finalSlot.y - 1);
  EXPECT_EQ(frame.bottom, finalSlot.y + finalSlot.height - 1);
  EXPECT_TRUE(frame.updateBounds.contains(frame.left, frame.top));
  EXPECT_TRUE(frame.updateBounds.contains(frame.right, frame.top));
  EXPECT_TRUE(frame.updateBounds.contains(frame.left, frame.bottom));
  EXPECT_TRUE(frame.updateBounds.contains(frame.right, frame.bottom));
  EXPECT_EQ(frame.updateBounds.width, finalSlot.width + 2);
  EXPECT_EQ(frame.updateBounds.height, finalSlot.height + 1);

  // Computing a focus region must not change the final geometry shared by
  // rendering and hit testing.
  EXPECT_EQ(finalSlot.x, 74);
  EXPECT_EQ(finalSlot.y, 138);
  EXPECT_EQ(finalSlot.width, 46);
  EXPECT_EQ(finalSlot.height, 174);
}

TEST(SpineShelfPlanner, FocusFrameRejectsSlotsTooSmallForTheThreeSidedStroke) {
  EXPECT_FALSE(SpineShelfPlanner::focusFrameFor(Rect{10, 10, 2, 20}).valid());
  EXPECT_FALSE(SpineShelfPlanner::focusFrameFor(Rect{10, 10, 20, 2}).valid());
}

TEST(SpineShelfPlanner, DeselectionRestoresExactPreviouslyRenderedAdjacentSlotGeometry) {
  SpineShelfPlanner::RenderedFocusGeometry renderedFocus;
  const Rect previousSlot{38, 144, 42, 170};
  const Rect adjacentSlot{84, 136, 48, 178};
  renderedFocus.remember(3, previousSlot);

  // The planner's current candidate is the adjacent selection, but deselection
  // must restore the geometry of the item whose focus was actually painted.
  const auto deselection = SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, 3, adjacentSlot);
  ASSERT_TRUE(deselection.restoredPreviousGeometry);
  const auto& erasedSlot = deselection.finalSlot;
  const auto& erasedFrame = deselection.frame;
  EXPECT_EQ(erasedSlot.x, previousSlot.x);
  EXPECT_EQ(erasedSlot.y, previousSlot.y);
  EXPECT_EQ(erasedSlot.width, previousSlot.width);
  EXPECT_EQ(erasedSlot.height, previousSlot.height);
  EXPECT_EQ(SpineShelfPlanner::slotRightEdgeX(erasedSlot), previousSlot.x + previousSlot.width - 1);
  EXPECT_EQ(erasedFrame.left, previousSlot.x - 1);
  EXPECT_EQ(erasedFrame.right, previousSlot.x + previousSlot.width);
  EXPECT_EQ(erasedFrame.top, previousSlot.y - 1);
  EXPECT_EQ(erasedFrame.bottom, previousSlot.y + previousSlot.height - 1);
  EXPECT_NE(erasedFrame.right, adjacentSlot.x + adjacentSlot.width);

  renderedFocus.remember(4, adjacentSlot);
  const auto nextDeselect = SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, 4, previousSlot);
  ASSERT_TRUE(nextDeselect.restoredPreviousGeometry);
  const auto& nextSlot = nextDeselect.finalSlot;
  const auto& nextFrame = nextDeselect.frame;
  EXPECT_EQ(nextSlot.x, adjacentSlot.x);
  EXPECT_EQ(nextSlot.y, adjacentSlot.y);
  EXPECT_EQ(nextFrame.left, adjacentSlot.x - 1);
  EXPECT_EQ(nextFrame.right, adjacentSlot.x + adjacentSlot.width);
}

TEST(SpineShelfPlanner, NewlyRenderedPageReplacesPreviousPageFocusGeometry) {
  SpineShelfPlanner::RenderedFocusGeometry renderedFocus;
  renderedFocus.remember(8, Rect{20, 150, 40, 160});

  // A page repaint removes the previous frame; recording its selected item
  // then gives later deselection the geometry actually rendered on that page.
  renderedFocus.clear();
  const Rect newPageSlot{612, 142, 52, 168};
  renderedFocus.remember(9, newPageSlot);

  const auto wrongItem = SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, 8, Rect{600, 140, 40, 170});
  EXPECT_FALSE(wrongItem.restoredPreviousGeometry);
  EXPECT_EQ(wrongItem.finalSlot.x, 600);

  // The wrong-item lookup leaves the saved new-page selection intact.
  const auto deselection = SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, 9, Rect{0, 0, 1, 1});
  ASSERT_TRUE(deselection.restoredPreviousGeometry);
  const auto& erasedSlot = deselection.finalSlot;
  const auto& erasedFrame = deselection.frame;
  EXPECT_EQ(erasedSlot.x, newPageSlot.x);
  EXPECT_EQ(erasedSlot.width, newPageSlot.width);
  EXPECT_EQ(erasedFrame.left, newPageSlot.x - 1);
  EXPECT_EQ(erasedFrame.right, newPageSlot.x + newPageSlot.width);
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
    EXPECT_LE(static_cast<uint8_t>(first), static_cast<uint8_t>(SpineShelfPlanner::SpineStyle::OldLibrary));
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

TEST(SpineShelfPlanner, TwoRowShelfIsDeterministicAndUsesTwoRows) {
  const auto input = books();
  const Rect bounds{0, 0, 300, 300};
  const auto first = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  const auto second = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_EQ(first.itemCount, 10);
  ASSERT_EQ(first.itemCount, second.itemCount);
  const auto topShelf = SpineShelfPlanner::twoRowShelfRect(first, bounds, 0);
  const auto bottomShelf = SpineShelfPlanner::twoRowShelfRect(first, bounds, 1);
  bool sawTop = false;
  bool sawBottom = false;
  for (size_t i = 0; i < first.itemCount; ++i) {
    EXPECT_EQ(first.slots[i].rect.x, second.slots[i].rect.x);
    EXPECT_EQ(first.slots[i].rect.y, second.slots[i].rect.y);
    EXPECT_TRUE(first.slots[i].rect.contains(first.slots[i].rect.x, first.slots[i].rect.y));
    if (first.slots[i].rect.y + first.slots[i].rect.height <= topShelf.y) sawTop = true;
    if (first.slots[i].rect.y + first.slots[i].rect.height <= bottomShelf.y &&
        first.slots[i].rect.y >= topShelf.y) sawBottom = true;
  }
  EXPECT_TRUE(sawTop);
  EXPECT_TRUE(sawBottom);
  EXPECT_LT(topShelf.y, bottomShelf.y);
  EXPECT_EQ(topShelf.height, SpineShelfPlanner::TWO_ROW_SHELF_HEIGHT);
  EXPECT_EQ(bottomShelf.height, SpineShelfPlanner::TWO_ROW_SHELF_HEIGHT);
  EXPECT_EQ(topShelf.x, bounds.x);
  EXPECT_EQ(topShelf.width, bounds.width);
  EXPECT_EQ(bottomShelf.x, bounds.x);
  EXPECT_EQ(bottomShelf.width, bounds.width);
}

TEST(SpineShelfPlanner, TwoRowSpinesStayThinAndInsideTheBoundedShelves) {
  const auto input = books();
  const Rect bounds{0, 0, 300, 300};
  const auto page = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_EQ(page.itemCount, 10);
  const auto topShelf = SpineShelfPlanner::twoRowShelfRect(page, bounds, 0);
  const auto bottomShelf = SpineShelfPlanner::twoRowShelfRect(page, bounds, 1);
  for (size_t i = 0; i < page.itemCount; ++i) {
    EXPECT_GE(page.slots[i].rect.width, SpineShelfPlanner::TWO_ROW_MIN_BOOK_WIDTH);
    EXPECT_LE(page.slots[i].rect.width, SpineShelfPlanner::TWO_ROW_MAX_BOOK_WIDTH);
    EXPECT_GT(page.slots[i].rect.height, 0);
    const bool topRow = page.slots[i].rect.y + page.slots[i].rect.height <= topShelf.y;
    EXPECT_LE(page.slots[i].rect.y + page.slots[i].rect.height, (topRow ? topShelf : bottomShelf).y);
  }
  const auto lowerShelf = SpineShelfPlanner::twoRowShelfRect(page, bounds, 1);
  EXPECT_LE(lowerShelf.y + lowerShelf.height, bounds.y + bounds.height);
}

TEST(SpineShelfPlanner, TwoRowCentersEachPopulatedRowAndOmitsEmptyShelf) {
  const auto input = books();
  const Rect bounds{0, 0, 480, 300};
  const auto page = SpineShelfPlanner::twoRowPageForSelection(input.data(), 3, 0, bounds);
  ASSERT_EQ(page.itemCount, 3);
  const int shelfCenter = bounds.x + bounds.width / 2;
  size_t topCount = 0;
  while (topCount < page.itemCount && page.slots[topCount].rect.y + page.slots[topCount].rect.height <=
                                           SpineShelfPlanner::twoRowShelfRect(page, bounds, 0).y) {
    ++topCount;
  }
  ASSERT_GT(topCount, 0);
  const int topLeft = page.slots[0].rect.x;
  const int topRight = page.slots[topCount - 1].rect.x + page.slots[topCount - 1].rect.width;
  EXPECT_LE(std::abs((topLeft + topRight) / 2 - shelfCenter), 1);
  const auto topShelf = SpineShelfPlanner::twoRowShelfRect(page, bounds, 0);
  const auto bottomShelf = SpineShelfPlanner::twoRowShelfRect(page, bounds, 1);
  EXPECT_EQ(topShelf.width, bounds.width);
  EXPECT_EQ(bottomShelf.width, 0);
  EXPECT_EQ(bottomShelf.height, 0);
}

TEST(SpineShelfPlanner, TwoRowPlantIsDeterministicAndNeverOverlapsBooks) {
  const auto input = books();
  const Rect bounds{0, 0, 900, 300};
  const auto first = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  const auto second = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  for (size_t row = 0; row < 2; ++row) {
    EXPECT_EQ(first.plants[row].visible, second.plants[row].visible);
    if (!first.plants[row].visible) continue;
    EXPECT_EQ(first.plants[row].rect.x, second.plants[row].rect.x);
    for (size_t i = 0; i < first.itemCount; ++i) {
      const auto& bookRect = first.slots[i].rect;
      const auto shelf = SpineShelfPlanner::twoRowShelfRect(first, bounds, row);
      const bool sameRow = first.slots[i].rect.y + first.slots[i].rect.height <= shelf.y;
      if (!sameRow) continue;
      const bool separated = first.plants[row].rect.x + first.plants[row].rect.width <= bookRect.x ||
                             bookRect.x + bookRect.width <= first.plants[row].rect.x;
      EXPECT_TRUE(separated);
    }
  }
}

TEST(SpineShelfPlanner, TwoRowCapacityComesFromGeometryRatherThanTenBookCap) {
  // The evolved geometry can legitimately fit twenty books on this shelf.
  // Use enough books to exercise a real page boundary while still proving
  // that a page can hold more than the old ten-book limit.
  std::array<SpineShelfPlanner::Book, 40> input{};
  for (auto& book : input) book = {"same-width-book", "Title", 0, "Author"};

  const Rect bounds{0, 0, 480, 300};
  const auto page = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  EXPECT_GT(page.itemCount, SpineShelfPlanner::MAX_BOOKS);
  EXPECT_LE(page.itemCount, SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY);
  EXPECT_GT(page.pageCount, 1);
}

TEST(SpineShelfPlanner, TwoRowShelfPaginatesAndPreservesHitSelection) {
  const auto input = books();
  const Rect bounds{0, 0, 180, 260};
  const auto first = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(first.itemCount, 0);
  EXPECT_GT(first.pageCount, 1);
  const size_t next = SpineShelfPlanner::twoRowNextPageStart(input.data(), input.size(), 0, bounds);
  ASSERT_GT(next, first.firstIndex);
  const auto nextPage = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), next, bounds);
  EXPECT_EQ(nextPage.firstIndex, next);
  EXPECT_EQ(SpineShelfPlanner::twoRowPreviousPageStart(input.data(), input.size(), next, bounds), 0u);
  for (size_t i = 0; i < nextPage.itemCount; ++i) {
    const auto& rect = nextPage.slots[i].rect;
    EXPECT_TRUE(rect.contains(rect.x, rect.y));
    EXPECT_FALSE(rect.contains(rect.x + rect.width, rect.y));
  }
}

TEST(SpineShelfPlanner, ThreeRowShelfUsesThreeBoundedRowsAndPaginates) {
  std::array<SpineShelfPlanner::Book, 60> input{};
  for (auto& book : input) book = {"three-row-book", "Title", 25, "Author"};
  const Rect bounds{4, 0, 480, 300};
  const auto first = SpineShelfPlanner::threeRowPageForSelection(input.data(), input.size(), 0, bounds);
  const auto repeated = SpineShelfPlanner::threeRowPageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(first.itemCount, 0);
  EXPECT_EQ(first.itemCount, repeated.itemCount);
  EXPECT_LE(first.itemCount, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY);

  int priorShelfY = -1;
  for (size_t row = 0; row < 3; ++row) {
    const auto shelf = SpineShelfPlanner::threeRowShelfRect(first, bounds, row);
    ASSERT_GT(shelf.height, 0);
    EXPECT_EQ(shelf.height, SpineShelfPlanner::THREE_ROW_SHELF_HEIGHT);
    EXPECT_GT(shelf.y, priorShelfY);
    priorShelfY = shelf.y;
    EXPECT_LE(shelf.y + shelf.height, bounds.y + bounds.height);
    bool found = false;
    for (size_t slot = 0; slot < first.itemCount; ++slot) {
      const auto& rect = first.slots[slot].rect;
      EXPECT_EQ(rect.x, repeated.slots[slot].rect.x);
      EXPECT_EQ(rect.y, repeated.slots[slot].rect.y);
      if (rect.y + rect.height <= shelf.y && rect.y >= bounds.y + static_cast<int>(row) * 70) found = true;
    }
    EXPECT_TRUE(found);
  }

  ASSERT_GT(first.pageCount, 1);
  const size_t next = SpineShelfPlanner::threeRowNextPageStart(input.data(), input.size(), 0, bounds);
  EXPECT_EQ(next, first.itemCount);
  const auto nextPage = SpineShelfPlanner::threeRowPageForSelection(input.data(), input.size(), next, bounds);
  EXPECT_EQ(nextPage.firstIndex, next);
  EXPECT_EQ(SpineShelfPlanner::threeRowPreviousPageStart(input.data(), input.size(), next, bounds), 0u);
  EXPECT_EQ(SpineShelfPlanner::threeRowNextPageStart(input.data(), input.size(), nextPage.firstIndex, bounds),
            nextPage.pageNumber < nextPage.pageCount ? nextPage.firstIndex + nextPage.itemCount : 0u);
}

TEST(SpineShelfPlanner, OneRowAppearanceModesKeepSpineAndCoverGeometrySeparate) {
  auto input = books();
  const Rect bounds{0, 0, 480, 300};
  const Page spines = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(spines.itemCount, 0);
  for (size_t i = 0; i < spines.itemCount; ++i) EXPECT_FALSE(spines.slots[i].cover);

  for (auto& book : input) book.appearance = SpineShelfPlanner::Appearance::CoversOnly;
  const Page covers = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(covers.itemCount, 0);
  EXPECT_LT(covers.itemCount, spines.itemCount);
  for (size_t i = 0; i < covers.itemCount; ++i) {
    EXPECT_TRUE(covers.slots[i].cover);
    const auto& rect = covers.slots[i].rect;
    EXPECT_LE(std::abs(rect.width * 3 - rect.height * 2), 3);
  }

  for (auto& book : input) book.appearance = SpineShelfPlanner::Appearance::SpinesAndCovers;
  const Page mixed = SpineShelfPlanner::pageForSelection(input.data(), input.size(), 0, bounds);
  bool sawSpine = false;
  bool sawCover = false;
  for (size_t i = 0; i < mixed.itemCount; ++i) {
    sawCover = sawCover || mixed.slots[i].cover;
    sawSpine = sawSpine || !mixed.slots[i].cover;
  }
  EXPECT_TRUE(sawSpine);
  EXPECT_TRUE(sawCover);
}

TEST(SpineShelfPlanner, TwoRowAppearanceModesPreserveBothRowsAndCoverFallbackGeometry) {
  std::array<Book, 24> input{};
  std::array<std::array<char, 12>, 24> keys{};
  for (size_t i = 0; i < input.size(); ++i) {
    std::snprintf(keys[i].data(), keys[i].size(), "book-%zu", i);
    input[i] = {keys[i].data(), "UTF-8 — café", 23, "Author"};
  }
  const Rect bounds{0, 0, 480, 300};
  const auto spines = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(spines.itemCount, 0);
  for (size_t i = 0; i < spines.itemCount; ++i) EXPECT_FALSE(spines.slots[i].cover);

  // A null prepared-thumbnail path models an unavailable cached cover. The
  // planner still produces bounded, selectable face-out slots; rendering owns
  // the placeholder fallback and never needs to inspect the book source.
  for (auto& book : input) book.appearance = SpineShelfPlanner::Appearance::CoversOnly;
  const auto covers = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  ASSERT_GT(covers.itemCount, 0);
  EXPECT_LT(covers.itemCount, spines.itemCount);
  for (size_t i = 0; i < covers.itemCount; ++i) {
    EXPECT_TRUE(covers.slots[i].cover);
    const auto& rect = covers.slots[i].rect;
    EXPECT_LE(std::abs(rect.width * 3 - rect.height * 2), 3);
    EXPECT_GT(rect.x, bounds.x - 1);
    EXPECT_LT(rect.x + rect.width, bounds.x + bounds.width + 1);
  }

  for (auto& book : input) book.appearance = SpineShelfPlanner::Appearance::SpinesAndCovers;
  const auto mixed = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  const auto repeatedMixed = SpineShelfPlanner::twoRowPageForSelection(input.data(), input.size(), 0, bounds);
  bool sawMixedSpine = false;
  bool sawMixedCover = false;
  ASSERT_EQ(mixed.itemCount, repeatedMixed.itemCount);
  for (size_t i = 0; i < mixed.itemCount; ++i) {
    EXPECT_EQ(mixed.slots[i].cover, repeatedMixed.slots[i].cover);
    EXPECT_EQ(mixed.slots[i].rect.x, repeatedMixed.slots[i].rect.x);
    sawMixedCover = sawMixedCover || mixed.slots[i].cover;
    sawMixedSpine = sawMixedSpine || !mixed.slots[i].cover;
  }
  EXPECT_TRUE(sawMixedSpine);
  EXPECT_TRUE(sawMixedCover);
}
