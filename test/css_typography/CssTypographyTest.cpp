#include <gtest/gtest.h>

#include <string_view>

#include "CssParser.h"

TEST(CssTypographyTest, ParsesBoundedFontSizeUnits) {
  const CssStyle style = CssParser::parseInlineStyle(
      "font-size: 0.55em; line-height: 1.2; margin: 1em; padding: 4px;");

  ASSERT_TRUE(style.hasFontSize());
  EXPECT_FLOAT_EQ(style.fontSize.value, 0.55f);
  EXPECT_EQ(style.fontSize.unit, CssUnit::Em);
  ASSERT_TRUE(style.hasLineHeight());
  EXPECT_FLOAT_EQ(style.lineHeight.value, 1.2f);
  EXPECT_EQ(style.lineHeight.unit, CssUnit::Unitless);
}

TEST(CssTypographyTest, ParsesSupportedAbsoluteAndRelativeUnits) {
  const CssStyle style = CssParser::parseInlineStyle(
      "font-size: 16px; line-height: 18pt;");
  ASSERT_TRUE(style.hasFontSize());
  EXPECT_FLOAT_EQ(style.fontSize.value, 16.0f);
  EXPECT_EQ(style.fontSize.unit, CssUnit::Pixels);
  ASSERT_TRUE(style.hasLineHeight());
  EXPECT_FLOAT_EQ(style.lineHeight.value, 18.0f);
  EXPECT_EQ(style.lineHeight.unit, CssUnit::Points);

  const CssStyle percent = CssParser::parseInlineStyle("font-size: 120%; line-height: 125%;");
  ASSERT_TRUE(percent.hasFontSize());
  EXPECT_EQ(percent.fontSize.unit, CssUnit::Percent);
  ASSERT_TRUE(percent.hasLineHeight());
  EXPECT_EQ(percent.lineHeight.unit, CssUnit::Percent);
}

TEST(CssTypographyTest, PreservesCascadeAndImportantValues) {
  const CssStyle style = CssParser::parseInlineStyle(
      "font-size: 1.1rem !important; line-height: 1.4em !important;");
  ASSERT_TRUE(style.hasFontSize());
  EXPECT_FLOAT_EQ(style.fontSize.value, 1.1f);
  EXPECT_EQ(style.fontSize.unit, CssUnit::Rem);
  ASSERT_TRUE(style.hasLineHeight());
  EXPECT_FLOAT_EQ(style.lineHeight.value, 1.4f);
  EXPECT_EQ(style.lineHeight.unit, CssUnit::Em);
}

TEST(CssTypographyTest, RejectsUnsupportedOrUnsafeValues) {
  const CssStyle style = CssParser::parseInlineStyle(
      "font-size: medium; line-height: normal; font-size: 1vh;");
  EXPECT_FALSE(style.hasFontSize());
  EXPECT_FALSE(style.hasLineHeight());

  const CssStyle unitlessFont = CssParser::parseInlineStyle("font-size: 2;");
  EXPECT_FALSE(unitlessFont.hasFontSize());
}
