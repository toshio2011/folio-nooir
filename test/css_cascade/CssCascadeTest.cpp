#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "CssParser.h"

namespace {

std::unique_ptr<CssParser> parserFor(const std::string& css) {
  auto parser = std::make_unique<CssParser>("css_cascade_test");
  HalFile source = HalFile::readBuffer(css);
  EXPECT_TRUE(parser->loadFromStream(source));
  return parser;
}

void resetStorage() { Storage.clear(); }

}  // namespace

TEST(CssCascadeTest, EqualSpecificityIgnoresHtmlClassOrder) {
  const auto parser = parserFor(".a { text-align: left; } .b { text-align: center; }");

  EXPECT_EQ(parser->resolveStyle("div", "a b").textAlign, CssTextAlign::Center);
  EXPECT_EQ(parser->resolveStyle("div", "b a").textAlign, CssTextAlign::Center);
}

TEST(CssCascadeTest, ReversedCssSourceOrderWinsRegardlessOfClassOrder) {
  const auto parser = parserFor(".b { text-align: center; } .a { text-align: left; }");

  EXPECT_EQ(parser->resolveStyle("div", "a b").textAlign, CssTextAlign::Left);
  EXPECT_EQ(parser->resolveStyle("div", "b a").textAlign, CssTextAlign::Left);
}

TEST(CssCascadeTest, ElementClassRulesUseSourceOrderWithinTheirCategory) {
  const auto parser = parserFor("p.a { font-weight: normal; } p.b { font-weight: bold; }");

  EXPECT_EQ(parser->resolveStyle("p", "a b").fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(parser->resolveStyle("p", "b a").fontWeight, CssFontWeight::Bold);
}

TEST(CssCascadeTest, ExistingSpecificityCategoriesRemainStrongerThanSourceOrder) {
  const auto parser = parserFor(
      "#entry { text-align: right; } p { text-align: left; } .entry { text-align: center; } "
      "p.entry { text-align: justify; } p#entry { text-align: right; }");

  EXPECT_EQ(parser->resolveStyle("p", "entry", "entry").textAlign, CssTextAlign::Right);
}

TEST(CssCascadeTest, RepeatedSelectorRetainsPerPropertySourceOrder) {
  const auto parser = parserFor(
      ".a { font-weight: bold; text-align: left; } .a { text-align: center; }");
  const auto style = parser->resolveStyle("div", "a");

  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.textAlign, CssTextAlign::Center);
}

TEST(CssCascadeTest, SelectorListsRemainSupported) {
  const auto parser = parserFor("h1, h2 { font-style: italic; }");

  EXPECT_EQ(parser->resolveStyle("h1", "").fontStyle, CssFontStyle::Italic);
  EXPECT_EQ(parser->resolveStyle("h2", "").fontStyle, CssFontStyle::Italic);
}

TEST(CssCascadeTest, InlineStyleStillOverridesResolvedStylesheetStyle) {
  const auto parser = parserFor(".a { text-align: left; }");
  auto style = parser->resolveStyle("div", "a");
  style.applyOver(CssParser::parseInlineStyle("text-align: right;"));

  EXPECT_EQ(style.textAlign, CssTextAlign::Right);
}

TEST(CssCascadeTest, DisplayNoneStillResolves) {
  const auto parser = parserFor(".hidden { display: none; }");
  const auto style = parser->resolveStyle("div", "hidden");

  EXPECT_EQ(style.display, CssDisplay::None);
}

TEST(CssCascadeTest, CompoundSelectorsRemainOutsideC1) {
  const auto parser = parserFor(".a.b { text-align: center; } p.a.b { font-weight: bold; }");
  const auto style = parser->resolveStyle("p", "a b");

  EXPECT_FALSE(style.hasTextAlign());
  EXPECT_FALSE(style.hasFontWeight());
}

TEST(CssCascadeTest, CacheVersionBumpRejectsVersionTen) {
  resetStorage();
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("CSS", "css_cascade_test/css_rules.cache", file));
  file.write(static_cast<uint8_t>(10));
  file.close();

  CssParser parser("css_cascade_test");
  EXPECT_FALSE(parser.loadFromCache());
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache"));
}

