#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "components/themes/BookProgressFormatter.h"

namespace {

constexpr uint8_t kWhole = 0;
constexpr uint8_t kOneDecimal = 1;
constexpr uint8_t kTwoDecimals = 2;

std::string format(const float progress, const uint8_t percentageFormat) {
  char buffer[16]{};
  BookProgressFormatter::format(buffer, sizeof(buffer), progress, percentageFormat);
  return buffer;
}

TEST(StatusProgress, WholeModeUsesOnlyValidFiniteCompletionFor100) {
  EXPECT_EQ(format(0.0f, kWhole), "0%");
  EXPECT_EQ(format(42.25f, kWhole), "42%");
  EXPECT_EQ(format(99.4f, kWhole), "99%");
  EXPECT_EQ(format(99.5f, kWhole), "99%");
  EXPECT_EQ(format(99.6f, kWhole), "99%");
  EXPECT_EQ(format(99.99f, kWhole), "99%");
  EXPECT_EQ(format(100.0f, kWhole), "100%");
  EXPECT_EQ(format(-1.0f, kWhole), "0%");
  EXPECT_EQ(format(101.0f, kWhole), "0%");
  EXPECT_EQ(format(1.0e30f, kWhole), "0%");
  EXPECT_EQ(format(std::numeric_limits<float>::quiet_NaN(), kWhole), "0%");
  EXPECT_EQ(format(std::numeric_limits<float>::infinity(), kWhole), "0%");
  EXPECT_EQ(format(-std::numeric_limits<float>::infinity(), kWhole), "0%");
}

TEST(StatusProgress, OneDecimalModeUsesOnlyValidFiniteCompletionFor100) {
  EXPECT_EQ(format(0.0f, kOneDecimal), "0.0%");
  EXPECT_EQ(format(42.25f, kOneDecimal), "42.3%");
  EXPECT_EQ(format(99.4f, kOneDecimal), "99.4%");
  EXPECT_EQ(format(99.5f, kOneDecimal), "99.5%");
  EXPECT_EQ(format(99.6f, kOneDecimal), "99.6%");
  EXPECT_EQ(format(99.99f, kOneDecimal), "99.9%");
  EXPECT_EQ(format(100.0f, kOneDecimal), "100.0%");
  EXPECT_EQ(format(-1.0f, kOneDecimal), "0.0%");
  EXPECT_EQ(format(101.0f, kOneDecimal), "0.0%");
  EXPECT_EQ(format(1.0e30f, kOneDecimal), "0.0%");
  EXPECT_EQ(format(std::numeric_limits<float>::quiet_NaN(), kOneDecimal), "0.0%");
  EXPECT_EQ(format(std::numeric_limits<float>::infinity(), kOneDecimal), "0.0%");
  EXPECT_EQ(format(-std::numeric_limits<float>::infinity(), kOneDecimal), "0.0%");
}

TEST(StatusProgress, TwoDecimalModeUsesOnlyValidFiniteCompletionFor100) {
  EXPECT_EQ(format(0.0f, kTwoDecimals), "0.00%");
  EXPECT_EQ(format(42.25f, kTwoDecimals), "42.25%");
  EXPECT_EQ(format(99.4f, kTwoDecimals), "99.40%");
  EXPECT_EQ(format(99.5f, kTwoDecimals), "99.50%");
  EXPECT_EQ(format(99.6f, kTwoDecimals), "99.60%");
  EXPECT_EQ(format(99.99f, kTwoDecimals), "99.99%");
  EXPECT_EQ(format(100.0f, kTwoDecimals), "100.00%");
  EXPECT_EQ(format(-1.0f, kTwoDecimals), "0.00%");
  EXPECT_EQ(format(101.0f, kTwoDecimals), "0.00%");
  EXPECT_EQ(format(1.0e30f, kTwoDecimals), "0.00%");
  EXPECT_EQ(format(std::numeric_limits<float>::quiet_NaN(), kTwoDecimals), "0.00%");
  EXPECT_EQ(format(std::numeric_limits<float>::infinity(), kTwoDecimals), "0.00%");
  EXPECT_EQ(format(-std::numeric_limits<float>::infinity(), kTwoDecimals), "0.00%");
}

}  // namespace
