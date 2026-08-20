#pragma once

#include <string>

// Clears the metadata/page cache for a book file if its extension is recognised
// (EPUB, XTC, CBZ, or TXT), while preserving the saved reading position.
// Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// A cache entry can exist as a zero-byte/invalid BMP after an interrupted or
// unsupported cover conversion. Treat it as missing so the next idle pass can
// retry instead of rendering a permanent blank cover.
bool isValidBookThumbnail(const std::string& path);
