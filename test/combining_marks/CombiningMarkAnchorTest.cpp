#include <gtest/gtest.h>

#include "lib/EpdFont/EpdFontData.h"
#include "lib/Utf8/Utf8.h"

// ============================================================================
// Anchor selection and placement for combining marks without GPOS tables.
//
// Hebrew niqqud whose identity depends on position must not use the default
// centre-and-raise heuristic: dagesh sits inside the letter body, the
// shin/sin dots sit over the letter's right/left arm, and holam hangs over
// the left corner (see PR #2541 review feedback).
// ============================================================================

using combiningMark::Anchor;
using combiningMark::anchorFor;
using combiningMark::anchorOver;
using combiningMark::anchorOverRotated90CW;
using combiningMark::raiseAboveBase;

TEST(AnchorFor, PositionSensitiveNiqqud) {
  EXPECT_EQ(anchorFor(0x05BC), Anchor::CenterNative);  // dagesh/mapiq
  EXPECT_EQ(anchorFor(0x05BA), Anchor::CenterNative);  // holam haser for vav
  EXPECT_EQ(anchorFor(0x05C1), Anchor::RightNative);   // shin dot
  EXPECT_EQ(anchorFor(0x05C2), Anchor::LeftNative);    // sin dot
  EXPECT_EQ(anchorFor(0x05B9), Anchor::LeftNative);    // holam
}

TEST(AnchorFor, EverythingElseKeepsCentreRaisedDefault) {
  EXPECT_EQ(anchorFor(0x05B0), Anchor::CenterRaised);  // Hebrew sheva
  EXPECT_EQ(anchorFor(0x05B7), Anchor::CenterRaised);  // Hebrew patach
  EXPECT_EQ(anchorFor(0x064E), Anchor::CenterRaised);  // Arabic fatha
  EXPECT_EQ(anchorFor(0x0651), Anchor::CenterRaised);  // Arabic shadda
  EXPECT_EQ(anchorFor(0x0301), Anchor::CenterRaised);  // combining acute
}

// Base glyph: cursor 100, left 1, width 12.  Mark: left 2, width 4.
TEST(AnchorOver, HorizontalPlacementPerAnchor) {
  // Centered: base bitmap spans [101, 113), centre 107; mark starts at 105.
  EXPECT_EQ(anchorOver(Anchor::CenterRaised, 100, 1, 12, 2, 4), 105 - 2);
  EXPECT_EQ(anchorOver(Anchor::CenterNative, 100, 1, 12, 2, 4), 105 - 2);
  // Left-aligned: mark bitmap starts at the base bitmap's left edge (101).
  EXPECT_EQ(anchorOver(Anchor::LeftNative, 100, 1, 12, 2, 4), 101 - 2);
  // Right-aligned: mark bitmap ends at the base bitmap's right edge (113).
  EXPECT_EQ(anchorOver(Anchor::RightNative, 100, 1, 12, 2, 4), 109 - 2);
}

// The rotated coordinate system inverts every left/width term, so the mark's
// offset from the base cursor must be the exact mirror of the unrotated one.
TEST(AnchorOverRotated90CW, MirrorsUnrotatedOffsets) {
  for (const Anchor anchor : {Anchor::CenterRaised, Anchor::CenterNative, Anchor::LeftNative, Anchor::RightNative}) {
    const int offset = anchorOver(anchor, 100, 1, 12, 2, 4) - 100;
    EXPECT_EQ(anchorOverRotated90CW(anchor, 100, 1, 12, 2, 4), 100 - offset);
  }
}

TEST(RaiseAboveBase, NativeAnchorsKeepFontDesignedHeight) {
  // A dagesh-like dot designed inside the letter body (top 8 of a base whose
  // top is 12) must NOT be hoisted above the letter.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterNative, 8, 3, 12), 0);
  // Shin/sin dots overlapping the letter's top must not be pushed clear.
  EXPECT_EQ(raiseAboveBase(Anchor::RightNative, 13, 3, 12), 0);
  EXPECT_EQ(raiseAboveBase(Anchor::LeftNative, 13, 3, 12), 0);
}

TEST(RaiseAboveBase, CentreRaisedBehaviourUnchanged) {
  // Above-baseline mark colliding with the base: raised to restore a 1px gap.
  // gap = markTop - markHeight - baseTop = 10 - 3 - 12 = -5  ->  raise 6.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 10, 3, 12), 6);
  // Already clear of the base: no raise.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 16, 3, 12), 0);
  // Below-baseline mark (kasra, cedilla): stays at font-native position.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 2, 4, 12), 0);
}

