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

struct StarBounds {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
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

// Fit the complete five-star row inside a caller-owned content rectangle and
// the actual framebuffer. Horizontal centering is optional so metadata rows
// can reserve a label on the left while picker screens center the stars.
inline StarRow fitStarRowInBounds(const int screenWidth, const int screenHeight, const StarBounds bounds,
                                 const int requestedStarSize, const int requestedSpacing,
                                 const bool centerHorizontally = false) {
  if (screenWidth < kMaximum * 5 || screenHeight < 5 || requestedStarSize < 5) return {};
  const long long requestedRight = static_cast<long long>(bounds.x) + bounds.width;
  const long long requestedBottom = static_cast<long long>(bounds.y) + bounds.height;
  const int left = static_cast<int>(std::clamp<long long>(bounds.x, 0, screenWidth));
  const int top = static_cast<int>(std::clamp<long long>(bounds.y, 0, screenHeight));
  const int right = static_cast<int>(std::clamp<long long>(requestedRight, 0, screenWidth));
  const int bottom = static_cast<int>(std::clamp<long long>(requestedBottom, 0, screenHeight));
  const int availableWidth = right - left;
  const int availableHeight = bottom - top;
  if (availableWidth < kMaximum * 5 || availableHeight < 5) return {};

  const int maxSpacing = (availableWidth - kMaximum * 5) / (kMaximum - 1);
  const int spacing = std::clamp(requestedSpacing, 0, maxSpacing);
  const int starSize = std::min({requestedStarSize, availableHeight,
                                 (availableWidth - (kMaximum - 1) * spacing) / kMaximum});
  if (starSize < 5) return {};
  const int rowWidth = kMaximum * starSize + (kMaximum - 1) * spacing;
  const int x = centerHorizontally ? left + (availableWidth - rowWidth) / 2 : left;
  const int y = top + (availableHeight - starSize) / 2;
  return {x, y, starSize, spacing, rowWidth};
}

inline StarRow centeredStarRowInBounds(const int screenWidth, const int screenHeight, const StarBounds bounds,
                                       const int requestedStarSize, const int requestedSpacing) {
  return fitStarRowInBounds(screenWidth, screenHeight, bounds, requestedStarSize, requestedSpacing, true);
}

// Draws five small outline or filled stars using renderer primitives only.
void drawStars(const GfxRenderer& renderer, int x, int y, uint8_t rating, int starSize = 14, int spacing = 4,
               int selectedValue = -1);
void drawStarsInBounds(const GfxRenderer& renderer, StarBounds bounds, uint8_t rating, int starSize = 14,
                       int spacing = 4, int selectedValue = -1, bool centerHorizontally = false);

}  // namespace BookRating
