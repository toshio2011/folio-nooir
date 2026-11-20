#pragma once

#include <string>

#include "BookStateStore.h"
#include "RecentBooksStore.h"
#include "util/BookCacheUtils.h"

// Reset the per-book reader position first, then its shelf and statistics
// summaries. Each layer reports persistence failures to the caller.
inline bool resetBookReadingProgress(const std::string& path) {
  // Do not make the on-card position reset if the shelf history is in an
  // uncertain recovery state and cannot safely persist its matching reset.
  if (!RECENT_BOOKS.automaticWritesAllowed()) return false;
  if (!resetBookProgress(path)) return false;
  const bool bookStateSaved = BOOK_STATES.reset(path);
  const bool recentStateSaved = RECENT_BOOKS.resetReading(path);
  return bookStateSaved && recentStateSaved;
}
