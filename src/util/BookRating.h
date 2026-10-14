#pragma once

#include <cstdint>

class GfxRenderer;

namespace BookRating {

constexpr uint8_t kMaximum = 5;

// Draws five small outline or filled stars using renderer primitives only.
void drawStars(const GfxRenderer& renderer, int x, int y, uint8_t rating, int starSize = 14, int spacing = 4,
               int selectedValue = -1);

}  // namespace BookRating
