#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "KeyboardUtf8.h"
#include "KeyboardNavigation.h"

namespace {

struct NavigationKey {
  uint8_t widthUnits = 1;
};

struct NavigationRow {
  const NavigationKey* keys = nullptr;
  uint8_t count = 0;
  uint8_t insetUnits = 0;
};

const std::string kMixedUtf8 = std::string("A") + "\xC3\xA9" + "\xD0\x90" + "\xE2\x82\xAC" +
                                "\xF0\x90\x8D\x88" + "\xD9\x85" + "z";

bool isBoundary(const std::string& text, const std::size_t pos) {
  return pos == 0 || pos == text.length() || !keyboard_utf8::isContinuationByte(text[pos]);
}

}  // namespace

TEST(KeyboardUtf8Boundary, SnapsEveryRawOffsetToACompleteCodepoint) {
  // ASCII, 2-byte accented Latin/Cyrillic/Arabic, 3-byte Euro, and a 4-byte
  // supplementary codepoint all occur in one mixed input.
  const std::vector<std::size_t> boundaries{0, 1, 3, 5, 8, 12, 14, 15};

  for (std::size_t raw = 0; raw <= kMixedUtf8.length(); ++raw) {
    const std::size_t snapped = keyboard_utf8::endAtOrBefore(kMixedUtf8, raw);
    ASSERT_TRUE(isBoundary(kMixedUtf8, snapped));
    ASSERT_LE(snapped, raw);
    EXPECT_EQ(snapped, *std::prev(std::upper_bound(boundaries.begin(), boundaries.end(), raw)));
  }
}

TEST(KeyboardUtf8Boundary, NextBoundaryWalkNeverStopsInsideACharacter) {
  std::vector<std::size_t> actual;
  for (std::size_t pos = 0;;) {
    actual.push_back(pos);
    if (pos == kMixedUtf8.length()) break;
    pos = keyboard_utf8::nextBoundary(kMixedUtf8, pos);
  }

  EXPECT_EQ(actual, (std::vector<std::size_t>{0, 1, 3, 5, 8, 12, 14, 15}));
}

TEST(KeyboardUtf8Boundary, NarrowWrapCandidatesRemainBoundaries) {
  // Exercise the same local snap operation used by the byte-index binary
  // search when a narrow field forces a break through each encoded character.
  for (std::size_t raw = 1; raw < kMixedUtf8.length(); ++raw) {
    const std::size_t candidate = keyboard_utf8::endAtOrBefore(kMixedUtf8, raw);
    EXPECT_TRUE(isBoundary(kMixedUtf8, candidate));
    EXPECT_LE(candidate, raw);
  }
}

TEST(KeyboardNavigation, HorizontalMovementStopsAtRowEdges) {
  EXPECT_EQ(keyboard_navigation::moveColumn(0, 7, -1), 0);
  EXPECT_EQ(keyboard_navigation::moveColumn(6, 7, 1), 6);
  EXPECT_EQ(keyboard_navigation::moveColumn(5, 7, 1), 6);
  EXPECT_EQ(keyboard_navigation::moveColumn(1, 7, -1), 0);
}

TEST(KeyboardNavigation, InvalidColumnsAndOutOfRangeSelectionsAreBounded) {
  EXPECT_EQ(keyboard_navigation::moveColumn(4, 0, 1), 0);
  EXPECT_EQ(keyboard_navigation::moveColumn(-1, 4, 1), 1);
  EXPECT_EQ(keyboard_navigation::moveColumn(8, 4, -1), 2);
}

TEST(KeyboardNavigation, VerticalTravelUsesRenderedKeyCentersAndWideOkKey) {
  const NavigationKey letters[] = {{1}, {1}, {1}, {1}, {1}, {1}, {1}, {1}, {1}, {1}};
  const NavigationKey bottom[] = {{2}, {1}, {1}, {1}, {2}, {2}};  // ?123, space, URL, OK
  const NavigationRow letterRow{letters, 10, 0};
  const NavigationRow bottomRow{bottom, 6, 0};

  const int col = keyboard_navigation::nearestColumnByVisualCenter(letterRow, 9, bottomRow, 100, 3);
  ASSERT_EQ(col, 5);
  EXPECT_EQ(keyboard_navigation::moveColumn(col, bottomRow.count, 1), col);
  EXPECT_EQ(keyboard_navigation::moveColumn(col, bottomRow.count, -1), 4);
}

TEST(KeyboardNavigation, VerticalTravelIncludesInsetsAndUsesNearestPhysicalCenter) {
  const NavigationKey source[] = {{1}, {1}, {1}, {1}, {1}, {1}, {1}, {1}, {1}, {1}};
  const NavigationKey insetRowKeys[] = {{2}, {1}, {1}, {2}};
  const NavigationRow sourceRow{source, 10, 0};
  const NavigationRow insetRow{insetRowKeys, 4, 1};

  const int column = keyboard_navigation::nearestColumnByVisualCenter(sourceRow, 7, insetRow, 120, 4);
  EXPECT_EQ(column, 3);
}

TEST(KeyboardNavigation, VerticalEdgesDoNotWrapToTheOppositeRow) {
  EXPECT_EQ(keyboard_navigation::moveRow(0, 5, -1), 0);
  EXPECT_EQ(keyboard_navigation::moveRow(4, 5, 1), 4);
  EXPECT_EQ(keyboard_navigation::moveRow(2, 5, -1), 1);
  EXPECT_EQ(keyboard_navigation::moveRow(2, 5, 1), 3);
}

TEST(KeyboardNavigation, HorizontalRepeatUsesItsOwnPressAndReleaseTimeline) {
  keyboard_navigation::KeyRepeatGate left;
  keyboard_navigation::KeyRepeatGate right;

  EXPECT_TRUE(left.update(true, true, false, 1000));  // initial step
  EXPECT_FALSE(left.update(false, true, false, 1499));
  EXPECT_TRUE(left.update(false, true, false, 1500));  // first repeat after its own 500 ms
  EXPECT_TRUE(right.update(true, true, false, 1510));  // a new key starts a fresh repeat clock
  EXPECT_FALSE(right.update(false, true, false, 1511));
  EXPECT_FALSE(left.update(false, true, false, 1999));
  EXPECT_TRUE(left.update(false, true, false, 2000));
  EXPECT_FALSE(right.update(false, true, false, 2009));
  EXPECT_TRUE(right.update(false, true, false, 2010));
  EXPECT_FALSE(right.update(false, false, true, 2011));
  EXPECT_TRUE(right.update(true, true, false, 2050));
}

TEST(KeyboardNavigation, ShortClickWithBothEdgesInOnePollStillMovesOnce) {
  keyboard_navigation::KeyRepeatGate gate;
  EXPECT_TRUE(gate.update(true, false, true, 1000));
  EXPECT_FALSE(gate.update(false, false, true, 1001));
  EXPECT_TRUE(gate.update(true, true, false, 1100));
}

TEST(KeyboardNavigation, HeldKeyAtEntryIsIgnoredUntilReleased) {
  keyboard_navigation::KeyRepeatGate gate;
  gate.reset(true);
  EXPECT_FALSE(gate.update(false, true, false, 1000));
  EXPECT_FALSE(gate.update(false, true, false, 1600));
  EXPECT_FALSE(gate.update(false, false, true, 1610));
  EXPECT_TRUE(gate.update(true, true, false, 1700));
}
