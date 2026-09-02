#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <expat.h>

#include "FootnoteEntry.h"
#include "TocNavAmpersandSanitizer.h"
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
  std::vector<std::string> classes;
  std::vector<std::string> styles;
  std::vector<std::string> guideTypes;
  std::vector<std::pair<std::string, std::string>> guideReferences;
  std::vector<std::pair<std::string, std::string>> blockTexts;
  std::vector<size_t> openBlockIndexes;
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

bool isBlockElement(const std::string_view name) {
  return name == "p" || name == "li" || name == "div" || name == "br" || name == "blockquote" ||
         (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6');
}

void XMLCALL onStart(void* data, const XML_Char* name, const XML_Char** atts) {
  auto& probe = *static_cast<XmlProbe*>(data);
  probe.elementNames.emplace_back(name);
  probe.depth++;

  if (isBlockElement(name)) {
    probe.blockTexts.emplace_back(name, "");
    probe.openBlockIndexes.push_back(probe.blockTexts.size() - 1);
  }

  if (const char* id = attribute(atts, "id")) probe.ids.emplace_back(id);
  if (const char* href = attribute(atts, "href")) probe.hrefs.emplace_back(href);
  if (const char* className = attribute(atts, "class")) probe.classes.emplace_back(className);
  if (const char* style = attribute(atts, "style")) probe.styles.emplace_back(style);
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
  if (!probe.openBlockIndexes.empty()) probe.blockTexts[probe.openBlockIndexes.back()].second.append(text, length);
}

void XMLCALL onEnd(void* data, const XML_Char* name) {
  auto& probe = *static_cast<XmlProbe*>(data);
  if (probe.depth == 1) probe.rootClosed = true;
  if (probe.depth == probe.pagebreakDepth) probe.pagebreakDepth = -1;
  if (!probe.openBlockIndexes.empty() && probe.blockTexts[probe.openBlockIndexes.back()].first == name) {
    probe.openBlockIndexes.pop_back();
  }
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

struct SanitizedOutput {
  std::string value;
};

bool collectSanitizedOutput(void* context, const uint8_t* data, const size_t size) {
  auto& output = *static_cast<SanitizedOutput*>(context);
  output.value.append(reinterpret_cast<const char*>(data), size);
  return true;
}

std::string sanitizeInChunks(const std::string& input, const size_t chunkSize, bool* ok, bool* changed) {
  SanitizedOutput output;
  TocNavAmpersandSanitizer sanitizer(collectSanitizedOutput, &output);
  *ok = true;
  for (size_t offset = 0; offset < input.size(); offset += chunkSize) {
    const size_t length = std::min(chunkSize, input.size() - offset);
    if (!sanitizer.write(reinterpret_cast<const uint8_t*>(input.data() + offset), length)) {
      *ok = false;
      break;
    }
  }
  if (*ok) *ok = sanitizer.finish();
  *changed = sanitizer.changed();
  return output.value;
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
  EXPECT_STREQ(lookupHtmlEntity("&apos;", 6), "'");
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

TEST(EpubParserRegression, BareAmpersandInChapterTextIsRecovered) {
  const std::string nav =
      R"(<html><body><nav epub:type="toc"><ol><li><a href="a.xhtml">Terpecah & Terbelah</a></li></ol></nav></body></html>)";
  XmlProbe strictProbe;
  EXPECT_EQ(parseXml(nav, strictProbe), XML_ERROR_INVALID_TOKEN);

  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(nav, 7, &ok, &changed);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(changed);
  EXPECT_NE(sanitized.find("Terpecah &amp; Terbelah"), std::string::npos);

  XmlProbe recoveredProbe;
  EXPECT_EQ(parseXml(sanitized, recoveredProbe), XML_ERROR_NONE);
}

TEST(EpubParserRegression, MultipleBareAmpersandsInChapterTextAreRecovered) {
  const std::string nav = R"(<root>A & B & C & D</root>)";
  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(nav, 1, &ok, &changed);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(changed);
  EXPECT_EQ(std::count(sanitized.begin(), sanitized.end(), '&'), 3);
  EXPECT_EQ(sanitized, "<root>A &amp; B &amp; C &amp; D</root>");

  XmlProbe probe;
  EXPECT_EQ(parseXml(sanitized, probe), XML_ERROR_NONE);
}

TEST(EpubParserRegression, ValidChapterEntitiesRemainUnchanged) {
  const std::string input = R"(<root>&amp; &lt; &gt; &quot; &apos;</root>)";
  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(input, 2, &ok, &changed);

  EXPECT_TRUE(ok);
  EXPECT_FALSE(changed);
  EXPECT_EQ(sanitized, input);

  XmlProbe probe;
  EXPECT_EQ(parseXml(sanitized, probe), XML_ERROR_NONE);
}

TEST(EpubParserRegression, NumericAndHexChapterEntitiesRemainUnchanged) {
  const std::string input = R"(<root>&#123; &#x1F600;</root>)";
  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(input, 1, &ok, &changed);

  EXPECT_TRUE(ok);
  EXPECT_FALSE(changed);
  EXPECT_EQ(sanitized, input);

  XmlProbe probe;
  EXPECT_EQ(parseXml(sanitized, probe), XML_ERROR_NONE);
}

TEST(EpubParserRegression, MixedChapterEntitiesAndBareAmpersandsOnlyEscapeBareOnes) {
  const std::string input = R"(<root>A &amp; B & C &#123; D &lt; E & F</root>)";
  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(input, 3, &ok, &changed);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(changed);
  EXPECT_EQ(sanitized, R"(<root>A &amp; B &amp; C &#123; D &lt; E &amp; F</root>)");

  XmlProbe probe;
  EXPECT_EQ(parseXml(sanitized, probe), XML_ERROR_NONE);
}

TEST(EpubParserRegression, UnrelatedMalformedChapterXmlStillFailsAfterRecovery) {
  const std::string input = R"(<root><item>A &amp; B</item>)";
  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(input, 5, &ok, &changed);

  EXPECT_TRUE(ok);
  EXPECT_FALSE(changed);
  EXPECT_EQ(sanitized, input);

  XmlProbe probe;
  EXPECT_NE(parseXml(sanitized, probe), XML_ERROR_NONE);
}

TEST(EpubParserRegression, ChapterRecoveryHandlesAmpersandsAndEntitiesSplitAcrossBuffers) {
  const std::string xhtml =
      R"(<html><body><p>Valid &amp; text, bare & text, numeric &#123;, hex &#x1F600;.</p></body></html>)";
  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(xhtml, 1, &ok, &changed);

  ASSERT_TRUE(ok);
  ASSERT_TRUE(changed);
  EXPECT_NE(sanitized.find("Valid &amp; text, bare &amp; text, numeric &#123;, hex &#x1F600;."),
            std::string::npos);

  XmlProbe probe;
  EXPECT_EQ(parseXml(sanitized, probe), XML_ERROR_NONE);
  EXPECT_NE(probe.text.find("Valid & text, bare & text"), std::string::npos);
}

TEST(EpubParserRegression, EofAfterHiddenPageMapElementPreservesAllVisibleText) {
  const std::string xhtml = readFixture("chapter_eof_hidden_page_map.xhtml");
  ASSERT_FALSE(xhtml.empty());

  XmlProbe probe;
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);

  constexpr std::string_view visible[] = {
      "VISIBLE_PAGE_PREFIX",
      "VISIBLE_FINAL_PARAGRAPH",
      "VISIBLE_TAIL_MARKER",
  };
  for (const auto text : visible) {
    ASSERT_EQ(probe.text.find(text), probe.text.rfind(text)) << text;
  }
  ASSERT_TRUE(probe.rootClosed);
  ASSERT_EQ(probe.ids.size(), 2u);
  ASSERT_EQ(probe.classes.size(), 2u);
  EXPECT_EQ(probe.classes[0], "chapter");
  EXPECT_EQ(probe.classes[1], "page-map");
  ASSERT_EQ(probe.styles.size(), 1u);
  EXPECT_EQ(probe.styles.front(), "display:none;");
  EXPECT_EQ(probe.ids[0], "GBS.TEST.01");
  EXPECT_EQ(probe.ids[1], "GBS.TEST.02");
  const auto prefix = probe.text.find("VISIBLE_PAGE_PREFIX");
  const auto finalParagraph = probe.text.find("VISIBLE_FINAL_PARAGRAPH");
  const auto tail = probe.text.find("VISIBLE_TAIL_MARKER");
  ASSERT_NE(prefix, std::string::npos);
  ASSERT_NE(finalParagraph, std::string::npos);
  ASSERT_NE(tail, std::string::npos);
  EXPECT_LT(prefix, finalParagraph);
  EXPECT_LT(finalParagraph, tail);
}

TEST(EpubParserRegression, BareAmpersandInChapterAttributeIsRecovered) {
  const std::string xhtml =
      R"(<html><body><p><a href="chapter.xhtml?left=1&right=2">Query link</a></p></body></html>)";
  XmlProbe strictProbe;
  EXPECT_EQ(parseXml(xhtml, strictProbe), XML_ERROR_INVALID_TOKEN);

  bool ok = false;
  bool changed = false;
  const std::string sanitized = sanitizeInChunks(xhtml, 5, &ok, &changed);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(changed);
  EXPECT_NE(sanitized.find("chapter.xhtml?left=1&amp;right=2"), std::string::npos);

  XmlProbe recoveredProbe;
  ASSERT_EQ(parseXml(sanitized, recoveredProbe), XML_ERROR_NONE);
  ASSERT_EQ(recoveredProbe.hrefs.size(), 1u);
  EXPECT_EQ(recoveredProbe.hrefs.front(), "chapter.xhtml?left=1&right=2");
}

TEST(EpubParserRegression, MixedArabicLatinBlockElementsRemainDistinct) {
  const std::string xhtml =
      R"(<html xmlns="http://www.w3.org/1999/xhtml"><body><div id="arabic-block">&#x645;&#x631;&#x62D;&#x628;&#x627; ARABIC_BLOCK_END</div><p id="latin-block">LATIN_BLOCK_START English paragraph.</p><div id="inline-mixed">MIXED_START <span>&#x645;&#x631;&#x62D;&#x628;&#x627;</span> MIXED_END</div></body></html>)";
  XmlProbe probe;
  ASSERT_EQ(parseXml(xhtml, probe), XML_ERROR_NONE);
  ASSERT_EQ(probe.blockTexts.size(), 3u);

  EXPECT_EQ(probe.blockTexts[0].first, "div");
  EXPECT_NE(probe.blockTexts[0].second.find("ARABIC_BLOCK_END"), std::string::npos);
  EXPECT_EQ(probe.blockTexts[1].first, "p");
  EXPECT_NE(probe.blockTexts[1].second.find("LATIN_BLOCK_START"), std::string::npos);
  EXPECT_EQ(probe.blockTexts[2].first, "div");
  EXPECT_NE(probe.blockTexts[2].second.find("MIXED_START"), std::string::npos);
  EXPECT_NE(probe.blockTexts[2].second.find("MIXED_END"), std::string::npos);
}
