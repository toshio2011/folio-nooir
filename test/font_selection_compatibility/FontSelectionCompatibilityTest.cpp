#include <gtest/gtest.h>

#include <algorithm>

#include "EpdFontFamily.h"
#include "FontSelectionCompatibility.h"
#include "InterfaceFontSelection.h"
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

TEST(FontSelectionCompatibility, InterfaceFamiliesRequireEveryExactUiSize) {
  SdCardFontFamilyInfo family;
  family.files = {{"/fonts/Test/Test_8.cpfont", 8, 0},
                  {"/fonts/Test/Test_10.cpfont", 10, 0},
                  {"/fonts/Test/Test_12.cpfont", 12, 0},
                  {"/fonts/Test/Test_14.cpfont", 14, 0}};
  EXPECT_TRUE(family.hasInterfaceSizes());

  const uint8_t missingSizes[] = {8, 10, 12};
  for (const uint8_t missingSize : missingSizes) {
    auto incomplete = family;
    incomplete.files.erase(std::remove_if(incomplete.files.begin(), incomplete.files.end(),
                                          [missingSize](const SdCardFontFileInfo& file) {
                                            return file.pointSize == missingSize;
                                          }),
                           incomplete.files.end());
    EXPECT_FALSE(incomplete.hasInterfaceSizes()) << "missing " << static_cast<int>(missingSize) << " pt";
  }
}

TEST(FontSelectionCompatibility, InterfaceFontsStayUnloadedDuringHeavyModes) {
  EXPECT_TRUE(InterfaceFontSelection::mayLoadUiFontsNow(false, false, false, false));
  EXPECT_FALSE(InterfaceFontSelection::mayLoadUiFontsNow(true, false, false, false));
  EXPECT_FALSE(InterfaceFontSelection::mayLoadUiFontsNow(false, true, false, false));
  EXPECT_FALSE(InterfaceFontSelection::mayLoadUiFontsNow(false, false, true, false));
  EXPECT_FALSE(InterfaceFontSelection::mayLoadUiFontsNow(false, false, false, true));
}

TEST(FontSelectionCompatibility, CurrentReleaseStaysOnBuiltinUiFontsAndKeepsSelectionGateClosed) {
  constexpr bool familySelected = true;
  EXPECT_FALSE(InterfaceFontSelection::kCustomUiFontsEnabledForCurrentRelease);
  EXPECT_FALSE(InterfaceFontSelection::mayApplyCustomUiFont(
      InterfaceFontSelection::kCustomUiFontsEnabledForCurrentRelease, false));
  EXPECT_FALSE(InterfaceFontSelection::mayApplyCustomUiFont(
      InterfaceFontSelection::kCustomUiFontsEnabledForCurrentRelease, familySelected));
}

TEST(FontSelectionCompatibility, InterfaceMiniPackLoadsOnlyRequiredRoles) {
  EXPECT_EQ(InterfaceFontSelection::styleMaskForPointSize(8), InterfaceFontSelection::kRegularStyleMask);
  EXPECT_EQ(InterfaceFontSelection::styleMaskForPointSize(10),
            InterfaceFontSelection::kRegularStyleMask | InterfaceFontSelection::kBoldStyleMask);
  EXPECT_EQ(InterfaceFontSelection::styleMaskForPointSize(12),
            InterfaceFontSelection::kRegularStyleMask | InterfaceFontSelection::kBoldStyleMask);
  EXPECT_EQ(InterfaceFontSelection::styleMaskForPointSize(14), 0);

  const uint8_t ui8 = InterfaceFontSelection::styleMaskForPointSize(8);
  const uint8_t ui10 = InterfaceFontSelection::styleMaskForPointSize(10);
  const uint8_t ui12 = InterfaceFontSelection::styleMaskForPointSize(12);
  uint8_t miniPackRoleCount = 0;
  uint8_t fullReaderRoleCount = 0;
  const uint8_t roleMasks[] = {ui8, ui10, ui12};
  for (const uint8_t mask : roleMasks) {
    for (uint8_t style = 0; style < 4; ++style) {
      if (SdCardFontStyleMask::includes(mask, style)) ++miniPackRoleCount;
      if (SdCardFontStyleMask::includes(SdCardFontStyleMask::kAll, style)) ++fullReaderRoleCount;
    }
  }
  EXPECT_EQ(miniPackRoleCount, 5);
  EXPECT_EQ(fullReaderRoleCount, 12);

  // These masks are the exact gate used by SdCardFont::load() before it reads
  // or registers a style's interval table.
  EXPECT_TRUE(SdCardFontStyleMask::includes(ui8, EpdFontFamily::REGULAR));
  EXPECT_FALSE(SdCardFontStyleMask::includes(ui8, EpdFontFamily::BOLD));
  EXPECT_FALSE(SdCardFontStyleMask::includes(ui8, EpdFontFamily::ITALIC));
  EXPECT_FALSE(SdCardFontStyleMask::includes(ui8, EpdFontFamily::BOLD_ITALIC));
  EXPECT_TRUE(SdCardFontStyleMask::includes(ui10, EpdFontFamily::REGULAR));
  EXPECT_TRUE(SdCardFontStyleMask::includes(ui10, EpdFontFamily::BOLD));
  EXPECT_FALSE(SdCardFontStyleMask::includes(ui10, EpdFontFamily::ITALIC));
  EXPECT_FALSE(SdCardFontStyleMask::includes(ui10, EpdFontFamily::BOLD_ITALIC));

  // The Reader loader keeps the full-style default.
  for (uint8_t style = 0; style < 4; ++style) {
    EXPECT_TRUE(SdCardFontStyleMask::includes(SdCardFontStyleMask::kAll, style));
  }
}

