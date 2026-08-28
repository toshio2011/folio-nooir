#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "BidiUtils.h"
#include "EpdFont.h"
#include "Utf8.h"
#include "builtinFonts/arabic_14_regular.h"

namespace {

std::string encode(std::initializer_list<uint32_t> codepoints) {
  std::string text;
  for (const uint32_t cp : codepoints) utf8AppendCodepoint(cp, text);
  return text;
}

std::vector<uint32_t> decode(const std::string& text) {
  std::vector<uint32_t> codepoints;
  auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp) break;
    codepoints.push_back(cp);
  }
  return codepoints;
}

// A small font with equal 8px advances/widths makes the metric assertions
// independent of a particular built-in font while still exercising EpdFont's
// real bounds and mark-overlay path.
const EpdGlyph kGlyphs[] = {
    {8, 10, 128, 0, 10, 0, 0},  // U+0061 Latin a
    {3, 2, 0, 0, 12, 0, 0},      // U+0301 acute
    {3, 2, 0, 0, 12, 0, 0},      // U+05B0 Hebrew sheva
    {8, 10, 128, 0, 10, 0, 0},    // U+05D0 Hebrew alef
    {8, 10, 128, 0, 10, 0, 0},    // U+0627 Arabic alef
    {8, 10, 128, 0, 10, 0, 0},    // U+0628 Arabic beh
    {8, 10, 128, 0, 10, 0, 0},    // U+0644 Arabic lam
    {3, 2, 0, 0, 12, 0, 0},      // U+064E Arabic fatha
    {8, 10, 128, 0, 10, 0, 0},    // U+0661 Arabic-Indic digit one
    {3, 2, 0, 0, 12, 0, 0},      // U+0898 Arabic Extended-B mark
    {3, 2, 0, 0, 12, 0, 0},      // U+08CA Arabic Extended-A mark
};

const EpdUnicodeInterval kIntervals[] = {
    {0x0061, 0x0061, 0},
    {0x0301, 0x0301, 1},
    {0x05B0, 0x05B0, 2},
    {0x05D0, 0x05D0, 3},
    {0x0627, 0x0627, 4},
    {0x0628, 0x0628, 5},
    {0x0644, 0x0644, 6},
    {0x064E, 0x064E, 7},
    {0x0661, 0x0661, 8},
    {0x0898, 0x0898, 9},
    {0x08CA, 0x08CA, 10},
};

const EpdFontData kFontData = {
    .bitmap = nullptr,
    .glyph = kGlyphs,
    .intervals = kIntervals,
    .intervalCount = sizeof(kIntervals) / sizeof(kIntervals[0]),
    .advanceY = 12,
    .ascender = 10,
    .descender = 0,
    .is2Bit = false,
    .groups = nullptr,
    .groupCount = 0,
    .glyphToGroup = nullptr,
    .kernLeftClasses = nullptr,
    .kernRightClasses = nullptr,
    .kernMatrix = nullptr,
    .kernLeftEntryCount = 0,
    .kernRightEntryCount = 0,
    .kernLeftClassCount = 0,
    .kernRightClassCount = 0,
    .ligaturePairs = nullptr,
    .ligaturePairCount = 0,
    .glyphMissHandler = nullptr,
    .glyphMissCtx = nullptr,
};

EpdFont& testFont() {
  static EpdFont font(&kFontData);
  return font;
}

int textWidth(const std::string& text) {
  int width = 0;
  int height = 0;
  testFont().getTextDimensions(text.c_str(), &width, &height);
  return width;
}

int textHeight(const std::string& text) {
  int width = 0;
  int height = 0;
  testFont().getTextDimensions(text.c_str(), &width, &height);
  return height;
}

int drawnAdvance(const std::string& text) {
  int advance = 0;
  auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!utf8IsTextMark(cp)) {
      const EpdGlyph* glyph = testFont().getGlyph(cp);
      if (glyph) advance += static_cast<int>((glyph->advanceX + 8) / 16);
    }
  }
  return advance;
}

}  // namespace

TEST(ArabicFoundation, TransparentMarksCoverArabicHebrewAndLatinPaths) {
  EXPECT_TRUE(utf8IsTextMark(0x064E));
  EXPECT_TRUE(utf8IsTextMark(0x0651));
  EXPECT_TRUE(utf8IsTextMark(0x06D6));
  EXPECT_TRUE(utf8IsTextMark(0x0898));
  EXPECT_TRUE(utf8IsTextMark(0x08CA));
  EXPECT_TRUE(utf8IsTextMark(0x05B0));
  EXPECT_TRUE(utf8IsTextMark(0x0301));
  EXPECT_FALSE(utf8IsTextMark(0x0628));
  EXPECT_FALSE(utf8IsTextMark(0x0661));
  EXPECT_FALSE(utf8IsTextMark(0x060C));
  EXPECT_TRUE(utf8IsArabicCodepoint(0x0627));
  EXPECT_TRUE(utf8IsArabicCodepoint(0x064E));
  EXPECT_TRUE(utf8IsArabicCodepoint(0x0661));
  EXPECT_TRUE(utf8IsArabicCodepoint(0xFE8D));
  EXPECT_FALSE(utf8IsArabicCodepoint(0x05B0));
}

