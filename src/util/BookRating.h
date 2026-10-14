#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

class GfxRenderer;

namespace BookRating {

constexpr uint8_t kMaximum = 5;

struct StarRow {
  int x = 0;
  int y = 0;
  int starSize = 0;
  int spacing = 0;
  int width = 0;
};

struct StarPoint {
  int x = 0;
  int y = 0;
};

inline std::array<StarPoint, 10> starOutline(const int starSize) {
  return {{{starSize / 2, 0},
           {starSize * 62 / 100, starSize * 34 / 100},
           {starSize - 1, starSize * 38 / 100},
           {starSize * 69 / 100, starSize * 59 / 100},
           {starSize * 81 / 100, starSize - 1},
           {starSize / 2, starSize * 74 / 100},
           {starSize * 19 / 100, starSize - 1},
           {starSize * 31 / 100, starSize * 59 / 100},
           {0, starSize * 38 / 100},
           {starSize * 38 / 100, starSize * 34 / 100}}};
}

// Fits the full five-star row, including its selection border, inside the
// renderer's logical framebuffer. Returned star coordinates use inclusive
// outer edges from x/y through x + starSize - 1 / y + starSize - 1.
inline StarRow fitStarRow(const int screenWidth, const int screenHeight, const int x, const int y,
                          const int requestedStarSize, const int requestedSpacing) {
  if (requestedStarSize < 5 || screenWidth < kMaximum * 5 || screenHeight < 5) return {};

  const int requestedSpacingBounded = std::clamp(requestedSpacing, 0, (screenWidth - kMaximum * 5) / (kMaximum - 1));
  const int maxSizeForWidth = (screenWidth - (kMaximum - 1) * requestedSpacingBounded) / kMaximum;
  const int starSize = std::min({requestedStarSize, screenHeight, maxSizeForWidth});
  if (starSize < 5) return {};

  const int spacing = std::min(requestedSpacingBounded,
                               (screenWidth - kMaximum * starSize) / (kMaximum - 1));
  const int rowWidth = kMaximum * starSize + (kMaximum - 1) * spacing;
  return {std::clamp(x, 0, screenWidth - rowWidth), std::clamp(y, 0, screenHeight - starSize), starSize, spacing,
          rowWidth};
}

inline StarRow centeredStarRow(const int screenWidth, const int screenHeight, const int requestedStarSize,
                              const int requestedSpacing) {
  const StarRow fitted = fitStarRow(screenWidth, screenHeight, 0, 0, requestedStarSize, requestedSpacing);
  if (fitted.starSize < 5) return fitted;
  return fitStarRow(screenWidth, screenHeight, (screenWidth - fitted.width) / 2,
                    (screenHeight - fitted.starSize) / 2, fitted.starSize, fitted.spacing);
}

// Draws five small outline or filled stars using renderer primitives only.
void drawStars(const GfxRenderer& renderer, int x, int y, uint8_t rating, int starSize = 14, int spacing = 4,
               int selectedValue = -1);

}  // namespace BookRating
