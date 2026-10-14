#include <gtest/gtest.h>

#include <limits>

#include "util/BookRating.h"

namespace {
void expectRowInsideFramebuffer(const BookRating::StarRow& row, const int width, const int height) {
  ASSERT_GE(row.starSize, 5);
  ASSERT_GE(row.spacing, 0);
  EXPECT_GE(row.x, 0);
  EXPECT_GE(row.y, 0);
  EXPECT_LE(row.x + row.width, width);
  EXPECT_LE(row.y + row.starSize, height);

  for (int star = 0; star < BookRating::kMaximum; ++star) {
    const int starX = row.x + star * (row.starSize + row.spacing);
    EXPECT_GE(starX, 0);
    EXPECT_GE(row.y, 0);
    EXPECT_LE(starX + row.starSize, width);
    EXPECT_LE(row.y + row.starSize, height);
    for (const auto& point : BookRating::starOutline(row.starSize)) {
      EXPECT_GE(starX + point.x, 0);
      EXPECT_GE(row.y + point.y, 0);
      EXPECT_LT(starX + point.x, width);
      EXPECT_LT(row.y + point.y, height);
    }
  }
}
}  // namespace

TEST(BookRatingGeometry, CentersAndFitsPortraitAndLandscapeRows) {
  const auto portrait = BookRating::centeredStarRow(480, 800, 28, 9);
  const auto landscape = BookRating::centeredStarRow(800, 480, 28, 9);

  expectRowInsideFramebuffer(portrait, 480, 800);
  expectRowInsideFramebuffer(landscape, 800, 480);
  EXPECT_EQ(portrait.x, (480 - portrait.width) / 2);
  EXPECT_EQ(landscape.x, (800 - landscape.width) / 2);
}

TEST(BookRatingGeometry, ClampsRequestedOriginAndSizeToFramebuffer) {
  const auto row = BookRating::fitStarRow(160, 80, -100, 1000, 100, 20);

  expectRowInsideFramebuffer(row, 160, 80);
  EXPECT_EQ(row.x, 0);
  EXPECT_EQ(row.y + row.starSize, 80);
}

TEST(BookRatingGeometry, ClampsExtremeSpacingWithoutOverflow) {
  const auto row = BookRating::fitStarRow(160, 80, std::numeric_limits<int>::min(),
                                          std::numeric_limits<int>::max(), 100,
                                          std::numeric_limits<int>::max());

  expectRowInsideFramebuffer(row, 160, 80);
  EXPECT_EQ(row.x, 0);
  EXPECT_EQ(row.y + row.starSize, 80);
}

TEST(BookRatingGeometry, ReturnsEmptyWhenFiveMinimumStarsCannotFit) {
  EXPECT_EQ(BookRating::fitStarRow(24, 800, 0, 0, 10, 2).starSize, 0);
  EXPECT_EQ(BookRating::fitStarRow(480, 4, 0, 0, 10, 2).starSize, 0);
  EXPECT_EQ(BookRating::fitStarRow(480, 800, 0, 0, 4, 2).starSize, 0);
}
