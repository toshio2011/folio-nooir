#pragma once

#include <string>

// Clears the metadata/page cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT), while preserving the saved reading position.
// Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);