TEST(FontSelectionCompatibility, InterfaceMiniPackFallsBackPerStyle) {
  EpdFontData customRegularData{};
  EpdFontData customBoldData{};
  EpdFontData builtinRegularData{};
  EpdFontData builtinBoldData{};
  EpdFontData builtinItalicData{};
  EpdFontData builtinBoldItalicData{};
  EpdFont customRegular(&customRegularData);
  EpdFont customBold(&customBoldData);
  EpdFont builtinRegular(&builtinRegularData);
  EpdFont builtinBold(&builtinBoldData);
  EpdFont builtinItalic(&builtinItalicData);
  EpdFont builtinBoldItalic(&builtinBoldItalicData);

  const EpdFontFamily custom8(&customRegular);
  const EpdFontFamily builtin8(&builtinRegular, &builtinBold, &builtinItalic, &builtinBoldItalic);
  const EpdFontFamily mini8 = EpdFontFamily::withStyleFallbacks(
      custom8, builtin8, InterfaceFontSelection::styleMaskForPointSize(8));
  EXPECT_EQ(mini8.getData(EpdFontFamily::REGULAR), &customRegularData);
  EXPECT_EQ(mini8.getData(EpdFontFamily::BOLD), &builtinBoldData);
  EXPECT_EQ(mini8.getData(EpdFontFamily::ITALIC), &builtinItalicData);
  EXPECT_EQ(mini8.getData(EpdFontFamily::BOLD_ITALIC), &builtinBoldItalicData);

  // A missing custom 10/12 pt Bold face uses built-in Bold instead of
  // silently resolving to the custom Regular face.
  const EpdFontFamily custom10(&customRegular);
  const EpdFontFamily mini10 = EpdFontFamily::withStyleFallbacks(
      custom10, builtin8, InterfaceFontSelection::styleMaskForPointSize(10));
  EXPECT_EQ(mini10.getData(EpdFontFamily::REGULAR), &customRegularData);
  EXPECT_EQ(mini10.getData(EpdFontFamily::BOLD), &builtinBoldData);

  const EpdFontFamily custom10Bold(&customRegular, &customBold);
  const EpdFontFamily mini10Bold = EpdFontFamily::withStyleFallbacks(
      custom10Bold, builtin8, InterfaceFontSelection::styleMaskForPointSize(10));
  EXPECT_EQ(mini10Bold.getData(EpdFontFamily::REGULAR), &customRegularData);
  EXPECT_EQ(mini10Bold.getData(EpdFontFamily::BOLD), &customBoldData);
}

TEST(FontSelectionCompatibility, MissingOrUnpersistableInterfaceFamilyFallsBack) {
  EXPECT_FALSE(InterfaceFontSelection::isCompatibleFamily(nullptr, 32));

  SdCardFontFamilyInfo family;
  family.name = "Installed";
  family.files = {{"/fonts/Installed/Installed_8.cpfont", 8, 0},
                  {"/fonts/Installed/Installed_10.cpfont", 10, 0},
                  {"/fonts/Installed/Installed_12.cpfont", 12, 0}};
  EXPECT_TRUE(InterfaceFontSelection::isCompatibleFamily(&family, 32));
  EXPECT_FALSE(InterfaceFontSelection::isCompatibleFamily(&family, family.name.size()));

  family.name.clear();
  EXPECT_FALSE(InterfaceFontSelection::isCompatibleFamily(&family, 32));
}

}  // namespace
