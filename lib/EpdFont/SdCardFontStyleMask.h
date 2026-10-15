#pragma once

#include <cstdint>

namespace SdCardFontStyleMask {

constexpr uint8_t kAll = 0x0F;

constexpr bool includes(const uint8_t mask, const uint8_t style) {
  return style < 4 && (mask & (1u << style)) != 0;
}

}  // namespace SdCardFontStyleMask
