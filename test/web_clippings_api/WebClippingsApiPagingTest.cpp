#include "network/ClippingsApiPaging.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FixtureBook {
  std::string path;
  std::string title;
  size_t count;
};

template <typename Visitor>
void visitFixtureBooks(const std::vector<FixtureBook>& books, Visitor&& visitor) {
  for (const auto& book : books) {
    if (!visitor(book.path, book.title)) return;
  }
}

bool countFixtureBook(const std::vector<FixtureBook>& books, const std::string& path, size_t& count) {
  const auto found = std::find_if(books.begin(), books.end(), [&](const FixtureBook& book) {
    return book.path == path;
  });
  if (found == books.end()) return false;
  count = found->count;
  return true;
}

bool loadFixturePage(const std::vector<FixtureBook>& books, const std::string& path, const size_t offset,
                     const size_t limit, std::vector<ClippingEntry>& clippings,
                     std::vector<std::string>& notes, size_t& totalCount) {
  if (!countFixtureBook(books, path, totalCount)) return false;
  const size_t end = std::min(totalCount, offset + limit);
  for (size_t index = offset; index < end; ++index) {
    ClippingEntry entry;
    entry.text = path + "#" + std::to_string(index);
    clippings.push_back(std::move(entry));
    notes.push_back("note-" + std::to_string(index));
  }
  return true;
}

}  // namespace

TEST(WebClippingsApiPaging, RejectsInvalidOffsetsAndBoundsPageSize) {
  WebClippingsApi::PageWindow page;
  EXPECT_FALSE(WebClippingsApi::makePageWindow(-1, 12, page));
  EXPECT_FALSE(WebClippingsApi::makePageWindow(WebClippingsApi::kMaxOffset + 1, 12, page));
  EXPECT_FALSE(WebClippingsApi::makePageWindow(0, 0, page));
  ASSERT_TRUE(WebClippingsApi::makePageWindow(24, 100, page));
  EXPECT_EQ(page.offset, 24u);
  EXPECT_EQ(page.limit, WebClippingsApi::kPageLimit);
}

TEST(WebClippingsApiPaging, PerBookRouteUsesClipFilePageAndReportsTotals) {
  const std::vector<FixtureBook> books{{"/Books/Reader.epub", "Reader", 29}};
  std::vector<ClippingEntry> clippings;
  std::vector<std::string> notes;
  bool called = false;
  const auto summary = WebClippingsApi::loadBookPage(
      books[0].path, 12, 12, clippings, notes,
      [&](const std::string& path, const size_t offset, const size_t limit,
          std::vector<ClippingEntry>& page, std::vector<std::string>& pageNotes, size_t& totalCount) {
        called = true;
        EXPECT_EQ(path, books[0].path);
        EXPECT_EQ(offset, 12u);
        EXPECT_EQ(limit, 12u);
        return loadFixturePage(books, path, offset, limit, page, pageNotes, totalCount);
      });

  EXPECT_TRUE(called);
  EXPECT_EQ(summary.storageErrors, 0u);
  EXPECT_EQ(summary.recordsFound, 29u);
  EXPECT_EQ(summary.responseCount, 12u);
  EXPECT_TRUE(summary.hasMore);
  ASSERT_EQ(clippings.size(), 12u);
  EXPECT_EQ(clippings.front().text, "/Books/Reader.epub#12");
  EXPECT_EQ(notes.front(), "note-12");
}

TEST(WebClippingsApiPaging, AllBooksRouteStreamsOnlyTheRequestedWindowAcrossBooks) {
  const std::vector<FixtureBook> books{{"/Books/A.epub", "A", 2},
                                       {"/Books/B.epub", "B", 4},
                                       {"/Books/C.epub", "C", 3}};
  std::vector<std::pair<std::string, size_t>> emitted;
  std::vector<std::string> loadedBooks;
  const auto summary = WebClippingsApi::streamAllBooksPage(
      1, 3,
      [&](auto&& visitor) { visitFixtureBooks(books, std::forward<decltype(visitor)>(visitor)); },
      [&](const std::string& path, size_t& count) { return countFixtureBook(books, path, count); },
      [&](const std::string& path, const size_t offset, const size_t limit,
          std::vector<ClippingEntry>& clippings, std::vector<std::string>& notes, size_t& totalCount) {
        loadedBooks.push_back(path);
        return loadFixturePage(books, path, offset, limit, clippings, notes, totalCount);
      },
      [&](const std::string& path, const std::string&, const size_t index, const ClippingEntry&, const std::string&) {
        emitted.emplace_back(path, index);
      });

  EXPECT_EQ(summary.recordsFound, 6u);
  EXPECT_EQ(summary.responseCount, 3u);
  EXPECT_EQ(summary.storageErrors, 0u);
  EXPECT_TRUE(summary.hasMore);
  ASSERT_EQ(emitted.size(), 3u);
  EXPECT_EQ(emitted[0].first, "/Books/A.epub");
  EXPECT_EQ(emitted[0].second, 1u);
  EXPECT_EQ(emitted[1].first, "/Books/B.epub");
  EXPECT_EQ(emitted[1].second, 0u);
  EXPECT_EQ(emitted[2].first, "/Books/B.epub");
  EXPECT_EQ(emitted[2].second, 1u);
  EXPECT_EQ(loadedBooks, (std::vector<std::string>{"/Books/A.epub", "/Books/B.epub"}));
}

TEST(WebClippingsApiPaging, AllBooksRouteSkipsBooksWithoutClippingsWithoutStorageErrors) {
  const std::vector<FixtureBook> books{{"/Books/Empty.epub", "Empty", 0},
                                       {"/Books/Annotated.epub", "Annotated", 2}};
  std::vector<std::string> loadedBooks;
  const auto summary = WebClippingsApi::streamAllBooksPage(
      0, 12,
      [&](auto&& visitor) { visitFixtureBooks(books, std::forward<decltype(visitor)>(visitor)); },
      [&](const std::string& path, size_t& count) { return countFixtureBook(books, path, count); },
      [&](const std::string& path, const size_t offset, const size_t limit,
          std::vector<ClippingEntry>& clippings, std::vector<std::string>& notes, size_t& totalCount) {
        loadedBooks.push_back(path);
        return loadFixturePage(books, path, offset, limit, clippings, notes, totalCount);
      },
      [](const std::string&, const std::string&, size_t, const ClippingEntry&, const std::string&) {});

  EXPECT_EQ(summary.recordsFound, 2u);
  EXPECT_EQ(summary.responseCount, 2u);
  EXPECT_EQ(summary.storageErrors, 0u);
  EXPECT_FALSE(summary.hasMore);
  EXPECT_EQ(loadedBooks, (std::vector<std::string>{"/Books/Annotated.epub"}));
}

TEST(WebClippingsApiPaging, PerBookRouteReportsMissingClipFile) {
  std::vector<ClippingEntry> clippings;
  std::vector<std::string> notes;
  const auto summary = WebClippingsApi::loadBookPage(
      "/Books/Missing.epub", 0, 12, clippings, notes,
      [](const std::string&, size_t, size_t, std::vector<ClippingEntry>&, std::vector<std::string>&, size_t&) {
        return false;
      });
  EXPECT_EQ(summary.storageErrors, 1u);
  EXPECT_EQ(summary.responseCount, 0u);
}
