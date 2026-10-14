#include "BookRating.h"

#include <GfxRenderer.h>

#include <algorithm>

namespace BookRating {

void drawStars(const GfxRenderer& renderer, const int x, const int y, const uint8_t rating, const int starSize,
               const int spacing, const int selectedValue) {
  if (starSize < 5) return;
  const uint8_t boundedRating = std::min<uint8_t>(rating, kMaximum);
  const int outerX[10] = {starSize / 2,
                          starSize * 62 / 100,
                          starSize - 1,
                          starSize * 69 / 100,
                          starSize * 81 / 100,
                          starSize / 2,
                          starSize * 19 / 100,
                          starSize * 31 / 100,
                          0,
                          starSize * 38 / 100};
  const int outerY[10] = {0,
                          starSize * 34 / 100,
                          starSize * 38 / 100,
                          starSize * 59 / 100,
                          starSize - 1,
                          starSize * 74 / 100,
                          starSize - 1,
                          starSize * 59 / 100,
                          starSize * 38 / 100,
                          starSize * 34 / 100};
  for (int star = 0; star < kMaximum; ++star) {
    const int starX = x + star * (starSize + spacing);
    if (star < boundedRating) {
      int fillX[10];
      int fillY[10];
      for (int point = 0; point < 10; ++point) {
        fillX[point] = starX + outerX[point];
        fillY[point] = y + outerY[point];
      }
      renderer.fillPolygon(fillX, fillY, 10, true);
    } else {
      for (int point = 0; point < 10; ++point) {
        const int next = (point + 1) % 10;
        renderer.drawLine(starX + outerX[point], y + outerY[point], starX + outerX[next], y + outerY[next]);
      }
    }
    if (selectedValue >= 0 && (star == selectedValue - 1 || (selectedValue == 0 && star == 0))) {
      renderer.drawRect(starX - 2, y - 2, starSize + 4, starSize + 4);
    }
  }
}

}  // namespace BookRating
