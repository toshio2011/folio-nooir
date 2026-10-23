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

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

struct ProgressCandidate {
  const char* suffix;
  std::string path;
  std::string restoreTempPath;
  std::array<uint8_t, 6> data{};
  size_t length = 0;
  bool present = false;
};

bool isSupportedProgressLength(const uint64_t length, const size_t maxProgressBytes) {
  return length == 4 || (length == 6 && maxProgressBytes >= 6);
}

// Read each possible progress copy without using ProgressFile::openForRead(),
// which may rename a backup into place. A cache refresh must leave every valid
// recovery candidate available until the destructive directory removal has
// been authorized by a complete read.
bool readProgressCandidate(ProgressCandidate& candidate, const size_t maxProgressBytes) {
  candidate.present = Storage.exists(candidate.path.c_str());
  if (!candidate.present) return true;

  HalFile file;
  if (!Storage.openFileForRead("BookCache", candidate.path, file)) return false;
  const uint64_t fileLength = file.fileSize64();
  if (!isSupportedProgressLength(fileLength, maxProgressBytes)) {
    file.close();
    return false;
  }

  candidate.length = static_cast<size_t>(fileLength);
  const int bytesRead = file.read(candidate.data.data(), candidate.length);
  const bool closed = file.close();
  return bytesRead == static_cast<int>(candidate.length) && closed;
}

