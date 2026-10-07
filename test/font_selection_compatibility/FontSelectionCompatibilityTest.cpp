#include <gtest/gtest.h>

#include "FontSelectionCompatibility.h"
#include "SdCardFontRegistry.h"

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

TEST(FontSelectionCompatibility, InterfaceFamiliesRequireAllUiSizes) {
  SdCardFontFamilyInfo family;
  family.files = {{"/fonts/Test/Test_8.cpfont", 8, 0},
                  {"/fonts/Test/Test_10.cpfont", 10, 0},
                  {"/fonts/Test/Test_12.cpfont", 12, 0}};
  EXPECT_TRUE(family.hasInterfaceSizes());

  family.files.pop_back();
  EXPECT_FALSE(family.hasInterfaceSizes());
}

}  // namespace
