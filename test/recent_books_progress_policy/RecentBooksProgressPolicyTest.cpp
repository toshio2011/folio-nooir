#include <gtest/gtest.h>

#include "util/RecentBooksProgressPolicy.h"

TEST(RecentBooksProgressPolicy, FallsBackToRecentBooksWhenStateIsAbsent) {
  const auto value = RecentBooksProgressPolicy::resolve(42, false, 0, false);
  EXPECT_EQ(value.percent, 42);
  EXPECT_FALSE(value.finished);
}

TEST(RecentBooksProgressPolicy, PreservesLegacyFinishedRecentBooksWhenStateIsAbsent) {
  const auto value = RecentBooksProgressPolicy::resolve(100, false, 0, false);
  EXPECT_EQ(value.percent, 100);
  EXPECT_TRUE(value.finished);
}

TEST(RecentBooksProgressPolicy, FinishedStateCorrectsStaleZeroProgressImmediately) {
  const auto value = RecentBooksProgressPolicy::resolve(0, true, 0, true);
  EXPECT_EQ(value.percent, 100);
  EXPECT_TRUE(value.finished);
}

TEST(RecentBooksProgressPolicy, MarkUnfinishedOverridesStaleFinishedRecentBooks) {
  const auto value = RecentBooksProgressPolicy::resolve(100, true, 99, false);
  EXPECT_EQ(value.percent, 99);
  EXPECT_FALSE(value.finished);
}

TEST(RecentBooksProgressPolicy, ResetOverridesStaleFinishedRecentBooks) {
  const auto value = RecentBooksProgressPolicy::resolve(100, true, 0, false);
  EXPECT_EQ(value.percent, 0);
  EXPECT_FALSE(value.finished);
}

TEST(RecentBooksProgressPolicy, ExistingStateWinsInsteadOfTakingHistoricalMaximum) {
  const auto value = RecentBooksProgressPolicy::resolve(78, true, 31, false);
  EXPECT_EQ(value.percent, 31);
  EXPECT_FALSE(value.finished);
}

TEST(RecentBooksProgressPolicy, NonFinishedStateCannotDisplayOneHundredPercent) {
  const auto value = RecentBooksProgressPolicy::resolve(100, true, 100, false);
  EXPECT_EQ(value.percent, 99);
  EXPECT_FALSE(value.finished);
}

TEST(RecentBooksProgressPolicy, BoundsLegacyPercentage) {
  const auto value = RecentBooksProgressPolicy::resolve(255, false, 0, false);
  EXPECT_EQ(value.percent, 100);
  EXPECT_TRUE(value.finished);
}
