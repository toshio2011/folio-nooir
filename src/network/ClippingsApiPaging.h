#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "ClippingEntry.h"

namespace WebClippingsApi {

constexpr size_t kPageLimit = 12;
constexpr int kMaxOffset = 100000;

struct PageWindow {
  size_t offset = 0;
  size_t limit = kPageLimit;
};

struct PageSummary {
  size_t recordsFound = 0;
  size_t responseCount = 0;
  size_t storageErrors = 0;
  bool hasMore = false;
};

inline bool makePageWindow(const int offset, const int requestedLimit, PageWindow& page) {
  if (offset < 0 || offset > kMaxOffset || requestedLimit < 1) return false;
  page.offset = static_cast<size_t>(offset);
  page.limit = std::min(static_cast<size_t>(requestedLimit), kPageLimit);
  return true;
}

// Runs the same bounded page selection used by the HTTP endpoint. The book
// visitor, per-book count, and page loader are supplied by the firmware, while
// host tests can exercise this exact selection path with a small fixture.
template <typename VisitBooks, typename CountBook, typename LoadPage, typename EmitRow>
PageSummary streamAllBooksPage(const size_t offset, const size_t limit, VisitBooks&& visitBooks,
                               CountBook&& countBook, LoadPage&& loadPage, EmitRow&& emitRow) {
  PageSummary summary;
  if (limit == 0) return summary;
  size_t remaining = offset;

  visitBooks([&](const std::string& path, const std::string& title) {
    size_t bookCount = 0;
    if (!countBook(path, bookCount)) {
      ++summary.storageErrors;
      return true;
    }
    summary.recordsFound += bookCount;
    if (bookCount == 0) return true;

    if (remaining >= bookCount) {
      remaining -= bookCount;
      return true;
    }
    if (summary.responseCount >= limit) {
      summary.hasMore = true;
      return false;
    }

    const size_t start = remaining;
    remaining = 0;
    const size_t requested = std::min(limit - summary.responseCount, bookCount - start);
    std::vector<ClippingEntry> clippings;
    std::vector<std::string> notes;
    size_t loadedCount = 0;
    if (!loadPage(path, start, requested, clippings, notes, loadedCount)) {
      ++summary.storageErrors;
      return true;
    }
    summary.recordsFound = summary.recordsFound - bookCount + loadedCount;
    for (size_t index = 0; index < clippings.size(); ++index) {
      static const std::string emptyNote;
      const std::string& note = index < notes.size() ? notes[index] : emptyNote;
      emitRow(path, title, start + index, clippings[index], note);
      ++summary.responseCount;
    }
    if (start + clippings.size() < loadedCount) {
      summary.hasMore = true;
      return false;
    }
    return true;
  });
  return summary;
}

template <typename LoadPage>
PageSummary loadBookPage(const std::string& path, const size_t offset, const size_t limit,
                         std::vector<ClippingEntry>& clippings, std::vector<std::string>& notes,
                         LoadPage&& loadPage) {
  PageSummary summary;
  size_t totalCount = 0;
  if (!loadPage(path, offset, limit, clippings, notes, totalCount)) {
    ++summary.storageErrors;
    return summary;
  }
  summary.recordsFound = totalCount;
  summary.responseCount = clippings.size();
  summary.hasMore = offset + clippings.size() < totalCount;
  return summary;
}

}  // namespace WebClippingsApi
