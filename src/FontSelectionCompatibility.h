#pragma once

#include <cstdint>

// Embedded Noto Sans 12-18 reader resources are intentionally absent from the
// production image. Their persisted enum values remain legacy data, so this
// layer keeps UI/API/resolution paths compatible without rewriting saved
// preferences.
namespace FontSelectionCompatibility {

constexpr uint8_t kReaderNotoSerif = 0;
constexpr uint8_t kReaderLegacyNotoSans = 1;
constexpr uint8_t kVisibleBuiltinFontCount = 1;
constexpr uint8_t kLegacyBuiltinFontCount = 2;

constexpr uint8_t kDictionaryUseReader = 0;
constexpr uint8_t kDictionaryNotoSerif = 1;
constexpr uint8_t kDictionaryLegacyNotoSans = 2;
constexpr uint8_t kVisibleDictionaryFontCount = 2;

constexpr uint8_t readerFamilyForRendering(const uint8_t family) {
  return family == kReaderLegacyNotoSans ? kReaderNotoSerif : family;
}

// Convert a persisted built-in reader value to a visible built-in option. The
// only visible built-in option is Noto Serif; SD options are indexed separately
// by family name and must never pass through this function.
constexpr uint8_t readerVisibleIndex(const uint8_t family) {
  return family == kReaderNotoSerif || family == kReaderLegacyNotoSans ? kReaderNotoSerif : kReaderNotoSerif;
}

constexpr uint8_t dictionaryFamilyForRendering(const uint8_t family) {
  return family == kDictionaryLegacyNotoSans ? kDictionaryNotoSerif : family;
}

constexpr uint8_t dictionaryFamilyForUi(const uint8_t family) {
  return family == kDictionaryLegacyNotoSans
             ? kDictionaryNotoSerif
             : (family < kVisibleDictionaryFontCount ? family : kDictionaryUseReader);
}

}  // namespace FontSelectionCompatibility