TEST(CssCascadeTest, VersionElevenCacheRoundTripPreservesCascade) {
  resetStorage();
  const std::string css = ".a { font-weight: bold; text-align: left; } .b { text-align: center; }";
  const auto writer = parserFor(css);
  ASSERT_TRUE(writer->saveToCache());

  CssParser reader("css_cascade_test");
  ASSERT_TRUE(reader.loadFromCache());
  EXPECT_EQ(reader.resolveStyle("div", "a b").textAlign, CssTextAlign::Center);
  EXPECT_EQ(reader.resolveStyle("div", "b a").textAlign, CssTextAlign::Center);
  EXPECT_EQ(reader.resolveStyle("div", "a").fontWeight, CssFontWeight::Bold);
}

TEST(CssCascadeTest, CacheVersionIsEleven) { EXPECT_EQ(CssParser::CSS_CACHE_VERSION, 11); }

TEST(CssCascadeTest, CacheCreationPublishesOnlyCompletedFinalFile) {
  resetStorage();
  const auto parser = parserFor(".a { text-align: center; }");

  ASSERT_TRUE(parser->saveToCache());
  EXPECT_TRUE(Storage.exists("css_cascade_test/css_rules.cache"));
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache.tmp"));
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache.bak"));
}

TEST(CssCascadeTest, FailedCandidateWritePreservesExistingFinalFile) {
  resetStorage();
  ASSERT_TRUE(parserFor(".old { text-align: left; }")->saveToCache());
  CssCacheTestHooks::shortWrite = true;

  EXPECT_FALSE(parserFor(".new { text-align: right; }")->saveToCache());
  EXPECT_TRUE(Storage.exists("css_cascade_test/css_rules.cache"));
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache.tmp"));

  CssParser reader("css_cascade_test");
  ASSERT_TRUE(reader.loadFromCache());
  EXPECT_EQ(reader.resolveStyle("div", "old").textAlign, CssTextAlign::Left);
  EXPECT_FALSE(reader.resolveStyle("div", "new").hasTextAlign());
}

TEST(CssCascadeTest, FailedCandidateOpenPreservesExistingFinalFile) {
  resetStorage();
  ASSERT_TRUE(parserFor(".old { text-align: left; }")->saveToCache());
  Storage.failOpenWrite = true;

  EXPECT_FALSE(parserFor(".new { text-align: right; }")->saveToCache());
  CssParser reader("css_cascade_test");
  ASSERT_TRUE(reader.loadFromCache());
  EXPECT_EQ(reader.resolveStyle("div", "old").textAlign, CssTextAlign::Left);
}

TEST(CssCascadeTest, FailedCandidateClosePreservesExistingFinalFile) {
  resetStorage();
  ASSERT_TRUE(parserFor(".old { text-align: left; }")->saveToCache());
  CssCacheTestHooks::failClose = true;

  EXPECT_FALSE(parserFor(".new { text-align: right; }")->saveToCache());
  CssParser reader("css_cascade_test");
  ASSERT_TRUE(reader.loadFromCache());
  EXPECT_EQ(reader.resolveStyle("div", "old").textAlign, CssTextAlign::Left);
}

TEST(CssCascadeTest, FailedPromotionRestoresExistingFinalFile) {
  resetStorage();
  ASSERT_TRUE(parserFor(".old { text-align: left; }")->saveToCache());
  Storage.renameCalls = 0;
  Storage.failRenameAt = 2;  // old final -> backup succeeds; temp -> final fails

  EXPECT_FALSE(parserFor(".new { text-align: right; }")->saveToCache());
  EXPECT_TRUE(Storage.exists("css_cascade_test/css_rules.cache"));
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache.tmp"));
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache.bak"));

  CssParser reader("css_cascade_test");
  ASSERT_TRUE(reader.loadFromCache());
  EXPECT_EQ(reader.resolveStyle("div", "old").textAlign, CssTextAlign::Left);
}

TEST(CssCascadeTest, StaleTempIsRemovedAndStrandedBackupIsRecovered) {
  resetStorage();
  ASSERT_TRUE(parserFor(".old { text-align: left; }")->saveToCache());
  ASSERT_TRUE(Storage.rename("css_cascade_test/css_rules.cache", "css_cascade_test/css_rules.cache.bak"));
  HalFile staleTemp;
  ASSERT_TRUE(Storage.openFileForWrite("CSS", "css_cascade_test/css_rules.cache.tmp", staleTemp));
  staleTemp.close();

  CssParser reader("css_cascade_test");
  ASSERT_TRUE(reader.loadFromCache());
  EXPECT_EQ(reader.resolveStyle("div", "old").textAlign, CssTextAlign::Left);
  EXPECT_TRUE(Storage.exists("css_cascade_test/css_rules.cache"));
  EXPECT_FALSE(Storage.exists("css_cascade_test/css_rules.cache.tmp"));
}
