#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "util/SleepComposer.h"

TEST(SleepComposer, ExistingSleepModeValuesStayStableAndFlexibleIsAppended) {
  EXPECT_EQ(SleepScreenModeValue::DARK, 0);
  EXPECT_EQ(SleepScreenModeValue::LIGHT, 1);
  EXPECT_EQ(SleepScreenModeValue::CUSTOM, 2);
  EXPECT_EQ(SleepScreenModeValue::COVER, 3);
  EXPECT_EQ(SleepScreenModeValue::COVER_CUSTOM, 4);
  EXPECT_EQ(SleepScreenModeValue::BLANK, 5);
  EXPECT_EQ(SleepScreenModeValue::QUICK_RESUME, 6);
  EXPECT_EQ(SleepScreenModeValue::OVERLAY, 7);
  EXPECT_EQ(SleepScreenModeValue::COVER_OVERLAY, 8);
  EXPECT_EQ(SleepScreenModeValue::READING_STATS_SLEEP, 9);
  EXPECT_EQ(SleepScreenModeValue::MINIMAL_STATS, 10);
  EXPECT_EQ(SleepScreenModeValue::CLIPPING_COVER, 11);
  EXPECT_EQ(SleepScreenModeValue::TODO_LIST, 12);
  EXPECT_EQ(SleepScreenModeValue::READING_CALENDAR_SLEEP, 13);
  EXPECT_EQ(SleepScreenModeValue::READING_SUMMARY_SLEEP, 14);
  EXPECT_EQ(SleepScreenModeValue::COVER_PROGRESS, 15);
  EXPECT_EQ(SleepScreenModeValue::RANDOM, 16);
  EXPECT_EQ(SleepScreenModeValue::FLEXIBLE_COMPOSITE, 17);
}

TEST(SleepComposer, FlexibleSettingsRoundTripKeepsSharedElementValues) {
  const auto values = normalizeSleepComposerSettings(
      static_cast<uint8_t>(SleepComposerElement::ReadingSummary),
      static_cast<uint8_t>(SleepComposerElement::ToDo),
      static_cast<uint8_t>(SleepComposerElement::BookProgress));

  EXPECT_EQ(values.background, static_cast<uint8_t>(SleepComposerElement::ReadingSummary));
  EXPECT_EQ(values.overlayA, static_cast<uint8_t>(SleepComposerElement::ToDo));
  EXPECT_EQ(values.overlayB, static_cast<uint8_t>(SleepComposerElement::BookProgress));
  EXPECT_EQ(normalizeSleepComposerSettings(255, 255, 255).background,
            static_cast<uint8_t>(SleepComposerElement::CurrentBookCover));
}

TEST(SleepComposer, RandomPreviewDetectionUsesOnlyTheThreeConfiguredLayers) {
  EXPECT_FALSE(sleepComposerHasRandomElement(static_cast<uint8_t>(SleepComposerElement::CurrentBookCover),
                                             static_cast<uint8_t>(SleepComposerElement::ReadingCalendar),
                                             static_cast<uint8_t>(SleepComposerElement::ClockDate)));
  EXPECT_TRUE(sleepComposerHasRandomElement(static_cast<uint8_t>(SleepComposerElement::Random),
                                            static_cast<uint8_t>(SleepComposerElement::ReadingCalendar),
                                            static_cast<uint8_t>(SleepComposerElement::ClockDate)));
  EXPECT_TRUE(sleepComposerHasRandomElement(static_cast<uint8_t>(SleepComposerElement::CurrentBookCover),
                                            static_cast<uint8_t>(SleepComposerElement::Random),
                                            static_cast<uint8_t>(SleepComposerElement::ClockDate)));
  EXPECT_TRUE(sleepComposerHasRandomElement(static_cast<uint8_t>(SleepComposerElement::CurrentBookCover),
                                            static_cast<uint8_t>(SleepComposerElement::ReadingCalendar),
                                            static_cast<uint8_t>(SleepComposerElement::Random)));
  EXPECT_FALSE(sleepComposerHasRandomElement(255, 255, 255));
}

