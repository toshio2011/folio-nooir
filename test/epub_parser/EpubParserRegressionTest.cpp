#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <expat.h>

#include "FootnoteEntry.h"
#include "XmlParserUtils.h"
#include "htmlEntities.h"

namespace {

struct XmlProbe {
  int depth = 0;
  int pagebreakDepth = -1;
  bool rootClosed = false;
  std::string text;
  std::string pagebreakText;
  std::vector<std::string> elementNames;
  std::vector<std::string> ids;
  std::vector<std::string> hrefs;
  std::vector<std::string> guideTypes;
  std::vector<std::pair<std::string, std::string>> guideReferences;
};

std::string readFixture(const char* name) {
  std::ifstream file(std::string(EPUB_REGRESSION_FIXTURE_DIR) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

const char* attribute(const XML_Char** atts, const char* wanted) {
  for (int i = 0; atts && atts[i]; i += 2) {
    if (std::string_view(atts[i]) == wanted) return atts[i + 1];
  }
  return nullptr;
}

bool isPagebreak(const XML_Char** atts) {
  const char* role = attribute(atts, "role");
  const char* epubType = attribute(atts, "epub:type");
  return (role && std::string_view(role) == "doc-pagebreak") ||
         (epubType && std::string_view(epubType) == "pagebreak");
}

void XMLCALL onStart(void* data, const XML_Char* name, const XML_Char** atts) {
  auto& probe = *static_cast<XmlProbe*>(data);
  probe.elementNames.emplace_back(name);
  probe.depth++;

  if (const char* id = attribute(atts, "id")) probe.ids.emplace_back(id);
  if (const char* href = attribute(atts, "href")) probe.hrefs.emplace_back(href);
  const char* type = attribute(atts, "type");
  const char* guideHref = attribute(atts, "href");
  if (type) probe.guideTypes.emplace_back(type);
  if (type && guideHref) probe.guideReferences.emplace_back(type, guideHref);

  if (isPagebreak(atts)) probe.pagebreakDepth = probe.depth;
}

void XMLCALL onText(void* data, const XML_Char* text, const int length) {
  auto& probe = *static_cast<XmlProbe*>(data);
  probe.text.append(text, length);
  if (probe.pagebreakDepth >= 0 && probe.depth >= probe.pagebreakDepth) probe.pagebreakText.append(text, length);
}

void XMLCALL onEnd(void* data, const XML_Char*) {
  auto& probe = *static_cast<XmlProbe*>(data);
  if (probe.depth == 1) probe.rootClosed = true;
  if (probe.depth == probe.pagebreakDepth) probe.pagebreakDepth = -1;
  probe.depth--;
}

enum XML_Error parseXml(const std::string& input, XmlProbe& probe) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  XML_SetUserData(parser, &probe);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);

  constexpr size_t CHUNK_SIZE = 127;
  enum XML_Error error = XML_ERROR_NONE;
  for (size_t offset = 0; offset < input.size(); offset += CHUNK_SIZE) {
    const size_t length = std::min(CHUNK_SIZE, input.size() - offset);
    const XML_Bool final = offset + length == input.size() ? XML_TRUE : XML_FALSE;
    if (XML_Parse(parser, input.data() + offset, static_cast<int>(length), final) == XML_STATUS_ERROR) {
      error = XML_GetErrorCode(parser);
      break;
    }
  }
  XML_ParserFree(parser);
  return error;
}

}  // namespace

TEST(EpubParserRegression, OpfNamesUseLocalNameAcrossPrefixes) {
  const std::string opf = readFixture("opf_namespace_prefix.opf");
  XmlProbe probe;
  ASSERT_FALSE(opf.empty());
  ASSERT_EQ(parseXml(opf, probe), XML_ERROR_NONE);

  EXPECT_NE(std::find_if(probe.elementNames.begin(), probe.elementNames.end(),
                         [](const std::string& name) { return xmlNameIs(name.c_str(), "package"); }),
            probe.elementNames.end());
  EXPECT_NE(std::find_if(probe.elementNames.begin(), probe.elementNames.end(),
                         [](const std::string& name) { return xmlNameIs(name.c_str(), "title"); }),
            probe.elementNames.end());
  EXPECT_EQ(xmlLocalName("opf2:metadata"), std::string("metadata"));
}

