#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "OpdsParser.h"

namespace {
void writeInChunks(OpdsParser& parser, const std::string& xml, const size_t chunkSize) {
  for (size_t offset = 0; offset < xml.size(); offset += chunkSize) {
    const size_t count = std::min(chunkSize, xml.size() - offset);
    ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), count), count);
  }
  parser.flush();
}

std::string entryXml(const std::string& title, const std::string& href) {
  return "<entry><title>" + title + "</title><link rel=\"http://opds-spec.org/acquisition/open-access\" "
         "type=\"application/epub+zip\" href=\"" +
         href + "\"/></entry>";
}

TEST(OpdsParserTorture, AcceptsChunkedWellFormedFeedAndNamespacedElements) {
  const std::string xml =
      "<atom:feed xmlns:atom=\"http://www.w3.org/2005/Atom\"><atom:entry><atom:title>One</atom:title>"
      "<atom:author><atom:name>Author</atom:name></atom:author><atom:id>id-1</atom:id>"
      "<atom:link rel=\"http://opds-spec.org/acquisition/open-access\" type=\"application/epub+zip\" "
      "href=\"books/one.epub\"/></atom:entry></atom:feed>";

  OpdsParser parser;
  writeInChunks(parser, xml, 7);

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "One");
  EXPECT_EQ(parser.getEntries()[0].author, "Author");
  EXPECT_EQ(parser.getEntries()[0].href, "books/one.epub");
  EXPECT_EQ(parser.getEntries()[0].id, "id-1");
}

TEST(OpdsParserTorture, RejectsMalformedAndTruncatedXmlWithoutPublishingPartialEntry) {
  for (const std::string xml : {
           "<feed><entry><title>Broken</title><link type=\"application/atom+xml\" href=\"next\"/></oops></entry></feed>",
           "<feed><entry><title>Truncated</title><link type=\"application/atom+xml\" href=\"next\"/></entry>",
       }) {
    OpdsParser parser;
    writeInChunks(parser, xml, 3);
    EXPECT_TRUE(parser.error()) << xml;
    EXPECT_TRUE(parser.getEntries().empty()) << xml;
  }
}

TEST(OpdsParserTorture, SkipsEntriesMissingTitleOrUsableLinkAndAllowsOptionalMetadata) {
  const std::string xml =
      "<feed>"
      "<entry><title>No link</title></entry>"
      "<entry><link type=\"application/atom+xml\" href=\"no-title.xml\"/></entry>"
      "<entry><title>Usable</title><link type=\"application/atom+xml\" href=\"section.xml\"/></entry>"
      "</feed>";
  OpdsParser parser;
  writeInChunks(parser, xml, 1024);

  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Usable");
  EXPECT_EQ(parser.getEntries()[0].href, "section.xml");
  EXPECT_EQ(parser.getEntries()[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_TRUE(parser.getEntries()[0].author.empty());
  EXPECT_TRUE(parser.getEntries()[0].id.empty());
}

TEST(OpdsParserTorture, IgnoresBadOrOversizedOpenSearchAndPagingLinks) {
  const std::string longHref(769, 'x');
  const std::string xml =
      "<feed>"
      "<link rel=\"search\" href=\"/search?q=no-placeholder\"/>"
      "<link rel=\"search\" href=\"" + longHref + "{searchTerms}\"/>"
      "<link rel=\"next\" href=\"" + longHref + "\"/>"
      "<link rel=\"previous\" href=\"../previous.xml\"/>"
      "<entry><title>Book</title><link rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\" "
      "href=\"" +
      longHref + "\"/></entry>"
      "</feed>";
  OpdsParser parser;
  writeInChunks(parser, xml, 97);

  EXPECT_TRUE(parser.getSearchTemplate().empty());
  EXPECT_TRUE(parser.getNextPageUrl().empty());
  EXPECT_EQ(parser.getPrevPageUrl(), "../previous.xml");
  EXPECT_TRUE(parser.getEntries().empty());
}

TEST(OpdsParserTorture, BoundsHugeCatalogAndMarksItTruncated) {
  std::string xml = "<feed>";
  for (int i = 0; i < 90; ++i) xml += entryXml("Book " + std::to_string(i), "books/" + std::to_string(i) + ".epub");
  xml += "</feed>";

  OpdsParser parser;
  writeInChunks(parser, xml, 31);
  EXPECT_FALSE(parser.error());
  EXPECT_EQ(parser.getEntries().size(), 62u);
  EXPECT_TRUE(parser.truncated());
}

TEST(OpdsParserTorture, ReinitializesAfterClearAndAcrossRepeatedRetries) {
  OpdsParser parser;
  writeInChunks(parser, "<feed>" + entryXml("First", "one.epub") + "</feed>", 9);
  ASSERT_EQ(parser.getEntries().size(), 1u);
  parser.clear();
  writeInChunks(parser, "<feed>" + entryXml("Second", "two.epub") + "</feed>", 5);
  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Second");

  for (int i = 0; i < 100; ++i) {
    OpdsParser retry;
    writeInChunks(retry, "<feed>" + entryXml("Retry", "retry.epub") + "</feed>", 13);
    ASSERT_FALSE(retry.error());
    ASSERT_EQ(retry.getEntries().size(), 1u);
  }
}
}  // namespace
