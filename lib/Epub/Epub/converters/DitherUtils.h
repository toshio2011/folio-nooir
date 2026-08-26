#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Apply Bayer dithering and quantize to 4 levels (0-3).
//
// The calibrated thresholds leave a little more room for the dark and light
// endpoints than an even 64/128/192 split. That gives EPUB illustrations
// better mid-tone separation on the Xteink waveforms without adding another
// pass, buffer, or per-image allocation. Keep this as a stateless fast path:
// EPUB pages are often rendered more than once (BW + grayscale planes).
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 4;  // Small +/-32 spread; less visible checkerboard

  int adjusted = gray + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 50) return 0;
  if (adjusted < 120) return 1;
  if (adjusted < 200) return 2;
  return 3;
}
