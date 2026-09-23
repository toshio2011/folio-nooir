#include <gtest/gtest.h>

#include "FontSelectionCompatibility.h"

namespace {

TEST(FontSelectionCompatibility, LegacyReaderValueUsesSerifWithoutChangingRawValue) {
  constexpr uint8_t rawLegacy = FontSelectionCompatibility::kReaderLegacyNotoSans;

  EXPECT_EQ(rawLegacy, 1);
  EXPECT_EQ(FontSelectionCompatibility::readerFamilyForRendering(rawLegacy),
            FontSelectionCompatibility::kReaderNotoSerif);
  EXPECT_EQ(FontSelectionCompatibility::readerVisibleIndex(rawLegacy), 0);
}

TEST(FontSelectionCompatibility, ReaderVisibleOptionsPutSerifBeforeSdFamilies) {
  EXPECT_EQ(FontSelectionCompatibility::kVisibleBuiltinFontCount, 1);
  EXPECT_EQ(FontSelectionCompatibility::kVisibleBuiltinFontCount + 0, 1);
  EXPECT_EQ(FontSelectionCompatibility::kVisibleBuiltinFontCount + 1, 2);
}

TEST(FontSelectionCompatibility, LegacyDictionaryValueUsesSerifWithoutChangingRawValue) {
  constexpr uint8_t rawLegacy = FontSelectionCompatibility::kDictionaryLegacyNotoSans;

  EXPECT_EQ(rawLegacy, 2);
  EXPECT_EQ(FontSelectionCompatibility::dictionaryFamilyForRendering(rawLegacy),
            FontSelectionCompatibility::kDictionaryNotoSerif);
  EXPECT_EQ(FontSelectionCompatibility::dictionaryFamilyForUi(rawLegacy),
            FontSelectionCompatibility::kDictionaryNotoSerif);
}

TEST(FontSelectionCompatibility, DictionaryVisibleValuesRemainStable) {
  EXPECT_EQ(FontSelectionCompatibility::dictionaryFamilyForUi(
                FontSelectionCompatibility::kDictionaryUseReader),
            0);
  EXPECT_EQ(FontSelectionCompatibility::dictionaryFamilyForUi(
                FontSelectionCompatibility::kDictionaryNotoSerif),
            1);
  EXPECT_EQ(FontSelectionCompatibility::kVisibleDictionaryFontCount, 2);
}

TEST(FontSelectionCompatibility, InvalidBuiltInValuesDoNotBecomeSdIndexes) {
  EXPECT_EQ(FontSelectionCompatibility::readerVisibleIndex(255), 0);
  EXPECT_EQ(FontSelectionCompatibility::dictionaryFamilyForUi(255),
            FontSelectionCompatibility::kDictionaryUseReader);
}

}  // namespace