TEST(SleepComposer, RandomCandidatesExcludeUnavailableSourcesAndNeverSelectRandom) {
  SleepComposerAvailability available;
  available.currentBook = false;
  available.bookCover = false;
  available.currentPage = false;
  available.customImage = false;
  available.transparentPng = false;
  available.readingCalendar = false;
  available.readingSummary = true;
  available.todo = false;
  available.clipping = false;
  available.clockDate = false;

  const auto candidates = buildSleepComposerCandidates(SleepComposerRole::Base, available);
  ASSERT_GT(candidates.count, 0U);
  for (size_t i = 0; i < candidates.count; ++i) {
    EXPECT_NE(candidates.elements[i], SleepComposerElement::None);
    EXPECT_NE(candidates.elements[i], SleepComposerElement::Random);
    EXPECT_TRUE(sleepComposerElementAvailable(candidates.elements[i], SleepComposerRole::Base, available));
  }
  EXPECT_EQ(resolveSleepComposerElement(SleepComposerElement::ToDo, SleepComposerRole::OverlayPrimary,
                                        available, 0),
            SleepComposerElement::None);
}

TEST(SleepComposer, RandomLayersAvoidDuplicatesWhenOtherValidChoicesExist) {
  SleepComposerAvailability available;
  const auto base = resolveSleepComposerElement(SleepComposerElement::Random, SleepComposerRole::Base,
                                                available, 0);
  const std::array<SleepComposerElement, 1> first = {base};
  const auto overlayA = resolveSleepComposerElement(SleepComposerElement::Random,
                                                     SleepComposerRole::OverlayPrimary, available, 0,
                                                     first.data(), first.size());
  const std::array<SleepComposerElement, 2> earlier = {base, overlayA};
  const auto overlayB = resolveSleepComposerElement(SleepComposerElement::Random,
                                                     SleepComposerRole::OverlaySecondary, available, 0,
                                                     earlier.data(), earlier.size());
  EXPECT_NE(base, overlayA);
  EXPECT_NE(base, overlayB);
  EXPECT_NE(overlayA, overlayB);
}

TEST(SleepComposer, MissingCurrentBookImageClippingsAndTodoAreExcludedFromRandom) {
  SleepComposerAvailability available;
  available.currentBook = false;
  available.bookCover = false;
  available.clipping = false;
  available.todo = false;
  const auto candidates = buildSleepComposerCandidates(SleepComposerRole::OverlayPrimary, available);
  for (size_t i = 0; i < candidates.count; ++i) {
    EXPECT_NE(candidates.elements[i], SleepComposerElement::CurrentBookCover);
    EXPECT_NE(candidates.elements[i], SleepComposerElement::BookStats);
    EXPECT_NE(candidates.elements[i], SleepComposerElement::ClippingQuote);
    EXPECT_NE(candidates.elements[i], SleepComposerElement::ToDo);
  }
  EXPECT_EQ(resolveSleepComposerElement(SleepComposerElement::CurrentBookCover, SleepComposerRole::Base,
                                        available, 0),
            SleepComposerElement::NooirDefault);
  EXPECT_EQ(resolveSleepComposerElement(SleepComposerElement::CustomImage, SleepComposerRole::OverlaySecondary,
                                        available, 0),
            SleepComposerElement::None);
}

TEST(SleepComposer, StatisticsSnapshotRequirementIsSharedAcrossAllLayers) {
  const std::array<SleepComposerElement, 3> elements = {
      SleepComposerElement::CurrentBookCover,
      SleepComposerElement::ReadingCalendar,
      SleepComposerElement::ReadingSummary,
  };
  EXPECT_TRUE(sleepComposerNeedsStatisticsSnapshot(elements.data(), elements.size()));
  const std::array<SleepComposerElement, 3> noStats = {
      SleepComposerElement::CurrentBookCover,
      SleepComposerElement::BookStats,
      SleepComposerElement::ToDo,
  };
  EXPECT_FALSE(sleepComposerNeedsStatisticsSnapshot(noStats.data(), noStats.size()));
}

