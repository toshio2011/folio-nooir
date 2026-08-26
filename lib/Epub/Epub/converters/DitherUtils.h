#pragma once

#include <stdint.h>

#include <algorithm>

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

// CBZ manga pages benefit from less high-frequency Bayer texture after area
// reduction. Keep four grayscale levels, but stabilize the near-black/near-
// white endpoints and use a smaller ordered-dither spread for midtones.
inline uint8_t applyCbzDither4Level(uint8_t gray, int x, int y) {
  if (gray <= 24) return 0;
  if (gray >= 232) return 3;

  const int bayer = bayer4x4[(y >> 1) & 3][(x >> 1) & 3];
  int adjusted = static_cast<int>(gray) + (bayer - 8) * 2;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  if (adjusted < 50) return 0;
  if (adjusted < 120) return 1;
  if (adjusted < 200) return 2;
  return 3;
}

// Direct quantization used by CBZ's no-dither quality path.  The thresholds
// are intentionally CBZ-only: previews of the supplied manga showed that an
// even 0/85/170/255 split leaves too many screentone pixels in the two gray
// levels.  Biasing the light endpoint toward white keeps speech bubbles and
// page backgrounds clean while retaining a dark level for ink and line art.
// Keep the legacy floor mapping for all other callers so EPUB/XTC/TXT image
// behavior is unchanged.
inline uint8_t quantizeCbzManga4Level(const uint8_t gray) {
  constexpr uint8_t DARK_THRESHOLD = 64;
  constexpr uint8_t LIGHT_THRESHOLD = 144;
  constexpr uint8_t WHITE_THRESHOLD = 192;
  if (gray < DARK_THRESHOLD) return 0;
  if (gray < LIGHT_THRESHOLD) return 1;
  if (gray < WHITE_THRESHOLD) return 2;
  return 3;
}

inline uint8_t quantizeDirect4Level(const uint8_t gray, const bool cbzQualityMode,
                                    const bool cbzBwDiagnostic = false) {
  if (cbzBwDiagnostic) return gray < 128 ? 0 : 3;
  if (!cbzQualityMode) return static_cast<uint8_t>(gray / 85u);
  return quantizeCbzManga4Level(gray);
}