TEST(StackOffsetForOverlap, ShaddaAndFathaStackAboveTheBase) {
  const combiningMark::Bounds previous{20, 40, 30, 50};
  const combiningMark::Bounds fatha{20, 45, 30, 55};

  const int delta = combiningMark::stackOffsetForOverlap(fatha, previous, combiningMark::StackAxis::Y, -1);
  EXPECT_EQ(delta, -17);
  EXPECT_EQ(fatha.y1 + delta, 38);  // one clear row before the previous mark
}

TEST(StackOffsetForOverlap, ShaddaAndDammaUseTheSameGenericAboveRule) {
  const combiningMark::Bounds previous{20, 40, 30, 50};
  const combiningMark::Bounds damma{19, 44, 31, 54};

  const int delta = combiningMark::stackOffsetForOverlap(damma, previous, combiningMark::StackAxis::Y, -1);
  EXPECT_EQ(delta, -16);
  EXPECT_EQ(damma.y1 + delta, 38);
}

TEST(StackOffsetForOverlap, KasraAndOtherBelowMarksStackDownward) {
  const combiningMark::Bounds previous{20, 50, 30, 60};
  const combiningMark::Bounds kasra{20, 55, 30, 65};

  const int delta = combiningMark::stackOffsetForOverlap(kasra, previous, combiningMark::StackAxis::Y, 1);
  EXPECT_EQ(delta, 7);
  EXPECT_EQ(kasra.y0 + delta, 62);  // one clear row after the previous mark
}

TEST(StackOffsetForOverlap, QuranicAndTanwinMarksUseBoundsNotCodepointSpecialCases) {
  EXPECT_TRUE(utf8IsArabicCodepoint(0x06E1));  // small high dotless head of khah
  EXPECT_TRUE(utf8IsArabicCodepoint(0x06D6));  // Quranic annotation mark
  EXPECT_TRUE(utf8IsArabicCodepoint(0x064B));  // fathatan

  const combiningMark::Bounds previous{20, 35, 30, 45};
  const combiningMark::Bounds quranicMark{20, 40, 30, 50};
  EXPECT_NE(combiningMark::stackOffsetForOverlap(quranicMark, previous, combiningMark::StackAxis::Y, -1), 0);
}

TEST(StackOffsetForOverlap, ThreeMarksAreKeptApartIncrementally) {
  const combiningMark::Bounds first{20, 40, 30, 50};
  const combiningMark::Bounds second{20, 45, 30, 55};
  const int secondDelta =
      combiningMark::stackOffsetForOverlap(second, first, combiningMark::StackAxis::Y, -1);
  combiningMark::Bounds positionedSecond = second;
  positionedSecond.y0 += secondDelta;
  positionedSecond.y1 += secondDelta;

  const combiningMark::Bounds third{20, 35, 30, 45};
  const int thirdDelta =
      combiningMark::stackOffsetForOverlap(third, positionedSecond, combiningMark::StackAxis::Y, -1);
  EXPECT_LT(thirdDelta, 0);
  EXPECT_LT(third.y1 + thirdDelta, positionedSecond.y0);
}

TEST(StackOffsetForOverlap, NonOverlappingMarksRemainAtTheirOriginalPosition) {
  const combiningMark::Bounds previous{20, 35, 30, 45};
  const combiningMark::Bounds alreadySeparated{20, 20, 30, 30};
  EXPECT_EQ(combiningMark::stackOffsetForOverlap(alreadySeparated, previous, combiningMark::StackAxis::Y, -1), 0);

  const combiningMark::Bounds single{20, 20, 30, 30};
  EXPECT_EQ(combiningMark::stackOffsetForOverlap(single, previous, combiningMark::StackAxis::Y, -1), 0);
}

TEST(StackOffsetForOverlap, HorizontalAndLatinGeometryAreUnaffected) {
  const combiningMark::Bounds previous{20, 35, 30, 45};
  const combiningMark::Bounds disjointX{31, 35, 41, 45};
  EXPECT_EQ(combiningMark::stackOffsetForOverlap(disjointX, previous, combiningMark::StackAxis::Y, -1), 0);
}
