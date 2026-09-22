#pragma once

#include <cstdint>

namespace gfx {

// SD-font advances are 12.4 fixed-point values. Zero means that the compact
// advance table has no usable entry; callers can then use the normal glyph
// metric path, which may load the glyph on demand.
constexpr int32_t selectSpaceAdvanceFP(const uint16_t cachedAdvanceFP, const int32_t fallbackAdvanceFP) {
  return cachedAdvanceFP != 0 ? static_cast<int32_t>(cachedAdvanceFP) : fallbackAdvanceFP;
}

}  // namespace gfx
