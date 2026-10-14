#include <gtest/gtest.h>

#include "util/StatisticsDate.h"

TEST(StatisticsDate, AddsAndSubtractsDaysAcrossLeapFebruary) {
  EXPECT_EQ(StatisticsDate::addDays(20240228, 1), 20240229u);
  EXPECT_EQ(StatisticsDate::addDays(20240228, 2), 20240301u);
  EXPECT_EQ(StatisticsDate::addDays(20240301, -1), 20240229u);
}

TEST(StatisticsDate, AddsAndSubtractsDaysAcrossYearBoundaries) {
  EXPECT_EQ(StatisticsDate::addDays(20240101, -1), 20231231u);
  EXPECT_EQ(StatisticsDate::addDays(20231231, 1), 20240101u);
}

TEST(StatisticsDate, RejectsInvalidDatesAndCalendarOverflow) {
  EXPECT_EQ(StatisticsDate::addDays(20240230, 1), 0u);
  EXPECT_EQ(StatisticsDate::addDays(20240229, 0), 20240229u);
  EXPECT_EQ(StatisticsDate::addDays(99991231, 1), 0u);
  EXPECT_EQ(StatisticsDate::addDays(10101, -1), 0u);
}
