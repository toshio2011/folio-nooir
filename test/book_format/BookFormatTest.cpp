#include <gtest/gtest.h>

#include <string>

#include "util/BookFormat.h"

TEST(BookFormat, ResolvesSupportedFinalExtensionsCaseInsensitively) {
  EXPECT_EQ(BookFormat::labelForPath("/books/novel.EPUB"), std::string("EPUB"));
  EXPECT_EQ(BookFormat::labelForPath("C:\\books\\comic.CbZ"), std::string("CBZ"));
  EXPECT_EQ(BookFormat::labelForPath("/books/converted.XTC"), std::string("XTC"));
  EXPECT_EQ(BookFormat::labelForPath("/books/converted.XTcH"), std::string("XTCH"));
  EXPECT_EQ(BookFormat::labelForPath("/books/notes.TxT"), std::string("TXT"));
  EXPECT_EQ(BookFormat::labelForPath("/books/notes.Md"), std::string("MD"));
}

TEST(BookFormat, UsesOnlyTheFinalExtension) {
  EXPECT_EQ(BookFormat::labelForPath("/books/archive.epub.backup.CBZ"), std::string("CBZ"));
  EXPECT_EQ(BookFormat::labelForPath("/books/edition.final.epub"), std::string("EPUB"));
}

TEST(BookFormat, HandlesMissingUnknownAndHiddenNamesGracefully) {
  EXPECT_EQ(BookFormat::kindForPath("/books/no-extension"), BookFormat::Kind::Unknown);
  EXPECT_EQ(BookFormat::kindForPath("/books/book."), BookFormat::Kind::Unknown);
  EXPECT_EQ(BookFormat::kindForPath("/books/.epub"), BookFormat::Kind::Unknown);
  EXPECT_EQ(BookFormat::kindForPath("/books/book.pdf"), BookFormat::Kind::Unknown);
  EXPECT_EQ(BookFormat::labelForPath(nullptr), nullptr);
}

TEST(BookFormat, DoesNotAlterUtf8PathOrTitleData) {
  const std::string path = "/books/القرآن.final.EPUB";
  const std::string title = "القرآن الكريم — 章";
  const std::string originalPath = path;
  const std::string originalTitle = title;

  EXPECT_EQ(BookFormat::labelForPath(path), std::string("EPUB"));
  EXPECT_EQ(path, originalPath);
  EXPECT_EQ(title, originalTitle);
}
