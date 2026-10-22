#pragma once
#include <string>
#include <vector>

#include "../BookmarkEntry.h"

// Per-book bookmark persistence. Takes the book's path; bookmark-file path
// derivation (BookmarkUtil) and directory creation are hidden inside. The
// legacy path omits the source extension, so records for same-named formats
// can share a file; load filters typed EPUB/CBZ/XTC entries and save preserves
// other formats' rows when updating one book.
namespace BookmarkFile {

// Loads the bookmarks for bookPath. The vector is cleared first; a missing or
// empty bookmark file yields an empty list and returns false.
bool load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks);

// Saves the bookmarks for bookPath, creating the bookmarks directory as needed.
bool save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks);

}  // namespace BookmarkFile
