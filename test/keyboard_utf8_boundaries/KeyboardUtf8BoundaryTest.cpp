#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "KeyboardUtf8.h"

namespace {

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
