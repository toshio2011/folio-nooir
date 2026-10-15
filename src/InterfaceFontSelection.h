#pragma once

#include <SdCardFontRegistry.h>
#include <SdCardFontStyleMask.h>

#include <cstddef>

namespace InterfaceFontSelection {

// Interface-font faces are deliberately narrower than reader font faces. The
// 8 pt role is used for compact labels; 10 pt and 12 pt also need bold titles.
// The cpfont style bits match EpdFontFamily's REGULAR/BOLD/ITALIC/BOLD_ITALIC
// ordering, while keeping this header usable by settings and host tests.
constexpr uint8_t kRegularStyleMask = 1u << 0;
constexpr uint8_t kBoldStyleMask = 1u << 1;
constexpr uint8_t kItalicStyleMask = 1u << 2;
constexpr uint8_t kBoldItalicStyleMask = 1u << 3;
constexpr uint8_t kAllStyleMask = SdCardFontStyleMask::kAll;

constexpr uint8_t styleMaskForPointSize(const uint8_t pointSize) {
  switch (pointSize) {
    case 8:
      return kRegularStyleMask;
    case 10:
    case 12:
      return kRegularStyleMask | kBoldStyleMask;
    default:
      return 0;
  }
}

// A saved choice stays usable only while the family is still installed,
// provides every size-matched UI face, and fits the persisted family field.
// The caller can safely fall back to built-in UI when this predicate fails.
inline bool isCompatibleFamily(const SdCardFontFamilyInfo* family, const size_t selectionCapacity) {
  return family != nullptr && !family->name.empty() && family->name.size() < selectionCapacity &&
         family->hasInterfaceSizes();
}

}  // namespace InterfaceFontSelection
