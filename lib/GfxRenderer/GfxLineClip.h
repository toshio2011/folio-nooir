#pragma once

#include <algorithm>
#include <cstdint>

namespace gfx {

// Clip a logical line to the inclusive screen rectangle. Endpoints that are
// already inside the rectangle are returned unchanged so the existing
// Bresenham output remains identical for normal lines.
inline bool clipLineToLogicalScreen(int& x1, int& y1, int& x2, int& y2, const int screenWidth,
                                    const int screenHeight) {
  if (screenWidth <= 0 || screenHeight <= 0) return false;

  const int maxX = screenWidth - 1;
  const int maxY = screenHeight - 1;

  if (x1 >= 0 && x1 <= maxX && y1 >= 0 && y1 <= maxY && x2 >= 0 && x2 <= maxX && y2 >= 0 && y2 <= maxY) {
    return true;
  }

  // Horizontal and vertical lines are clipped directly. Besides avoiding
  // division, this preserves the exact endpoint semantics of drawLine().
  if (y1 == y2) {
    if (y1 < 0 || y1 > maxY || (x1 < 0 && x2 < 0) || (x1 > maxX && x2 > maxX)) return false;
    x1 = std::clamp(x1, 0, maxX);
    x2 = std::clamp(x2, 0, maxX);
    return true;
  }
  if (x1 == x2) {
    if (x1 < 0 || x1 > maxX || (y1 < 0 && y2 < 0) || (y1 > maxY && y2 > maxY)) return false;
    y1 = std::clamp(y1, 0, maxY);
    y2 = std::clamp(y2, 0, maxY);
    return true;
  }

  constexpr uint8_t LEFT = 1;
  constexpr uint8_t RIGHT = 2;
  constexpr uint8_t TOP = 4;
  constexpr uint8_t BOTTOM = 8;

  const auto outCode = [=](const int x, const int y) {
    uint8_t code = 0;
    if (x < 0) code |= LEFT;
    if (x > maxX) code |= RIGHT;
    if (y < 0) code |= TOP;
    if (y > maxY) code |= BOTTOM;
    return code;
  };

  uint8_t code1 = outCode(x1, y1);
  uint8_t code2 = outCode(x2, y2);

  while (true) {
    if ((code1 | code2) == 0) return true;
    if ((code1 & code2) != 0) return false;

    const uint8_t outside = code1 != 0 ? code1 : code2;
    int clippedX = 0;
    int clippedY = 0;
    const int64_t dx = static_cast<int64_t>(x2) - x1;
    const int64_t dy = static_cast<int64_t>(y2) - y1;

    if ((outside & TOP) != 0) {
      clippedY = 0;
      clippedX = static_cast<int>(static_cast<int64_t>(x1) + dx * (0 - static_cast<int64_t>(y1)) / dy);
    } else if ((outside & BOTTOM) != 0) {
      clippedY = maxY;
      clippedX = static_cast<int>(static_cast<int64_t>(x1) + dx * (maxY - static_cast<int64_t>(y1)) / dy);
    } else if ((outside & RIGHT) != 0) {
      clippedX = maxX;
      clippedY = static_cast<int>(static_cast<int64_t>(y1) + dy * (maxX - static_cast<int64_t>(x1)) / dx);
    } else {
      clippedX = 0;
      clippedY = static_cast<int>(static_cast<int64_t>(y1) + dy * (0 - static_cast<int64_t>(x1)) / dx);
    }

    if (outside == code1) {
      x1 = clippedX;
      y1 = clippedY;
      code1 = outCode(x1, y1);
    } else {
      x2 = clippedX;
      y2 = clippedY;
      code2 = outCode(x2, y2);
    }
  }
}

}  // namespace gfx
