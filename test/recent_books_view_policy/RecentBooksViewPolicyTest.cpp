#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "util/RecentBooksViewPolicy.h"

namespace {
struct Book {
  std::string path;
  bool finished;
};

bool visibleInTab(const Book& book, const uint8_t tab) {
  return RecentBooksViewPolicy::tabIncludes(tab, book.finished);
}
}  // namespace

TEST(RecentBooksViewPolicy, KeepsEveryRecentBookReachableAcrossFilteredTabs) {
  std::vector<Book> books;
  for (size_t index = 0; index < 28; ++index) {
    books.push_back(Book{"/manga/Book " + std::to_string(index) + ".xtc", (index % 2) != 0});
  }

  EXPECT_EQ(RecentBooksViewPolicy::countVisible(books, [](const Book& book) { return visibleInTab(book, 0); }), 28U);
  EXPECT_EQ(RecentBooksViewPolicy::countVisible(books, [](const Book& book) { return visibleInTab(book, 1); }), 14U);
  EXPECT_EQ(RecentBooksViewPolicy::countVisible(books, [](const Book& book) { return visibleInTab(book, 2); }), 14U);

  for (size_t visible = 0; visible < 14; ++visible) {
    const size_t readingRaw = RecentBooksViewPolicy::rawIndexForVisible(
        books, visible, [](const Book& book) { return visibleInTab(book, 1); });
    const size_t finishedRaw = RecentBooksViewPolicy::rawIndexForVisible(
        books, visible, [](const Book& book) { return visibleInTab(book, 2); });
    EXPECT_EQ(readingRaw, visible * 2);
    EXPECT_EQ(finishedRaw, visible * 2 + 1);
    EXPECT_FALSE(books[readingRaw].finished);
    EXPECT_TRUE(books[finishedRaw].finished);
  }

  EXPECT_EQ(RecentBooksViewPolicy::rawIndexForVisible(
                books, 14, [](const Book& book) { return visibleInTab(book, 1); }),
            static_cast<size_t>(-1));
  EXPECT_EQ(RecentBooksViewPolicy::rawIndexForVisible(
                books, 27, [](const Book& book) { return visibleInTab(book, 0); }),
            27U);
}
