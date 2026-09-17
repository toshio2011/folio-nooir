// Presentation-form Arabic is already shaped by MiniBidi. EpdFont's GSUB
// ligature pass must not collapse those codepoints a second time.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "EpdFont.h"
#include "EpdFontData.h"
#include "Utf8.h"

namespace {

const EpdLigaturePair kPairs[] = {
    {(0x0066u << 16) | 0x0066u, 0xFB00u},  // Latin ff remains supported.
    {(0x0627u << 16) | 0x0653u, 0x0622u},  // Base Arabic composition remains supported.
    {(0xFEDFu << 16) | 0xFE8Eu, 0xFEFBu},  // Shaped Lam-Alef must be suppressed.
};

EpdFont makeFont(EpdFontData& data) {
  std::memset(&data, 0, sizeof(data));
  data.ligaturePairs = kPairs;
  data.ligaturePairCount = sizeof(kPairs) / sizeof(kPairs[0]);
  return EpdFont(&data);
}

}  // namespace

TEST(LigatureGuard, KeepsLatinLigature) {
  EpdFontData data;
  const EpdFont font = makeFont(data);
  EXPECT_EQ(font.getLigature(0x0066, 0x0066), 0xFB00u);
}

TEST(LigatureGuard, SuppressesArabicPresentationFormLigature) {
  EpdFontData data;
  const EpdFont font = makeFont(data);
  EXPECT_EQ(font.getLigature(0xFEDF, 0xFE8E), 0u);
}

TEST(LigatureGuard, KeepsBaseArabicComposition) {
  EpdFontData data;
  const EpdFont font = makeFont(data);
  EXPECT_EQ(font.getLigature(0x0627, 0x0653), 0x0622u);
}

TEST(LigatureGuard, DoesNotCollapseAlreadyShapedPair) {
  EpdFontData data;
  const EpdFont font = makeFont(data);

  std::string next;
  utf8AppendCodepoint(0xFE8E, next);
  const char* cursor = next.c_str();
  EXPECT_EQ(font.applyLigatures(0xFEDF, cursor), 0xFEDFu);
}