TEST(EpubParserRegression, AmbiguousGuideTextDoesNotChooseOpeningLocation) {
  const std::string opf = readFixture("opf_ambiguous_guide.opf");
  XmlProbe probe;
  ASSERT_FALSE(opf.empty());
  ASSERT_EQ(parseXml(opf, probe), XML_ERROR_NONE);
  ASSERT_EQ(probe.guideTypes.size(), 3u);
  ASSERT_EQ(probe.guideReferences.size(), 3u);

  const auto explicitStart = std::find_if(
      probe.guideReferences.begin(), probe.guideReferences.end(),
      [](const auto& reference) { return reference.first == "start"; });
  ASSERT_NE(explicitStart, probe.guideReferences.end());
  EXPECT_EQ(explicitStart->second, "chapter.xhtml#opening");
  EXPECT_EQ(std::count(probe.guideTypes.begin(), probe.guideTypes.end(), "text"), 2);
}

TEST(EpubParserRegression, TrailingContentIsIdentifiedAfterClosedRoot) {
  const std::string xhtml = readFixture("chapter_trailing.xhtml");
  XmlProbe probe;
  ASSERT_FALSE(xhtml.empty());
  EXPECT_EQ(parseXml(xhtml, probe), XML_ERROR_JUNK_AFTER_DOC_ELEMENT);
  EXPECT_TRUE(probe.rootClosed);
  EXPECT_NE(probe.text.find("Before trailing"), std::string::npos);
}

TEST(EpubParserRegression, ApostropheEntityIsAvailableToChapterEntityExpansion) {
  const std::string xhtml = readFixture("chapter_entities.xhtml");
  XmlProbe probe;
  ASSERT_FALSE(xhtml.empty());
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);
  EXPECT_NE(probe.text.find("This 'entity' must survive."), std::string::npos);
  EXPECT_STREQ(lookupHtmlEntity("&apos;", 7), "'");
}

TEST(EpubParserRegression, PagebreakTextRemainsVisibleContent) {
  const std::string xhtml = readFixture("chapter_pagebreak.xhtml");
  XmlProbe probe;
  ASSERT_FALSE(xhtml.empty());
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);
  EXPECT_NE(probe.pagebreakText.find("Printed page 12"), std::string::npos);
}

TEST(EpubParserRegression, ReferencedInlineSpanIsDistinguishableFromDecorativeSpan) {
  const std::string xhtml = readFixture("chapter_inline_anchor.xhtml");
  XmlProbe probe;
  ASSERT_FALSE(xhtml.empty());
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);
  ASSERT_EQ(probe.ids.size(), 2u);
  ASSERT_EQ(probe.hrefs.size(), 1u);
  EXPECT_EQ(probe.hrefs.front(), "#referenced-inline");
  EXPECT_NE(std::find(probe.ids.begin(), probe.ids.end(), "referenced-inline"), probe.ids.end());
  EXPECT_NE(std::find(probe.ids.begin(), probe.ids.end(), "decorative-only"), probe.ids.end());
}

TEST(EpubParserRegression, LongFootnoteHrefFitsBoundedStorage) {
  const std::string xhtml = readFixture("chapter_long_footnote.xhtml");
  XmlProbe probe;
  ASSERT_FALSE(xhtml.empty());
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);
  ASSERT_EQ(probe.hrefs.size(), 1u);
  EXPECT_GT(probe.hrefs.front().size(), 96u);
  EXPECT_LE(probe.hrefs.front().size(), 255u);
  EXPECT_EQ(FOOTNOTE_HREF_LEN, 256u);
}

TEST(EpubParserRegression, DenseFixtureExercisesManyIncrementalParserChunks) {
  const std::string fixture = readFixture("chapter_dense.xhtml");
  ASSERT_FALSE(fixture.empty());
  ASSERT_NE(fixture.find("DENSE_TEXT"), std::string::npos);

  std::string dense;
  dense.reserve(12000);
  for (int i = 0; i < 6000; ++i) dense += (i % 11 == 0) ? "word\n" : "word ";
  std::string xhtml = fixture;
  xhtml.replace(xhtml.find("DENSE_TEXT"), 10, dense);

  XmlProbe probe;
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);
  EXPECT_GT(probe.text.size(), 10000u);
}
