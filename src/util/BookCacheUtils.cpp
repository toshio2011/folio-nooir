#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Bitmap.h>
#include <Cbz.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include "activities/reader/ProgressFile.h"

namespace {

template <typename ClearFn, typename SetupFn>
void clearCachePreservingProgress(const std::string& cachePath, const size_t maxProgressBytes, ClearFn clearFn,
                                  SetupFn setupFn) {
  uint8_t progress[6] = {};
  size_t progressBytes = 0;
  HalFile progressFile;
  if (Storage.openFileForRead("BookCache", cachePath + "/progress.bin", progressFile)) {
    const int readBytes = progressFile.read(progress, sizeof(progress));
    // EPUB historically used either 4 or 6 bytes; XTC/TXT use 4 bytes.
    if (readBytes == 4 || (readBytes == 6 && maxProgressBytes >= 6)) progressBytes = readBytes;
    progressFile.close();
  }

  if (!clearFn()) return;
  setupFn();
  if (progressBytes > 0 && !ProgressFile::writeAtomic(cachePath, progress, progressBytes)) {
    LOG_ERR("BookCache", "Could not restore reading position after cache refresh: %s", cachePath.c_str());
  }
}

}  // namespace

bool isValidBookThumbnail(const std::string& path) {
  if (path.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead("BookCache", path, file)) return false;
  Bitmap bitmap(file);
  const bool valid = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0;
  file.close();
  return valid;
}

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";
  constexpr char CBZ_PREFIX[] = "cbz_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0 ||
         strncmp(name, CBZ_PREFIX, std::size(CBZ_PREFIX) - 1) == 0;
}

void clearBookCache(const std::string& path) {
  // Book caches live in their own per-book directories. Bookmark and clipping
  // files live under /.crosspoint/bookmarks/ and /.crosspoint/clippings/;
  // clearing the cache must never remove either reading annotation store.
  if (FsHelpers::hasEpubExtension(path)) {
    Epub book(path, "/.crosspoint");
    clearCachePreservingProgress(book.getCachePath(), 6, [&book] { return book.clearCache(); },
                                  [&book] { book.setupCacheDir(); });
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc book(path, "/.crosspoint");
    clearCachePreservingProgress(book.getCachePath(), 4, [&book] { return book.clearCache(); },
                                 [&book] { book.setupCacheDir(); });
  } else if (FsHelpers::hasTxtExtension(path)) {
    Txt book(path, "/.crosspoint");
    clearCachePreservingProgress(book.getCachePath(), 4, [&book] { return book.clearCache(); },
                                 [&book] { book.setupCacheDir(); });
  } else if (FsHelpers::hasCbzExtension(path)) {
    Cbz book(path, "/.crosspoint");
    clearCachePreservingProgress(book.getCachePath(), 4, [&book] { return book.clearCache(); },
                                 [&book] { book.setupCacheDir(); });
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}
