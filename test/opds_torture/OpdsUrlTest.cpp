#include <gtest/gtest.h>

#include "UrlUtils.h"

namespace {
TEST(OpdsUrl, ResolvesNestedAndDotRelativeReferencesAgainstFeed) {
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/root/index.xml", "sections/fiction.xml"),
            "https://catalog.example/root/sections/fiction.xml");
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/root/sections/fiction.xml", "../history.xml"),
            "https://catalog.example/root/history.xml");
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/root/sections/fiction.xml", "/books/42.epub"),
            "https://catalog.example/books/42.epub");
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/root/sections/fiction.xml", "//cdn.example/book.epub"),
            "https://cdn.example/book.epub");
}

TEST(OpdsUrl, ResolvesQueryAndFragmentReferencesWithoutAppendingAsPath) {
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/feed.xml?old=1", "?page=2"),
            "https://catalog.example/feed.xml?page=2");
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/feed.xml?cursor=1", "#section"),
            "https://catalog.example/feed.xml?cursor=1#section");
  EXPECT_EQ(UrlUtils::buildUrl("https://catalog.example/a/b/", "./c/../d.xml"),
            "https://catalog.example/a/b/d.xml");
}

TEST(OpdsUrl, EncodesUnsafeCharactersAndPreservesExistingEscapes) {
  EXPECT_EQ(UrlUtils::buildUrl("http://catalog.example/feed", "book title.epub?q=a%20b"),
            "http://catalog.example/book%20title.epub?q=a%20b");
  EXPECT_EQ(UrlUtils::buildUrl("http://catalog.example/feed", "bad%name.epub"),
            "http://catalog.example/bad%25name.epub");
  EXPECT_EQ(UrlUtils::buildUrl("http://catalog.example/root/feed.xml", "../search?q=http://other.example/path"),
            "http://catalog.example/search?q=http://other.example/path");
}

TEST(OpdsUrl, SameOriginNormalizesHostCaseAndDefaultPorts) {
  EXPECT_TRUE(UrlUtils::sameOrigin("https://user:pass@Catalog.Example/feed", "https://catalog.example:443/book"));
  EXPECT_TRUE(UrlUtils::sameOrigin("http://catalog.example/feed", "http://catalog.example:80/book"));
  EXPECT_FALSE(UrlUtils::sameOrigin("https://catalog.example/feed", "http://catalog.example/feed"));
  EXPECT_FALSE(UrlUtils::sameOrigin("https://catalog.example/feed", "https://cdn.example/book"));
  EXPECT_FALSE(UrlUtils::sameOrigin("https://catalog.example/feed", "https://catalog.example:444/book"));
  EXPECT_FALSE(UrlUtils::sameOrigin("https://catalog.example/feed", "https://catalog.example:999999999999/book"));
  EXPECT_FALSE(UrlUtils::sameOrigin("https://catalog.example/feed", "https://catalog.example:/book"));
}

TEST(OpdsUrl, DetectsHttpsDowngrades) {
  EXPECT_TRUE(UrlUtils::isHttpsDowngrade("https://catalog.example/feed", "http://catalog.example/feed"));
  EXPECT_FALSE(UrlUtils::isHttpsDowngrade("http://catalog.example/feed", "https://catalog.example/feed"));
  EXPECT_FALSE(UrlUtils::isHttpsDowngrade("https://catalog.example/feed", "https://cdn.example/feed"));
}
}  // namespace