// Restore a small candidate without truncating an existing file. Writing a
// temporary sibling and renaming it keeps a short write from damaging a valid
// backup that is already present.
bool restoreCandidate(const std::string& path, const std::string& temporaryPath, const uint8_t* data,
                      const size_t length) {
  if (Storage.exists(path.c_str())) return false;
  if (Storage.exists(temporaryPath.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForWrite("BookCache", temporaryPath, file)) return false;
  const size_t written = file.write(data, length);
  file.flush();
  const bool closed = file.close();
  if (written != length || !closed) return false;

  HalFile verify;
  if (!Storage.openFileForRead("BookCache", temporaryPath, verify)) return false;
  std::array<uint8_t, 6> check{};
  const uint64_t fileLength = verify.fileSize64();
  const int bytesRead = fileLength == length ? verify.read(check.data(), length) : -1;
  const bool verifyClosed = verify.close();
  if (!verifyClosed || bytesRead != static_cast<int>(length) ||
      !std::equal(data, data + length, check.begin())) {
    return false;
  }

  return Storage.rename(temporaryPath.c_str(), path.c_str());
}

template <typename ClearFn, typename SetupFn>
void clearCachePreservingProgress(const std::string& cachePath, const size_t maxProgressBytes, ClearFn clearFn,
                                  SetupFn setupFn) {
  if (!Storage.exists(cachePath.c_str())) {
    // A false existence result can also mean that the card is temporarily
    // unavailable. The clear methods already treat an absent cache as a
    // no-op, so avoid entering their directory-removal paths here.
    return;
  }

  std::array<ProgressCandidate, 3> candidates = {ProgressCandidate{"/progress.bin"},
                                                 ProgressCandidate{"/progress.bin.bak"},
                                                 ProgressCandidate{"/progress.bin.tmp"}};
  bool hasProgressCandidate = false;
  for (auto& candidate : candidates) {
    candidate.path = cachePath + candidate.suffix;
    candidate.restoreTempPath = candidate.path + ".bookcache-restore-tmp";
    if (!readProgressCandidate(candidate, maxProgressBytes)) {
      LOG_ERR("BookCache", "Aborting cache clear; progress candidate is unreadable or invalid: %s",
              candidate.path.c_str());
      return;
    }
    hasProgressCandidate = hasProgressCandidate || candidate.present;
  }

  // Without at least one fully validated progress record, absence cannot be
  // distinguished safely from a transient SD read/lookup failure. Keep the
  // cache directory intact so a later retry can recover it.
  if (!hasProgressCandidate) {
    LOG_ERR("BookCache", "Aborting cache clear; no readable progress candidate in %s", cachePath.c_str());
    return;
  }

  const ProgressCandidate* progress = &candidates[0];
  if (!candidates[0].present) {
    progress = candidates[1].present ? &candidates[1] : &candidates[2];
  }

  if (!clearFn()) {
    // Directory removal can fail after deleting some entries. Recreate the
    // directory if needed and put back any validated progress candidates that
    // disappeared before returning the failure to the caller.
    setupFn();
    for (const auto& candidate : candidates) {
      if (candidate.present && !Storage.exists(candidate.path.c_str()) &&
          !restoreCandidate(candidate.path, candidate.restoreTempPath, candidate.data.data(), candidate.length)) {
        LOG_ERR("BookCache", "Could not restore progress after partial cache clear: %s", candidate.path.c_str());
      }
    }
    LOG_ERR("BookCache", "Cache clear failed; validated progress candidates were preserved where possible: %s",
            cachePath.c_str());
    return;
  }
  setupFn();

  // Restore a validated backup first. ProgressFile::writeAtomic() leaves this
  // fallback in place when publishing into the now-empty cache directory.
  if (candidates[1].present &&
      !restoreCandidate(candidates[1].path, candidates[1].restoreTempPath,
                        candidates[1].data.data(), candidates[1].length)) {
    LOG_ERR("BookCache", "Could not restore validated progress backup: %s", candidates[1].path.c_str());
  }

  // Keep a fallback even when the original cache had only a canonical file.
  // If publishing the canonical progress.bin fails, ProgressFile can recover
  // from this validated backup on the next open.
  if (!candidates[1].present &&
      !restoreCandidate(candidates[1].path, candidates[1].restoreTempPath,
                        progress->data.data(), progress->length)) {
    LOG_ERR("BookCache", "Could not create progress recovery backup after cache refresh: %s",
            candidates[1].path.c_str());
  }

  if (!ProgressFile::writeAtomic(cachePath, progress->data.data(), progress->length)) {
    LOG_ERR("BookCache", "Could not restore reading position after cache refresh: %s", cachePath.c_str());
  }

  // Keep a valid interrupted-write temp candidate available for manual or
  // future recovery. It is intentionally restored after writeAtomic(), which
  // uses progress.bin.tmp while publishing the canonical copy.
  if (candidates[2].present &&
      !restoreCandidate(candidates[2].path, candidates[2].restoreTempPath,
                        candidates[2].data.data(), candidates[2].length)) {
    LOG_ERR("BookCache", "Could not restore validated progress temp candidate: %s", candidates[2].path.c_str());
  }
}

}  // namespace

bool isValidBookThumbnail(const std::string& path) {
  if (path.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead("BookCache", path, file)) return false;
  Bitmap bitmap(file);
  const bool parsed = bitmap.parseHeaders() == BmpReaderError::Ok;
  const uint64_t dataEnd = static_cast<uint64_t>(file.position()) +
                           static_cast<uint64_t>(bitmap.getRowBytes()) * bitmap.getHeight();
  const bool valid = parsed && bitmap.getWidth() > 0 && bitmap.getHeight() > 0 &&
                     file.fileSize64() >= dataEnd;
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

void resetBookProgress(const std::string& path) {
  if (!FsHelpers::hasCbzExtension(path)) return;

  Cbz book(path, "/.crosspoint");
  book.setupCacheDir();
  const char* progressFiles[] = {"/progress.bin", "/progress.bin.tmp", "/progress.bin.bak"};
  for (const char* suffix : progressFiles) {
    const std::string progressPath = book.getCachePath() + suffix;
    if (!Storage.exists(progressPath.c_str())) continue;
    if (Storage.remove(progressPath.c_str())) {
      LOG_DBG("BookCache", "Reset CBZ progress: %s", progressPath.c_str());
    } else {
      LOG_ERR("BookCache", "Could not reset CBZ progress: %s", progressPath.c_str());
    }
  }
}
