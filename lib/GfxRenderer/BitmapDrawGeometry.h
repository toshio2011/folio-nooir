#pragma once

#include <algorithm>
#include <cmath>

namespace BitmapDrawGeometry {

inline float fitScale(const float sourceWidth, const float sourceHeight, const int maxWidth, const int maxHeight) {
  bool hasBounds = false;
  float scale = 1.0f;
  if (maxWidth > 0 && sourceWidth > 0) {
    scale = static_cast<float>(maxWidth) / sourceWidth;
    hasBounds = true;
  }
  if (maxHeight > 0 && sourceHeight > 0) {
    const float heightScale = static_cast<float>(maxHeight) / sourceHeight;
    scale = hasBounds ? std::min(scale, heightScale) : heightScale;
    hasBounds = true;
  }
  return hasBounds ? scale : 1.0f;
}

inline float boundedScale(const float sourceWidth, const float sourceHeight, const int maxWidth, const int maxHeight,
                          const bool allowUpscale) {
  const float scale = fitScale(sourceWidth, sourceHeight, maxWidth, maxHeight);
  return allowUpscale ? scale : std::min(scale, 1.0f);
}

// Maps one source pixel to the destination interval sampled by nearest-neighbor
// enlargement. Ceil boundaries avoid overlaps and gaps at fractional scales.
inline int pixelStart(const int sourceCoordinate, const float scale) {
  return scale > 1.0f ? static_cast<int>(std::ceil(static_cast<float>(sourceCoordinate) * scale))
                      : static_cast<int>(std::floor(static_cast<float>(sourceCoordinate) * scale));
}

inline int pixelEndExclusive(const int sourceCoordinate, const float scale) {
  return scale > 1.0f ? static_cast<int>(std::ceil(static_cast<float>(sourceCoordinate + 1) * scale))
                      : pixelStart(sourceCoordinate, scale) + 1;
}

}  // namespace BitmapDrawGeometry