TEST(ArabicFoundation, GeneratedReaderFallbackCoversShapedArabicAndKeepsReaderMetrics) {
  EpdFont fallback(&arabic_14_regular);
  EXPECT_TRUE(fallback.hasCodepoint(0x0627));  // base alef
  EXPECT_TRUE(fallback.hasCodepoint(0x064E));  // fatha
  EXPECT_TRUE(fallback.hasCodepoint(0x06D6));  // Quranic annotation mark
  EXPECT_TRUE(fallback.hasCodepoint(0x0661));  // Arabic-Indic digit
  EXPECT_TRUE(fallback.hasCodepoint(0xFE8D));  // shaped isolated alef
  EXPECT_TRUE(fallback.hasCodepoint(0xFEE0));  // shaped medial lam
  EXPECT_TRUE(fallback.hasCodepoint(REPLACEMENT_GLYPH));
  EXPECT_FALSE(fallback.hasCodepoint('A'));
  EXPECT_EQ(arabic_14_regular.advanceY, 40);  // Noto Sans 14 reader metrics
  EXPECT_EQ(arabic_14_regular.ascender, 32);
  EXPECT_EQ(arabic_14_regular.descender, -9);
}

TEST(ArabicFoundation, MeasureAndDrawAdvanceAgreeForMarksAndMixedText) {
  const std::string arabic = encode({0x0628});
  const std::string vocalized = encode({0x0628, 0x0651, 0x064E});
  const std::string lamAlefWithMark = encode({0x0644, 0x064E, 0x0627});
  const std::string hebrew = encode({0x05D0, 0x05B0});
  const std::string latin = encode({'a', 0x0301});

  EXPECT_EQ(textWidth(vocalized), textWidth(arabic));
  EXPECT_GE(textHeight(vocalized), textHeight(arabic));
  EXPECT_EQ(textWidth(vocalized), drawnAdvance(vocalized));
  EXPECT_EQ(textWidth(lamAlefWithMark), drawnAdvance(lamAlefWithMark));
  EXPECT_EQ(textWidth(hebrew), drawnAdvance(hebrew));
  EXPECT_EQ(textWidth(latin), drawnAdvance(latin));
  EXPECT_EQ(textWidth(encode({0x0628, 0x0661})), 16);
}

TEST(ArabicFoundation, ScriptRunsAreBoundedAndAttachNeutrals) {
  const std::string mixed = encode({'a', ' ', 0x0628, 0x060C, ' ', 0x0661, ' ', 0x05D0});
  Utf8ScriptRun runs[UTF8_MAX_SCRIPT_RUNS]{};
  uint8_t count = 0;
  bool overflowed = false;
  ASSERT_TRUE(utf8ClassifyScriptRuns(mixed.c_str(), runs, UTF8_MAX_SCRIPT_RUNS, &count, &overflowed));
  ASSERT_FALSE(overflowed);
  ASSERT_EQ(count, 3);
  EXPECT_EQ(runs[0].script, Utf8Script::Latin);
  EXPECT_EQ(runs[1].script, Utf8Script::Arabic);
  EXPECT_EQ(runs[2].script, Utf8Script::Hebrew);
  EXPECT_EQ(runs[0].byteStart, 0u);
  EXPECT_EQ(runs[2].byteStart + runs[2].byteLength, mixed.size());
}

TEST(ArabicFoundation, RtlPreflightCoversExtendedArabicWithoutTreatingLatinAsRtl) {
  EXPECT_FALSE(utf8ContainsRtlScript("Latin e\xCC\x81"));
  EXPECT_TRUE(utf8ContainsRtlScript(encode({0x0750}).c_str()));
  EXPECT_TRUE(utf8ContainsRtlScript(encode({0x08A0}).c_str()));
  EXPECT_TRUE(utf8ContainsRtlScript(encode({0xFB50}).c_str()));
  EXPECT_TRUE(utf8ContainsRtlScript(encode({0x10EC0}).c_str()));
}

TEST(ArabicFoundation, LongMixedLineUsesBoundedBidiSegmentation) {
  std::string logical;
  for (int i = 0; i < 180; ++i) {
    utf8AppendCodepoint((i % 2) ? 'a' : 0x0628, logical);
    if ((i % 9) == 8) logical.push_back(' ');
  }

  std::string visual;
  ASSERT_TRUE(BidiUtils::applyBidiVisual(logical.c_str(), visual));
  EXPECT_FALSE(visual.empty());
  EXPECT_EQ(decode(visual).size(), decode(logical).size());
}

TEST(ArabicFoundation, ScriptRunOverflowIsExplicit) {
  std::string text;
  for (int i = 0; i < 20; ++i) {
    utf8AppendCodepoint((i % 2) ? 0x05D0 : 'a', text);
    text.push_back(' ');
  }

  Utf8ScriptRun runs[4]{};
  uint8_t count = 0;
  bool overflowed = false;
  ASSERT_TRUE(utf8ClassifyScriptRuns(text.c_str(), runs, 4, &count, &overflowed));
  EXPECT_TRUE(overflowed);
  EXPECT_LE(count, 4);
  ASSERT_GT(count, 0);
  EXPECT_EQ(runs[count - 1].script, Utf8Script::Mixed);
}