TEST(SleepComposer, FixedSlotsResolveOverlayCollisionsDeterministically) {
  const auto primary = chooseSleepComposerOverlaySlot(SleepComposerRole::OverlayPrimary, 600, 800);
  ASSERT_NE(primary, SleepComposerSlot::None);
  const auto primaryRect = sleepComposerSlotRect(SleepComposerRole::OverlayPrimary, primary, 600, 800);
  const auto secondary = chooseSleepComposerOverlaySlot(SleepComposerRole::OverlaySecondary, 600, 800,
                                                        &primaryRect, 1);
  ASSERT_NE(secondary, SleepComposerSlot::None);
  const auto secondaryRect = sleepComposerSlotRect(SleepComposerRole::OverlaySecondary, secondary, 600, 800);
  EXPECT_FALSE(sleepComposerRectsOverlap(primaryRect, secondaryRect));

  const std::array<SleepComposerRect, 2> occupied = {primaryRect, secondaryRect};
  const auto third = sleepComposerSlotRect(SleepComposerRole::OverlaySecondary,
                                           SleepComposerSlot::TopRight, 600, 800);
  EXPECT_FALSE(sleepComposerRectFitsSlot(occupied.data(), occupied.size(), third));
}

TEST(SleepComposer, FitRectPreservesAspectRatioAndCentersWithinBounds) {
  const SleepComposerRect bounds{20, 30, 200, 300};
  const auto fit = sleepComposerFitRect(100, 200, bounds);

  EXPECT_EQ(fit.width, 150);
  EXPECT_EQ(fit.height, 300);
  EXPECT_EQ(fit.x, 45);
  EXPECT_EQ(fit.y, 30);
}

TEST(SleepComposer, FitRectKeepsExtremeAspectRatiosVisible) {
  const SleepComposerRect bounds{0, 0, 480, 800};
  const auto veryWide = sleepComposerFitRect(65535, 1, bounds);
  const auto veryTall = sleepComposerFitRect(1, 65535, bounds);

  EXPECT_EQ(veryWide.height, 1);
  EXPECT_GT(veryWide.width, 0);
  EXPECT_LE(veryWide.width, bounds.width);
  EXPECT_EQ(veryTall.width, 1);
  EXPECT_GT(veryTall.height, 0);
  EXPECT_LE(veryTall.height, bounds.height);
}

TEST(SleepComposer, FitRectRejectsInvalidGeometry) {
  EXPECT_EQ(sleepComposerFitRect(0, 100, {0, 0, 100, 100}).width, 0);
  EXPECT_EQ(sleepComposerFitRect(100, 100, {0, 0, 0, 100}).height, 0);
}

TEST(SleepComposer, ThreeRandomLayersRemainBoundedWithEmptyAvailability) {
  SleepComposerAvailability unavailable;
  unavailable.readingSummary = false;
  const auto base = resolveSleepComposerElement(SleepComposerElement::Random, SleepComposerRole::Base,
                                                unavailable, UINT32_MAX);
  const std::array<SleepComposerElement, 1> one = {base};
  const auto overlayA = resolveSleepComposerElement(SleepComposerElement::Random,
                                                     SleepComposerRole::OverlayPrimary, unavailable, UINT32_MAX,
                                                     one.data(), one.size());
  const std::array<SleepComposerElement, 2> two = {base, overlayA};
  const auto overlayB = resolveSleepComposerElement(SleepComposerElement::Random,
                                                     SleepComposerRole::OverlaySecondary, unavailable, UINT32_MAX,
                                                     two.data(), two.size());
  EXPECT_EQ(base, SleepComposerElement::NooirDefault);
  EXPECT_NE(base, SleepComposerElement::Random);
  EXPECT_NE(overlayA, SleepComposerElement::Random);
  EXPECT_NE(overlayB, SleepComposerElement::Random);
  EXPECT_NE(base, overlayA);
  EXPECT_NE(base, overlayB);
  EXPECT_NE(overlayA, overlayB);
}
