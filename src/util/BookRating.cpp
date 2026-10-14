#include "BookRating.h"

#include <GfxRenderer.h>

#include <algorithm>

namespace BookRating {

namespace {
void drawFittedStars(const GfxRenderer& renderer, const StarRow& row, const uint8_t rating, const int selectedValue) {
  if (row.starSize < 5) return;
  const uint8_t boundedRating = std::min<uint8_t>(rating, kMaximum);
  const auto outline = starOutline(row.starSize);
  for (int star = 0; star < kMaximum; ++star) {
    const int starX = row.x + star * (row.starSize + row.spacing);
    if (star < boundedRating) {
      int fillX[10];
      int fillY[10];
      for (int point = 0; point < 10; ++point) {
        fillX[point] = starX + outline[point].x;
        fillY[point] = row.y + outline[point].y;
      }
      renderer.fillPolygon(fillX, fillY, 10, true);
    } else {
      for (int point = 0; point < 10; ++point) {
        const int next = (point + 1) % 10;
        renderer.drawLine(starX + outline[point].x, row.y + outline[point].y, starX + outline[next].x,
                          row.y + outline[next].y);
      }
    }
    if (selectedValue >= 0 && (star == selectedValue - 1 || (selectedValue == 0 && star == 0))) {
      renderer.drawRect(starX, row.y, row.starSize, row.starSize);
    }
  }
}
}  // namespace

void drawStars(const GfxRenderer& renderer, const int x, const int y, const uint8_t rating, const int starSize,
               const int spacing, const int selectedValue) {
  const StarRow row = fitStarRow(renderer.getScreenWidth(), renderer.getScreenHeight(), x, y, starSize, spacing);
  drawFittedStars(renderer, row, rating, selectedValue);
}

void drawStarsInBounds(const GfxRenderer& renderer, const StarBounds bounds, const uint8_t rating,
                       const int starSize, const int spacing, const int selectedValue,
                       const bool centerHorizontally) {
  const StarRow row = fitStarRowInBounds(renderer.getScreenWidth(), renderer.getScreenHeight(), bounds,
                                         starSize, spacing, centerHorizontally);
  drawFittedStars(renderer, row, rating, selectedValue);
}

}  // namespace BookRating
