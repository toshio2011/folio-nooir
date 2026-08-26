#include "FolioLibraryActivity.h"

#include <Bitmap.h>
#include <Cbz.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <iterator>

#include "CrossPointSettings.h"
#include "BookStateStore.h"
#include "BookMetadataOverridesStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "fontIds.h"
#include "activities/home/SynopsisActivity.h"
#include "activities/home/ReadingStatsActivity.h"
#include "activities/home/ClockWeatherActivity.h"
#include "activities/home/ToDoListActivity.h"
#include "activities/reader/EpubReaderBookmarksActivity.h"
#include "activities/reader/EpubReaderClippingListActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookCacheUtils.h"
#include "util/CbzDiagnostics.h"
#include "util/SynopsisPreview.h"

namespace {
constexpr char RETRIEVE_ALL_BOOKS_DONE_MESSAGE[] = "Book details and missing covers prepared.";
constexpr char SEARCH_ALL_FOLDERS_LABEL[] = "Search All Folders";
constexpr char RETRIEVE_QUEUE_PATH[] = "/.crosspoint/retrieve-all.queue";
constexpr char RETRIEVE_THUMB_QUEUE_PATH[] = "/.crosspoint/retrieve-thumbs.queue";
constexpr char STOP_RETRIEVE_LABEL[] = "Stop for now";
constexpr size_t SEARCH_ENTRIES_PER_STEP = 24;

bool isLibraryBook(const std::string& path) {
  std::string filename = path;
  const size_t slash = filename.find_last_of('/');
  if (slash != std::string::npos) filename.erase(0, slash + 1);
  std::transform(filename.begin(), filename.end(), filename.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (filename == "my clippings.txt" || filename == "clippings.txt") return false;
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) ||
         FsHelpers::hasCbzExtension(path) || FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

std::string joinLibraryPath(const std::string& basepath, const std::string& name) {
  if (!name.empty() && name.front() == '/') return name;
  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += '/';
  path += name;
  if (!path.empty() && path.back() == '/') path.pop_back();
  return path;
}

std::string relativeLibraryPath(const std::string& root, const std::string& path) {
  if (root.empty() || root == "/") {
    return !path.empty() && path.front() == '/' ? path.substr(1) : path;
  }
  if (path.compare(0, root.size(), root) == 0) {
    size_t start = root.size();
    if (start < path.size() && path[start] == '/') ++start;
    return path.substr(start);
  }
  return path;
}

std::string foldLibrarySearchTerm(const std::string& query) {
  std::string folded = query;
  std::transform(folded.begin(), folded.end(), folded.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return folded;
}

bool containsLibrarySearchTerm(const std::string& name, const std::string& foldedQuery) {
  if (foldedQuery.empty()) return true;
  if (foldedQuery.size() > name.size()) return false;
  for (size_t offset = 0; offset + foldedQuery.size() <= name.size(); ++offset) {
    bool match = true;
    for (size_t i = 0; i < foldedQuery.size(); ++i) {
      const char foldedChar = static_cast<char>(std::tolower(static_cast<unsigned char>(name[offset + i])));
      if (foldedChar != foldedQuery[i]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void removePartialRetrieveThumbnail(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    Storage.remove(UITheme::getCoverThumbPath(epub.getThumbBmpPath(), FolioNooirTheme::COVER_HEIGHT).c_str());
    Storage.remove((epub.getCachePath() + "/metadata.bin.tmp").c_str());
    Storage.remove((epub.getCachePath() + "/.cover.jpg").c_str());
    Storage.remove((epub.getCachePath() + "/.cover.png").c_str());
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    Storage.remove(UITheme::getCoverThumbPath(xtc.getThumbBmpPath(), FolioNooirTheme::COVER_HEIGHT).c_str());
  } else if (FsHelpers::hasCbzExtension(path)) {
    Cbz cbz(path, "/.crosspoint");
    const std::string thumb = UITheme::getCoverThumbPath(cbz.getThumbBmpPath(), FolioNooirTheme::COVER_HEIGHT);
    // Do not remove a valid persistent shelf thumbnail when stopping a
    // retrieval. Only an incomplete/invalid output is disposable.
    logCbzCacheLookup(thumb, Storage.exists(thumb.c_str()));
    if (!isValidBookThumbnail(thumb)) {
      logCbzCacheAction("remove", "stop_incomplete_thumbnail", thumb);
      Storage.remove(thumb.c_str());
    }
  }
}

void removePartialRetrieveMetadata(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    Storage.remove((epub.getCachePath() + "/metadata.bin.tmp").c_str());
    Storage.remove((epub.getCachePath() + "/.cover.jpg").c_str());
    Storage.remove((epub.getCachePath() + "/.cover.png").c_str());
  } else if (FsHelpers::hasCbzExtension(path)) {
    Cbz cbz(path, "/.crosspoint");
    const std::string tempPath = cbz.getCachePath() + "/metadata.bin.tmp";
    if (Storage.exists(tempPath.c_str())) {
      logCbzCacheAction("remove", "stop_incomplete_metadata", tempPath);
      Storage.remove(tempPath.c_str());
    }
  }
}

enum class LibraryBookState : uint8_t { Reading, Unread, OnHold, Finished };

LibraryBookState getLibraryBookState(const std::string& path) {
  const BookState* state = BOOK_STATES.find(path);
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  const auto recentIt = std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) {
    return book.path == path;
  });
  const uint8_t progress = state ? state->progressPercent
                                 : (recentIt == recentBooks.end() ? 0 : recentIt->progressPercent);
  if ((state && state->status == BookStatus::Finished) || progress >= 100) return LibraryBookState::Finished;
  if (state && state->status == BookStatus::OnHold) return LibraryBookState::OnHold;
  if ((state && state->status == BookStatus::Reading) || progress > 0) return LibraryBookState::Reading;
  return LibraryBookState::Unread;
}
}  // namespace

void FolioLibraryActivity::loadFiles() {
  allFiles.clear();
  allFiles.reserve(64);
  HalFile root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory() || !fileNameBuffer) return;
  root.rewindDirectory();
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }
    if (entry.isDirectory()) {
      allFiles.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      const std::string_view name(fileNameBuffer.get());
      if (FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasCbzExtension(name) ||
          FsHelpers::hasTxtExtension(name) ||
          FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name) ||
          FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name)) {
        allFiles.emplace_back(name);
      }
    }
  }
  FsHelpers::sortFileList(allFiles);
  applyLibraryFilter();
}

bool FolioLibraryActivity::matchesLibraryFilter(const std::string& name) const {
  if (!searchQueryFolded.empty() && !containsLibrarySearchTerm(name, searchQueryFolded)) return false;
  if (libraryFilter == LibraryFilter::All || (!name.empty() && name.back() == '/')) return true;
  const std::string path = joinLibraryPath(basepath, name);
  if (!isLibraryBook(path)) return false;
  const LibraryBookState state = getLibraryBookState(path);
  switch (libraryFilter) {
    case LibraryFilter::Reading:
      return state == LibraryBookState::Reading;
    case LibraryFilter::Unread:
      return state == LibraryBookState::Unread;
    case LibraryFilter::OnHold:
      return state == LibraryBookState::OnHold;
    case LibraryFilter::Finished:
      return state == LibraryBookState::Finished;
    case LibraryFilter::All:
      return true;
  }
  return true;
}

void FolioLibraryActivity::applyLibraryFilter() {
  files.clear();
  files.reserve(allFiles.size());
  for (const auto& name : allFiles) {
    if (matchesLibraryFilter(name)) files.push_back(name);
  }
  selectorIndex = 0;
  observedSelectorIndex = SIZE_MAX;
  retrievingMetadata = false;
  retrievingMetadataProgress = 0;
  retrievingMetadataCleanupOnCancel = false;
  retrievingMetadataIndex = SIZE_MAX;
  retrievingPopupRendered = false;
  forceMetadataRefresh = false;
  forceMetadataRefreshIndex = SIZE_MAX;
  resetPreviews();
}

FolioLibrarySummary FolioLibraryActivity::getLibrarySummary() const {
  FolioLibrarySummary summary;
  for (const auto& name : allFiles) {
    if (!name.empty() && name.back() == '/') continue;
    const std::string path = joinLibraryPath(basepath, name);
    if (!isLibraryBook(path)) continue;
    ++summary.total;
    switch (getLibraryBookState(path)) {
      case LibraryBookState::Reading:
        ++summary.reading;
        break;
      case LibraryBookState::Unread:
        ++summary.unread;
        break;
      case LibraryBookState::OnHold:
        ++summary.onHold;
        break;
      case LibraryBookState::Finished:
        ++summary.finished;
        break;
    }
  }
  return summary;
}

void FolioLibraryActivity::resetPreviews() {
  for (auto& preview : previews) preview = Preview{};
  previewPageStart = (selectorIndex / PAGE_SIZE) * PAGE_SIZE;
  nextPreviewSlot = 0;
}

std::string FolioLibraryActivity::fullPath(const size_t index) const {
  if (index >= files.size()) return {};
  if (recursiveSearchMode && !recursiveSearchRoot.empty()) {
    return joinLibraryPath(recursiveSearchRoot, files[index]);
  }
  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += '/';
  path += files[index];
  if (!path.empty() && path.back() == '/') path.pop_back();
  return path;
}

void FolioLibraryActivity::loadNextPreview() {
  while (nextPreviewSlot < PAGE_SIZE) {
    const size_t slot = nextPreviewSlot++;
    const size_t index = previewPageStart + slot;
    if (index >= files.size()) return;
    Preview& preview = previews[slot];
    preview.fileIndex = index;
    preview.directory = !files[index].empty() && files[index].back() == '/';
    preview.title = files[index];
    if (preview.directory && !preview.title.empty()) preview.title.pop_back();
    if (!preview.directory) {
      const std::string path = fullPath(index);
      if (FsHelpers::hasCbzExtension(path)) {
        const size_t dot = preview.title.find_last_of('.');
        if (dot != std::string::npos) preview.title.resize(dot);
      }
      const auto& recent = RECENT_BOOKS.getBooks();
      const auto recentIt = std::find_if(recent.begin(), recent.end(), [&](const RecentBook& book) {
        return book.path == path;
      });
      if (recentIt != recent.end()) {
        preview.progressPercent = recentIt->progressPercent;
        if (!recentIt->title.empty()) preview.title = recentIt->title;
        preview.author = recentIt->author;
        preview.synopsis = recentIt->synopsis;
        preview.coverBmpPath = recentIt->coverBmpPath;
        const bool cachedThumbAvailable =
            !preview.coverBmpPath.empty() &&
            isValidBookThumbnail(UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT));
        preview.metadataAttempted = !preview.title.empty() && cachedThumbAvailable;
      }
      if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path)) {
        if (!overrideData->title.empty()) preview.title = overrideData->title;
        preview.author = overrideData->author;
        preview.synopsis = overrideData->synopsis;
      }
      // Images are viewable files, not books. Do not show the metadata
      // retrieval popup when the highlight lands on a PNG/JPEG/BMP.
      if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
        preview.metadataAttempted = true;
      }
    }
    preview.loaded = true;
    // Prepare one item per loop so input remains responsive, but refresh the
    // e-ink panel only once after the visible page is complete.
    if (nextPreviewSlot >= PAGE_SIZE || previewPageStart + nextPreviewSlot >= files.size()) requestUpdate();
    return;  // Exactly one visible item per loop.
  }
}

FolioLibraryActivity::RetrieveCacheStatus FolioLibraryActivity::inspectRetrieveCache(const std::string& path) {
  RetrieveCacheStatus status;
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    status.metadataValid = epub.loadCachedMetadataOnly();
    if (status.metadataValid) {
      status.title = epub.getTitle();
      status.author = epub.getAuthor();
      status.synopsis = epub.getDescription();
    }
    status.thumbnailPath = epub.getThumbBmpPath();
    status.thumbnailValid =
        isValidBookThumbnail(UITheme::getCoverThumbPath(status.thumbnailPath, FolioNooirTheme::COVER_HEIGHT));
    return status;
  }
  if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    status.thumbnailPath = xtc.getThumbBmpPath();
    status.thumbnailValid =
        isValidBookThumbnail(UITheme::getCoverThumbPath(status.thumbnailPath, FolioNooirTheme::COVER_HEIGHT));
    // XTC has no separate lightweight metadata file. Its valid shelf
    // thumbnail remains the existing cache-validity signal.
    status.metadataValid = status.thumbnailValid;
    return status;
  }
  if (FsHelpers::hasCbzExtension(path)) {
    Cbz cbz(path, "/.crosspoint");
    status.metadataValid = cbz.loadCachedMetadataOnly();
    if (status.metadataValid) {
      status.title = cbz.getTitle();
      status.author = cbz.getAuthor();
      status.synopsis = cbz.getSynopsis();
    }
    status.thumbnailPath = cbz.getThumbBmpPath();
    const std::string thumb = UITheme::getCoverThumbPath(status.thumbnailPath, FolioNooirTheme::COVER_HEIGHT);
    logCbzCacheLookup(thumb, Storage.exists(thumb.c_str()));
    status.thumbnailValid = isValidBookThumbnail(thumb);
    return status;
  }
  status.metadataValid = true;
  status.thumbnailValid = true;
  return status;
}

void FolioLibraryActivity::refreshSelectedPreviewFromCache(const std::string& path,
                                                            const RetrieveCacheStatus* status) {
  // Retrieve All processes books that may not be in RecentBooksStore yet.
  // Refresh only the highlighted shelf slot from the cache just written; do
  // not clear/rebuild the other seven visible previews.
  if (selectorIndex >= files.size() || selectorIndex < previewPageStart ||
      selectorIndex >= previewPageStart + PAGE_SIZE || path != retrieveAllSelectedPath)
    return;

  Preview& preview = previews[selectorIndex - previewPageStart];
  if (FsHelpers::hasEpubExtension(path)) {
    if (status && status->metadataValid) {
      if (!status->title.empty()) preview.title = status->title;
      preview.author = status->author;
      if (!status->synopsis.empty()) preview.synopsis = status->synopsis;
      preview.coverBmpPath = status->thumbnailPath;
      preview.metadataAttempted = true;
    } else {
      Epub epub(path, "/.crosspoint");
      if (epub.loadCachedMetadataOnly()) {
        if (!epub.getTitle().empty()) preview.title = epub.getTitle();
        preview.author = epub.getAuthor();
        if (!epub.getDescription().empty()) preview.synopsis = epub.getDescription();
        preview.coverBmpPath = epub.getThumbBmpPath();
        preview.metadataAttempted = true;
      }
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    std::string thumb;
    if (status) {
      thumb = status->thumbnailPath;
    } else {
      Xtc xtc(path, "/.crosspoint");
      thumb = xtc.getThumbBmpPath();
    }
    const bool thumbnailValid = status ? status->thumbnailValid
                                       : isValidBookThumbnail(UITheme::getCoverThumbPath(thumb, FolioNooirTheme::COVER_HEIGHT));
    if (thumbnailValid) {
      preview.coverBmpPath = thumb;
      preview.metadataAttempted = true;
    }
  } else if (FsHelpers::hasCbzExtension(path)) {
    Cbz cbz(path, "/.crosspoint");
    if (status && status->metadataValid) {
      if (!status->title.empty()) preview.title = status->title;
      preview.author = status->author;
      preview.synopsis = status->synopsis;
      preview.coverBmpPath = status->thumbnailPath;
      preview.metadataAttempted = true;
    } else if (cbz.loadCachedMetadataOnly()) {
      if (!cbz.getTitle().empty()) preview.title = cbz.getTitle();
      preview.author = cbz.getAuthor();
      preview.synopsis = cbz.getSynopsis();
      preview.coverBmpPath = cbz.getThumbBmpPath();
      preview.metadataAttempted = true;
    }
  }
  if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path)) {
    if (!overrideData->title.empty()) preview.title = overrideData->title;
    preview.author = overrideData->author;
    preview.synopsis = overrideData->synopsis;
  }
}

void FolioLibraryActivity::startRetrieveAllBooks() {
  if (retrieveQueueOpen || retrieveQueueReading) retrieveQueueFile.close();
  if (retrieveThumbnailQueueWriting || retrieveThumbnailQueueReading) retrieveThumbnailQueueFile.close();
  if (retrieveScanDirectoryOpen) retrieveScanDirectory.close();
  retrieveQueueOpen = false;
  retrieveQueueReading = false;
  retrieveThumbnailQueueWriting = false;
  retrieveThumbnailQueueReading = false;
  retrieveScanDirectoryOpen = false;
  retrieveScanDirectoryPath.clear();
  Storage.remove(RETRIEVE_QUEUE_PATH);
  Storage.remove(RETRIEVE_THUMB_QUEUE_PATH);
  retrieveDirectories.clear();
  retrieveDirectories.reserve(16);
  retrieveDirectories.emplace_back("/");
  retrieveDirectoryIndex = 0;
  retrieveAllStage = RetrieveAllStage::Scanning;
  retrieveAllTotal = 0;
  retrieveAllProcessed = 0;
  retrieveAllThumbnailTotal = 0;
  retrieveAllThumbnailProcessed = 0;
  retrieveAllSelectedPath = selectorIndex < files.size() ? fullPath(selectorIndex) : std::string{};
  retrieveAllSelectedNeedsThumbnail = false;
  retrieveAllPriorityPending = false;
  retrieveAllPriorityDone = false;
  retrieveAllCurrentFromPriority = false;
  retrieveAllCurrentPath.clear();
  retrieveAllCurrentReady = false;
  retrieveAllCurrentMetadataCacheValid = false;
  retrieveAllCurrentThumbnailCacheValid = false;
  retrieveAllNextUiUpdateMs = 0;
  retrieveAllLastHalfRefreshProcessed = 0;
  retrieveAllCurrentStartedMs = 0;
  retrieveAllStartedMs = millis();
  retrieveAllMetadataCacheHits = 0;
  retrieveAllMetadataCacheMisses = 0;
  retrieveAllThumbnailCacheHits = 0;
  retrieveAllThumbnailCacheMisses = 0;
  retrieveAllProcessingBook.store(false);
  retrieveAllStatusMessage.clear();
  retrieveQueueOpen = Storage.openFileForWrite("FLIB", RETRIEVE_QUEUE_PATH, retrieveQueueFile);
  if (!retrieveQueueOpen) {
    finishRetrieveAllBooks("Could not start book retrieval.");
    return;
  }
  retrievingAllBooks = true;
  retrievingAllBooksPopupRendered = false;
  retrieveAllComplete = false;
  retrieveAllCompleteUntilMs = 0;
  LOG_DBG("PERF", "RetrieveAll start");
  requestUpdate(true);
}

void FolioLibraryActivity::processRetrieveAllBooks() {
  if (!retrievingAllBooks || !retrievingAllBooksPopupRendered) return;

  // First pass only records paths and counts books.  The queue is streamed to
  // SD so hundreds (or thousands) of books cannot exhaust the X3 heap.
  if (retrieveAllStage == RetrieveAllStage::Scanning) {
    constexpr size_t SCAN_ENTRIES_PER_STEP = 24;
    if (!retrieveScanDirectoryOpen) {
      if (retrieveDirectoryIndex >= retrieveDirectories.size()) {
        if (retrieveQueueOpen) retrieveQueueFile.close();
        retrieveQueueOpen = false;
        retrieveQueueReading = Storage.openFileForRead("FLIB", RETRIEVE_QUEUE_PATH, retrieveQueueFile);
        if (!retrieveQueueReading) {
          finishRetrieveAllBooks("Could not open book retrieval queue.");
          return;
        }
        retrieveThumbnailQueueWriting =
            Storage.openFileForWrite("FLIB", RETRIEVE_THUMB_QUEUE_PATH, retrieveThumbnailQueueFile);
        if (!retrieveThumbnailQueueWriting) {
          finishRetrieveAllBooks("Could not open thumbnail queue.");
          return;
        }
        retrieveAllStage = RetrieveAllStage::Metadata;
        retrieveAllProcessed = 0;
        retrieveAllStatusMessage.clear();
        requestUpdate(true);
        return;
      }
      retrieveScanDirectoryPath = retrieveDirectories[retrieveDirectoryIndex++];
      retrieveScanDirectory = Storage.open(retrieveScanDirectoryPath.c_str());
      if (!retrieveScanDirectory || !retrieveScanDirectory.isDirectory() || !fileNameBuffer) return;
      retrieveScanDirectory.rewindDirectory();
      retrieveScanDirectoryOpen = true;
    }

    size_t scanned = 0;
    while (scanned++ < SCAN_ENTRIES_PER_STEP) {
      HalFile entry = retrieveScanDirectory.openNextFile();
      if (!entry) {
        retrieveScanDirectory.close();
        retrieveScanDirectoryOpen = false;
        retrieveScanDirectoryPath.clear();
        break;
      }
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      const std::string name(fileNameBuffer.get());
      if (name.empty() || name == "." || name == ".." || name == "System Volume Information" ||
          (!SETTINGS.showHiddenFiles && name.front() == '.')) {
        continue;
      }
      const std::string path = joinLibraryPath(retrieveScanDirectoryPath, name);
      if (entry.isDirectory()) {
        if (name == ".crosspoint" || name == ".sleep") continue;
        retrieveDirectories.push_back(path);
      } else if (FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) ||
                 FsHelpers::hasCbzExtension(name)) {
        if (retrieveQueueOpen) {
          retrieveQueueFile.write(reinterpret_cast<const uint8_t*>(path.c_str()), path.size());
          retrieveQueueFile.write(static_cast<uint8_t>('\n'));
        }
        ++retrieveAllTotal;
      }
    }
    if (retrieveScanDirectoryOpen && millis() >= retrieveAllNextUiUpdateMs) {
      // Scanning is intentionally throttled. Redrawing the e-ink panel for
      // every small SD batch makes Retrieve All slower without adding useful
      // information; the popup still updates several times per second.
      retrieveAllNextUiUpdateMs = millis() + 500;
      requestUpdate();
    }
    return;
  }

  // Metadata phase has consumed the main queue. Rewind into a second phase
  // that contains only books whose shelf thumbnail was missing or invalid.
  if (retrieveAllStage == RetrieveAllStage::Metadata && retrieveAllProcessed >= retrieveAllTotal) {
    if (retrieveQueueReading) retrieveQueueFile.close();
    retrieveQueueReading = false;
    if (retrieveThumbnailQueueWriting) retrieveThumbnailQueueFile.close();
    retrieveThumbnailQueueWriting = false;
    if (retrieveAllThumbnailTotal == 0) {
      finishRetrieveAllBooks(RETRIEVE_ALL_BOOKS_DONE_MESSAGE);
      return;
    }
    retrieveThumbnailQueueReading =
        Storage.openFileForRead("FLIB", RETRIEVE_THUMB_QUEUE_PATH, retrieveThumbnailQueueFile);
    if (!retrieveThumbnailQueueReading) {
      finishRetrieveAllBooks("Could not open thumbnail queue.");
      return;
    }
    retrieveAllStage = RetrieveAllStage::Thumbnails;
    retrieveAllProcessed = 0;
    retrieveAllThumbnailProcessed = 0;
    retrieveAllLastHalfRefreshProcessed = 0;
    retrieveAllPriorityPending = retrieveAllSelectedNeedsThumbnail;
    retrieveAllPriorityDone = false;
    retrieveAllCurrentFromPriority = false;
    retrieveAllCurrentPath.clear();
    retrieveAllCurrentReady = false;
    retrievingAllBooksPopupRendered = false;
    requestUpdate(true);
    return;
  }

  if (retrieveAllStage == RetrieveAllStage::Metadata && retrieveQueueReading) {
    // Read one path, paint it, and return to the loop before opening/parsing
    // the book. This gives Back a real opportunity to stop between books.
    if (!retrieveAllCurrentReady) {
      retrieveAllCurrentPath.clear();
      while (retrieveQueueFile.available()) {
        const int value = retrieveQueueFile.read();
        if (value < 0 || value == '\n') break;
        if (value != '\r' && retrieveAllCurrentPath.size() < NAME_BUFFER_SIZE - 1)
          retrieveAllCurrentPath.push_back(static_cast<char>(value));
      }
      if (retrieveAllCurrentPath.empty()) {
        ++retrieveAllProcessed;
        return;
      }
      // Inspect metadata and the shelf thumbnail once for this queue item.
      // The result is retained only until the current item finishes, avoiding
      // duplicate Epub/cache construction without adding a library-wide index.
      const RetrieveCacheStatus cache = inspectRetrieveCache(retrieveAllCurrentPath);
      retrieveAllCurrentMetadataCacheValid = cache.metadataValid;
      retrieveAllCurrentThumbnailCacheValid = cache.thumbnailValid;
      if (cache.metadataValid) {
        ++retrieveAllMetadataCacheHits;
      } else {
        ++retrieveAllMetadataCacheMisses;
      }
      if (cache.thumbnailValid) {
        ++retrieveAllThumbnailCacheHits;
      } else {
        ++retrieveAllThumbnailCacheMisses;
      }

      // A valid metadata cache avoids ZIP parsing, but a missing thumbnail is
      // still added to the small second-phase queue.
      if (cache.metadataValid) {
        if (!cache.thumbnailValid) {
          if (retrieveThumbnailQueueWriting) {
            retrieveThumbnailQueueFile.write(reinterpret_cast<const uint8_t*>(retrieveAllCurrentPath.c_str()),
                                              retrieveAllCurrentPath.size());
            retrieveThumbnailQueueFile.write(static_cast<uint8_t>('\n'));
          }
          ++retrieveAllThumbnailTotal;
          if (retrieveAllCurrentPath == retrieveAllSelectedPath) retrieveAllSelectedNeedsThumbnail = true;
        }
        refreshSelectedPreviewFromCache(retrieveAllCurrentPath, &cache);
        ++retrieveAllProcessed;
        if (millis() >= retrieveAllNextUiUpdateMs) {
          retrieveAllNextUiUpdateMs = millis() + 500;
          requestUpdate();
        }
        return;
      }
      retrieveAllCurrentReady = true;
      retrievingAllBooksPopupRendered = false;
      requestUpdate(true);
      return;
    }
    const std::string path = retrieveAllCurrentPath;
    retrieveAllCurrentStartedMs = millis();
    retrieveAllProcessingBook.store(true);
    LOG_INF("FLIB", "Retrieve All metadata %lu/%lu: %s", static_cast<unsigned long>(retrieveAllProcessed + 1),
            static_cast<unsigned long>(retrieveAllTotal), path.c_str());
    requestUpdate(true);
    bool metadataReady = false;
    RetrieveCacheStatus processedCache;
    bool processedCacheValid = false;
    if (FsHelpers::hasEpubExtension(path)) {
      Epub epub(path, "/.crosspoint");
      metadataReady = epub.loadMetadataOnly();
      if (metadataReady) {
        processedCache.metadataValid = true;
        processedCache.title = epub.getTitle();
        processedCache.author = epub.getAuthor();
        processedCache.synopsis = epub.getDescription();
        processedCache.thumbnailPath = epub.getThumbBmpPath();
        processedCacheValid = true;
      }
    } else if (FsHelpers::hasXtcExtension(path)) {
      // XTC has no separate metadata.bin. Its thumbnail phase opens the XTC
      // once when a thumbnail is missing, so metadata remains filename-first.
      metadataReady = true;
    } else if (FsHelpers::hasCbzExtension(path)) {
      Cbz cbz(path, "/.crosspoint");
      metadataReady = cbz.loadMetadataOnly();
      if (metadataReady) {
        processedCache.metadataValid = true;
        processedCache.title = cbz.getTitle();
        processedCache.author = cbz.getAuthor();
        processedCache.synopsis = cbz.getSynopsis();
        processedCache.thumbnailPath = cbz.getThumbBmpPath();
        processedCacheValid = true;
      }
    }
    if (metadataReady && !retrieveAllCurrentThumbnailCacheValid) {
      if (retrieveThumbnailQueueWriting) {
        retrieveThumbnailQueueFile.write(reinterpret_cast<const uint8_t*>(path.c_str()), path.size());
        retrieveThumbnailQueueFile.write(static_cast<uint8_t>('\n'));
      }
      ++retrieveAllThumbnailTotal;
      if (path == retrieveAllSelectedPath) retrieveAllSelectedNeedsThumbnail = true;
    }
    retrieveAllProcessingBook.store(false);
    LOG_DBG("PERF", "RetrieveAll metadata book=%s cache=miss elapsed=%lums", path.c_str(),
            static_cast<unsigned long>(millis() - retrieveAllCurrentStartedMs));
    LOG_INF("FLIB", "Retrieve All metadata finished: %s", path.c_str());
    refreshSelectedPreviewFromCache(path, processedCacheValid ? &processedCache : nullptr);
    ++retrieveAllProcessed;
    retrieveAllCurrentReady = false;
    retrieveAllCurrentPath.clear();
    retrieveAllCurrentMetadataCacheValid = false;
    retrieveAllCurrentThumbnailCacheValid = false;
    return;
  }

  if (retrieveAllStage == RetrieveAllStage::Thumbnails && retrieveThumbnailQueueReading) {
    if (!retrieveAllCurrentReady) {
      retrieveAllCurrentFromPriority = false;
      if (retrieveAllPriorityPending) {
        retrieveAllCurrentPath = retrieveAllSelectedPath;
        retrieveAllPriorityPending = false;
        retrieveAllCurrentFromPriority = true;
      } else {
        retrieveAllCurrentPath.clear();
        while (retrieveThumbnailQueueFile.available()) {
          const int value = retrieveThumbnailQueueFile.read();
          if (value < 0 || value == '\n') break;
          if (value != '\r' && retrieveAllCurrentPath.size() < NAME_BUFFER_SIZE - 1)
            retrieveAllCurrentPath.push_back(static_cast<char>(value));
        }
        if (retrieveAllCurrentPath.empty()) {
          finishRetrieveAllBooks(RETRIEVE_ALL_BOOKS_DONE_MESSAGE);
          return;
        }
        // The selected book was processed first. Consume its queued duplicate
        // without decoding it a second time.
        if (retrieveAllPriorityDone && retrieveAllCurrentPath == retrieveAllSelectedPath) return;
      }
      retrieveAllCurrentReady = true;
      retrievingAllBooksPopupRendered = false;
      requestUpdate(true);
      return;
    }
    const std::string path = retrieveAllCurrentPath;
    retrieveAllCurrentStartedMs = millis();
    retrieveAllProcessingBook.store(true);
    LOG_INF("FLIB", "Retrieve All thumbnail %lu/%lu: %s", static_cast<unsigned long>(retrieveAllThumbnailProcessed + 1),
            static_cast<unsigned long>(retrieveAllThumbnailTotal), path.c_str());
    requestUpdate(true);
    if (FsHelpers::hasEpubExtension(path)) {
      Epub epub(path, "/.crosspoint");
      bool metadataReady = epub.loadCachedMetadataOnly();
      if (!metadataReady) metadataReady = epub.loadMetadataOnly();
      if (metadataReady) {
        const std::string thumb = epub.getThumbBmpPath(FolioNooirTheme::COVER_HEIGHT);
        const size_t coverBytes = epub.getCoverImageSize();
        constexpr size_t MAX_THUMBNAIL_SOURCE_BYTES = 512u * 1024u;
        if (!isValidBookThumbnail(thumb) && coverBytes <= MAX_THUMBNAIL_SOURCE_BYTES) {
          Storage.remove(thumb.c_str());
          epub.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
        } else if (coverBytes > MAX_THUMBNAIL_SOURCE_BYTES) {
          LOG_DBG("FLIB", "Skipping oversized thumbnail source (%lu bytes): %s",
                  static_cast<unsigned long>(coverBytes), path.c_str());
        }
      }
    } else if (FsHelpers::hasXtcExtension(path)) {
      Xtc xtc(path, "/.crosspoint");
      const std::string thumb = xtc.getThumbBmpPath(FolioNooirTheme::COVER_HEIGHT);
      if (!isValidBookThumbnail(thumb) && xtc.load()) {
        Storage.remove(thumb.c_str());
        xtc.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
      }
    } else if (FsHelpers::hasCbzExtension(path)) {
      Cbz cbz(path, "/.crosspoint");
      bool metadataReady = cbz.loadCachedMetadataOnly();
      if (!metadataReady) metadataReady = cbz.loadMetadataOnly();
      if (metadataReady) {
        const std::string thumb = cbz.getThumbBmpPath(FolioNooirTheme::COVER_HEIGHT);
        if (!isValidBookThumbnail(thumb)) {
          cbz.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
        }
      }
    }
    retrieveAllProcessingBook.store(false);
    LOG_DBG("PERF", "RetrieveAll thumbnail book=%s elapsed=%lums", path.c_str(),
            static_cast<unsigned long>(millis() - retrieveAllCurrentStartedMs));
    LOG_INF("FLIB", "Retrieve All thumbnail finished: %s", path.c_str());
    refreshSelectedPreviewFromCache(path);
    ++retrieveAllThumbnailProcessed;
    if (retrieveAllCurrentFromPriority) retrieveAllPriorityDone = true;
    retrieveAllCurrentReady = false;
    retrieveAllCurrentPath.clear();
    retrieveAllCurrentFromPriority = false;
    retrieveAllCurrentMetadataCacheValid = false;
    retrieveAllCurrentThumbnailCacheValid = false;
    requestUpdate();
    return;
  }

  finishRetrieveAllBooks(RETRIEVE_ALL_BOOKS_DONE_MESSAGE);
}

void FolioLibraryActivity::finishRetrieveAllBooks(const char* message, const bool showCompletion) {
  if (retrieveAllStartedMs != 0) {
    LOG_INF("PERF",
            "RetrieveAll complete books=%lu metadata_hits=%lu metadata_misses=%lu thumbnail_hits=%lu "
            "thumbnail_misses=%lu elapsed=%lums status=%s",
            static_cast<unsigned long>(retrieveAllTotal),
            static_cast<unsigned long>(retrieveAllMetadataCacheHits),
            static_cast<unsigned long>(retrieveAllMetadataCacheMisses),
            static_cast<unsigned long>(retrieveAllThumbnailCacheHits),
            static_cast<unsigned long>(retrieveAllThumbnailCacheMisses),
            static_cast<unsigned long>(millis() - retrieveAllStartedMs), message ? message : "finished");
  }
  retrieveAllProcessingBook.store(false);
  if (retrieveQueueOpen || retrieveQueueReading) retrieveQueueFile.close();
  if (retrieveThumbnailQueueWriting || retrieveThumbnailQueueReading) retrieveThumbnailQueueFile.close();
  if (retrieveScanDirectoryOpen) retrieveScanDirectory.close();
  retrieveQueueOpen = false;
  retrieveQueueReading = false;
  retrieveThumbnailQueueWriting = false;
  retrieveThumbnailQueueReading = false;
  retrieveScanDirectoryOpen = false;
  retrieveScanDirectoryPath.clear();
  Storage.remove(RETRIEVE_QUEUE_PATH);
  Storage.remove(RETRIEVE_THUMB_QUEUE_PATH);
  retrievingAllBooks = false;
  retrievingAllBooksPopupRendered = false;
  retrieveAllStatusMessage = message ? message : "";
  // A normal completion gets a short confirmation screen.  Stopping should
  // return to the library immediately; otherwise the old confirmation popup
  // can look like the stop button is stuck on an e-ink panel.
  retrieveAllComplete = showCompletion;
  retrieveAllCompleteUntilMs = showCompletion ? millis() + 2200 : 0;
  retrieveDirectories.clear();
  retrieveAllSelectedPath.clear();
  retrieveAllSelectedNeedsThumbnail = false;
  retrieveAllPriorityPending = false;
  retrieveAllPriorityDone = false;
  retrieveAllCurrentFromPriority = false;
  retrieveAllCurrentPath.clear();
  retrieveAllCurrentReady = false;
  retrieveAllCurrentMetadataCacheValid = false;
  retrieveAllCurrentThumbnailCacheValid = false;
  retrieveAllNextUiUpdateMs = 0;
  retrieveAllLastHalfRefreshProcessed = 0;
  retrieveAllCurrentStartedMs = 0;
  retrieveAllStartedMs = 0;
  requestUpdate(true);
}

void FolioLibraryActivity::cancelRetrieveAllBooks() {
  // A stopped thumbnail may be a partial/invalid BMP. Remove only that
  // presentation asset; completed metadata, reading progress, bookmarks, and
  // clippings remain available for the next resumable run.
  const std::string current = retrieveAllCurrentReady ? retrieveAllCurrentPath : std::string{};
  if (!current.empty()) {
    if (retrieveAllStage == RetrieveAllStage::Thumbnails) {
      removePartialRetrieveThumbnail(current);
    } else {
      removePartialRetrieveMetadata(current);
    }
  }
  finishRetrieveAllBooks("Book retrieval stopped for now.", false);
}

void FolioLibraryActivity::showMenu() {
  std::vector<std::string> options = {tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE), tr(STR_CLOCK_WEATHER),
                                      tr(STR_TODO_LIST), tr(STR_READING_STATS),
                                      "Retrieve All Book Details", "Bookmarks (all books)", "Clippings (all books)",
                                      searchQuery.empty() ? std::string(tr(STR_SEARCH))
                                                          : std::string(tr(STR_CLEAR_BUTTON)) + " " + tr(STR_SEARCH),
                                      SEARCH_ALL_FOLDERS_LABEL};
  menuPopup.show(StrId::STR_MENU, options, 0, [this](const int index) {
    // Always close the menu before starting another activity. This prevents
    // the popup from surviving a Settings replacement or a child activity.
    menuPopup.dismiss();
    if (index == 0) {
      activityManager.goToFileTransfer();
    } else if (index == 1) {
      activityManager.goToSettings();
    } else if (index == 2) {
      startActivityForResult(std::make_unique<ClockWeatherActivity>(renderer, mappedInput), nullptr);
    } else if (index == 3) {
      startActivityForResult(std::make_unique<ToDoListActivity>(renderer, mappedInput), nullptr);
    } else if (index == 4) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), nullptr);
    } else if (index == 5) {
      startRetrieveAllBooks();
    } else if (index == 6) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, std::string{}, true),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
    } else if (index == 7) {
      startActivityForResult(std::make_unique<EpubReaderClippingListActivity>(
                                renderer, mappedInput, std::string{}, "All books", true),
                            nullptr);
    } else if (index == 8) {
      if (searchQuery.empty()) {
        launchSearch(false);
      } else {
        clearSearch();
        requestUpdate(true);
      }
    } else if (index == 9) {
      launchSearch(true);
    }
  });
  requestUpdate();
}

void FolioLibraryActivity::launchSearch(const bool recursive) {
  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  startActivityForResult(std::move(keyboard), [this, recursive](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
    if (!keyboardResult) return;
    searchQuery = keyboardResult->text;
    searchQueryFolded = foldLibrarySearchTerm(searchQuery);
    if (searchQuery.empty()) {
      clearSearch();
    } else if (recursive) {
      startRecursiveSearch();
    } else {
      // Search is deliberately filename-only and local to the current folder.
      // Applying it is an in-memory filter; no metadata or cover extraction runs.
      recursiveSearchMode = false;
      applyLibraryFilter();
    }
    requestUpdate(true);
  });
}

void FolioLibraryActivity::startRecursiveSearch() {
  recursiveSearchMode = true;
  recursiveSearchActive = true;
  // “Search All Folders” covers the whole SD-card library, not only the
  // folder that happened to be open when the menu was pressed.
  recursiveSearchRoot = "/";
  recursiveSearchDirectories.clear();
  recursiveSearchMatches.clear();
  recursiveSearchDirectories.push_back(recursiveSearchRoot);
  recursiveSearchDirectoryIndex = 0;
  recursiveSearchScannedEntries = 0;
  recursiveSearchNextUiUpdateMs = 0;
  recursiveSearchNoResults = false;
  recursiveSearchNoResultsUntilMs = 0;
  recursiveSearchDirectoryPath.clear();
  if (recursiveSearchDirectoryOpen) recursiveSearchDirectory.close();
  recursiveSearchDirectoryOpen = false;
  allFiles.clear();
  files.clear();
  selectorIndex = 0;
  resetPreviews();
}

void FolioLibraryActivity::processRecursiveSearch() {
  if (!recursiveSearchActive) return;
  if (!fileNameBuffer) {
    recursiveSearchActive = false;
    recursiveSearchMode = false;
    requestUpdate(true);
    return;
  }

  size_t operations = 0;
  while (operations < SEARCH_ENTRIES_PER_STEP) {
    ++operations;
    if (!recursiveSearchDirectoryOpen) {
      if (recursiveSearchDirectoryIndex >= recursiveSearchDirectories.size()) {
        recursiveSearchActive = false;
        allFiles = std::move(recursiveSearchMatches);
        FsHelpers::sortFileList(allFiles);
        applyLibraryFilter();
        recursiveSearchNoResults = allFiles.empty();
        recursiveSearchNoResultsUntilMs = millis() + (recursiveSearchNoResults ? 3000UL : 0UL);
        requestUpdate(true);
        return;
      }
      recursiveSearchDirectoryPath = recursiveSearchDirectories[recursiveSearchDirectoryIndex++];
      recursiveSearchDirectory = Storage.open(recursiveSearchDirectoryPath.c_str());
      if (!recursiveSearchDirectory || !recursiveSearchDirectory.isDirectory()) continue;
      recursiveSearchDirectory.rewindDirectory();
      recursiveSearchDirectoryOpen = true;
    }

    HalFile entry = recursiveSearchDirectory.openNextFile();
    if (!entry) {
      recursiveSearchDirectory.close();
      recursiveSearchDirectoryOpen = false;
      continue;
    }
    entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const std::string name(fileNameBuffer.get());
    const size_t nameSlash = name.find_last_of('/');
    const std::string entryName = nameSlash == std::string::npos ? name : name.substr(nameSlash + 1);
    if (entryName.empty() || entryName == "." || entryName == ".." || entryName == ".crosspoint" ||
        entryName == ".sleep" || entryName == "System Volume Information" ||
        (!SETTINGS.showHiddenFiles && entryName.front() == '.')) {
      continue;
    }

    ++recursiveSearchScannedEntries;
    const std::string childPath = joinLibraryPath(recursiveSearchDirectoryPath, name);
    if (entry.isDirectory()) {
      recursiveSearchDirectories.push_back(childPath);
      continue;
    }
    // Search All Folders is intended for books. Images remain available from
    // the normal file browser and should not crowd the book result list.
    if (!isLibraryBook(childPath)) {
      continue;
    }
    const std::string relativePath = relativeLibraryPath(recursiveSearchRoot, childPath);
    if (containsLibrarySearchTerm(relativePath, searchQueryFolded)) {
      recursiveSearchMatches.push_back(relativePath);
    }
  }

  if (millis() >= recursiveSearchNextUiUpdateMs) {
    recursiveSearchNextUiUpdateMs = millis() + 500;
    requestUpdate(true);
  }
}

void FolioLibraryActivity::clearSearch() {
  if (recursiveSearchDirectoryOpen) recursiveSearchDirectory.close();
  recursiveSearchDirectoryOpen = false;
  recursiveSearchActive = false;
  recursiveSearchMode = false;
  recursiveSearchRoot.clear();
  recursiveSearchDirectories.clear();
  recursiveSearchMatches.clear();
  recursiveSearchDirectoryPath.clear();
  recursiveSearchScannedEntries = 0;
  recursiveSearchNextUiUpdateMs = 0;
  recursiveSearchNoResults = false;
  recursiveSearchNoResultsUntilMs = 0;
  searchQuery.clear();
  searchQueryFolded.clear();
  loadFiles();
}

void FolioLibraryActivity::showBookActions() {
  if (selectorIndex >= files.size() || (!files[selectorIndex].empty() && files[selectorIndex].back() == '/')) return;
  const std::string selectedPath = fullPath(selectorIndex);
  std::vector<std::string> actions = {tr(STR_OPEN), tr(STR_MARK_READING), tr(STR_MARK_ON_HOLD), tr(STR_FINISHED),
                                      tr(STR_RESET_PROGRESS), tr(STR_REFRESH_BOOK_CACHE),
                                      tr(STR_DELETE_CACHE), tr(STR_READ_FULL_SYNOPSIS),
                                      tr(STR_BOOK_STATISTICS)};
  if (FsHelpers::hasEpubExtension(selectedPath)) {
    actions.emplace_back(tr(STR_BOOKMARKS));
    actions.emplace_back(tr(STR_CLIPPINGS));
  }
  bookActionsPopup.show(StrId::STR_BOOK_ACTIONS, actions, 0, [this](const int action) {
    const std::string path = fullPath(selectorIndex);
    const size_t slot = selectorIndex - previewPageStart;
    if (slot >= PAGE_SIZE) return;
    Preview& preview = previews[slot];
    if (action == 0) {
      logCbzPath("library-book-selection", path);
      onSelectBook(path);
      return;
    }
    if (action == 1) {
      BOOK_STATES.setStatus(path, BookStatus::Reading);
      if (preview.progressPercent >= 100) {
        preview.progressPercent = 99;
        RECENT_BOOKS.recordReading(path, 99, 0);
      }
    } else if (action == 2) {
      BOOK_STATES.setStatus(path, BookStatus::OnHold);
      if (preview.progressPercent >= 100) {
        preview.progressPercent = 99;
        RECENT_BOOKS.recordReading(path, 99, 0);
      }
    } else if (action == 3) {
      BOOK_STATES.setStatus(path, BookStatus::Finished);
      RECENT_BOOKS.recordReading(path, 100, 0);
      preview.progressPercent = 100;
    } else if (action == 4) {
      BOOK_STATES.reset(path);
      resetBookProgress(path);
      RECENT_BOOKS.recordReading(path, 0, 0);
      preview.progressPercent = 0;
    } else if (action == 5) {
      // Refresh the source metadata without deleting the reader cache. This
      // preserves progress, bookmarks, clippings, and cached reading pages.
      // Keep the current preview visible while the replacement is being read;
      // blanking it first made X3 look permanently stuck when extraction was
      // slow or the source EPUB had a malformed cover.
      preview.metadataAttempted = false;
      preview.loaded = true;
      observedSelectorIndex = selectorIndex;
      selectionChangedMs = millis() - 1500;
      retrievingMetadata = false;
      retrievingMetadataProgress = 0;
      retrievingMetadataCleanupOnCancel = true;
      retrievingPopupRendered = false;
      forceMetadataRefresh = true;
      forceMetadataRefreshIndex = selectorIndex;
    } else if (action == 6) {
      // Preserve progress, but force CBZ reader pages to be rebuilt.
      clearBookCache(path);
    } else if (action == 7) {
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, preview.title, preview.author, preview.synopsis,
                                             path),
          nullptr);
      return;
    } else if (action == 8) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, path), nullptr);
      return;
    } else if (action == 9 && FsHelpers::hasEpubExtension(path)) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, path),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
      return;
    } else if (action == 10 && FsHelpers::hasEpubExtension(path)) {
      startActivityForResult(
          std::make_unique<EpubReaderClippingListActivity>(renderer, mappedInput, path, preview.title), nullptr);
      return;
    }
    requestUpdate(true);
  });
  requestUpdate();
}

void FolioLibraryActivity::loadSelectedMetadata() {
  constexpr unsigned long SELECTION_DEBOUNCE_MS = 1500;
  if (selectorIndex >= files.size() || selectorIndex < previewPageStart ||
      selectorIndex >= previewPageStart + PAGE_SIZE || millis() - selectionChangedMs < SELECTION_DEBOUNCE_MS)
    return;

  Preview& preview = previews[selectorIndex - previewPageStart];
  if (!preview.loaded || preview.directory || preview.metadataAttempted) return;

  const std::string path = fullPath(selectorIndex);
  logCbzPath("library-file-selection", path);
  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
    preview.metadataAttempted = true;
    return;
  }
  // Only EPUB/XTC/CBZ books have metadata extractors here. Leave other file
  // types (including future PDF/image entries) as filename-only rows.
  if (!FsHelpers::hasEpubExtension(path) && !FsHelpers::hasXtcExtension(path) &&
      !FsHelpers::hasCbzExtension(path)) {
    preview.metadataAttempted = true;
    return;
  }

  // A complete cached header is safe to read after the debounce and does not
  // need a retrieval popup. Only a cache miss (or a missing thumbnail that
  // needs extraction) enters the visible retrieval path.
  const bool forceRefresh = forceMetadataRefresh && forceMetadataRefreshIndex == selectorIndex;
  if (!forceRefresh && !retrievingMetadata && FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (epub.loadCachedMetadataOnly()) {
      preview.title = epub.getTitle().empty() ? files[selectorIndex] : epub.getTitle();
      preview.author = epub.getAuthor();
      // This is the non-blocking cached shelf path. Read the synopsis from
      // the cache as well; this never opens the EPUB ZIP or reparses CSS.
      // Keep an already-populated shelf synopsis if an older cache has none.
      if (!epub.getDescription().empty()) preview.synopsis = epub.getDescription();
      const std::string cachedCover = epub.getThumbBmpPath();
      if (isValidBookThumbnail(UITheme::getCoverThumbPath(cachedCover, FolioNooirTheme::COVER_HEIGHT))) {
        preview.coverBmpPath = cachedCover;
      }
      if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path)) {
        if (!overrideData->title.empty()) preview.title = overrideData->title;
        preview.author = overrideData->author;
        preview.synopsis = overrideData->synopsis;
      }
      // A valid EPUB metadata cache is enough for the bookshelf. A missing
      // optional cover can be repaired explicitly with Refresh Book Cache;
      // never reparse the ZIP merely because that cover is absent.
      preview.metadataAttempted = true;
      requestUpdate();
      return;
    }
  }
  if (!forceRefresh && !retrievingMetadata && FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    const std::string thumb = xtc.getThumbBmpPath(FolioNooirTheme::COVER_HEIGHT);
    if (isValidBookThumbnail(thumb)) {
      preview.coverBmpPath = xtc.getThumbBmpPath();
      if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path)) {
        if (!overrideData->title.empty()) preview.title = overrideData->title;
        preview.author = overrideData->author;
        preview.synopsis = overrideData->synopsis;
      }
      preview.metadataAttempted = true;
      requestUpdate();
      return;
    }
  }
  if (!forceRefresh && !retrievingMetadata && FsHelpers::hasCbzExtension(path)) {
    Cbz cbz(path, "/.crosspoint");
    if (cbz.loadCachedMetadataOnly()) {
      preview.title = cbz.getTitle().empty() ? files[selectorIndex] : cbz.getTitle();
      preview.author = cbz.getAuthor();
      preview.synopsis = cbz.getSynopsis();
      const std::string cachedCover = cbz.getThumbBmpPath();
      const std::string cachedThumb = UITheme::getCoverThumbPath(cachedCover, FolioNooirTheme::COVER_HEIGHT);
      logCbzCacheLookup(cachedThumb, Storage.exists(cachedThumb.c_str()));
      if (isValidBookThumbnail(cachedThumb)) {
        preview.coverBmpPath = cachedCover;
      }
      if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path)) {
        if (!overrideData->title.empty()) preview.title = overrideData->title;
        preview.author = overrideData->author;
        preview.synopsis = overrideData->synopsis;
      }
      preview.metadataAttempted = true;
      requestUpdate();
      return;
    }
  }

  if (!retrievingMetadata) {
    retrievingMetadata = true;
    retrievingMetadataProgress = 5;
    retrievingMetadataCleanupOnCancel = forceRefresh;
    retrievingMetadataIndex = selectorIndex;
    retrievingPopupRendered = false;
    // Queue the busy frame without blocking the main task. The next loop will
    // wait for the render acknowledgement before starting synchronous
    // ZIP/thumbnail work. This keeps the popup visible and X3 input responsive.
    requestUpdate(true);
    return;
  }
  if (retrievingMetadataIndex != selectorIndex) return;
  // Popup-first: do not begin synchronous metadata/cover work until the
  // retrieval dialog has actually rendered.  The 1.5 s selection debounce is
  // enforced before this state is entered; this guard only waits for the
  // visible feedback frame and never starts work for a transient selection.
  if (!retrievingPopupRendered) return;

  retrievingMetadataProgress = 35;
  requestUpdateAndWait();
  const unsigned long retrievalWorkStartedMs = millis();
  retrievingMetadata = false;
  retrievingPopupRendered = false;
  forceMetadataRefresh = false;
  forceMetadataRefreshIndex = SIZE_MAX;
  preview.metadataAttempted = true;
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (epub.loadMetadataOnly()) {
      preview.title = epub.getTitle().empty() ? files[selectorIndex] : epub.getTitle();
      preview.author = epub.getAuthor();
      // A lightweight EPUB metadata pass can legitimately return no
      // description (for example while an EPUB package is still being
      // opened). Keep the existing preview instead of turning that
      // temporary empty result into "No synopsis".
      std::string parsedSynopsis = epub.getDescription();
      if (parsedSynopsis.empty()) {
        // Prefer the existing reader metadata cache when a lightweight OPF
        // pass returns an empty description during a refresh.
        Epub cached(path, "/.crosspoint");
        if (cached.loadCachedMetadataOnly()) parsedSynopsis = cached.getDescription();
      }
      if (!parsedSynopsis.empty()) preview.synopsis = parsedSynopsis;
      preview.coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
      if (!isValidBookThumbnail(thumb)) {
        Storage.remove(thumb.c_str());
        if (!epub.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
          preview.coverBmpPath.clear();
        }
      }
      // Refresh Book Cache updates presentation metadata only; an empty
      // lightweight description must not erase the existing shelf synopsis.
      // Reading state is stored separately.
      RECENT_BOOKS.refreshBookMetadata(path, preview.title, preview.author, preview.coverBmpPath, preview.synopsis);
      // Metadata can be shown immediately.  Thumbnail conversion is deferred
      // only for already-cached entries; the metadata-only path above keeps
      // this selected featured book self-contained.
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      preview.title = xtc.getTitle();
      preview.author = xtc.getAuthor();
      preview.coverBmpPath = xtc.getThumbBmpPath();
    }
  } else if (FsHelpers::hasCbzExtension(path)) {
    Cbz cbz(path, "/.crosspoint");
    if (cbz.loadMetadataOnly()) {
      preview.title = cbz.getTitle().empty() ? files[selectorIndex] : cbz.getTitle();
      preview.author = cbz.getAuthor();
      preview.synopsis = cbz.getSynopsis();
      preview.coverBmpPath = cbz.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
      logCbzCacheLookup(thumb, Storage.exists(thumb.c_str()));
      if (!isValidBookThumbnail(thumb)) {
        if (!cbz.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
          preview.coverBmpPath.clear();
        }
      }
      RECENT_BOOKS.refreshBookMetadata(path, preview.title, preview.author, preview.coverBmpPath, preview.synopsis);
    }
  }
  if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path)) {
    if (!overrideData->title.empty()) preview.title = overrideData->title;
    preview.author = overrideData->author;
    preview.synopsis = overrideData->synopsis;
  }
  LOG_DBG("PERF", "Metadata retrieval book=%s elapsed=%lums", path.c_str(),
          static_cast<unsigned long>(millis() - retrievalWorkStartedMs));
  retrievingMetadataProgress = 100;
  // Always repaint after retrieval, including a failed/empty metadata pass,
  // so the progress overlay cannot remain ghosted on the e-ink panel.
  requestUpdate(true);
}

void FolioLibraryActivity::activateSelected() {
  if (selectorIndex >= files.size()) return;
  if (!files[selectorIndex].empty() && files[selectorIndex].back() == '/') {
    searchQuery.clear();
    searchQueryFolded.clear();
    recursiveSearchMode = false;
    basepath = fullPath(selectorIndex);
    loadFiles();
    requestUpdate();
    return;
  }
  const std::string path = fullPath(selectorIndex);
  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
    activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
  } else {
    onSelectBook(path);
  }
}

void FolioLibraryActivity::onEnter() {
  Activity::onEnter();
  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FLIB", "OOM: %u-byte filename buffer", static_cast<unsigned>(NAME_BUFFER_SIZE));
    return;
  }
  HalFile start = Storage.open(basepath.c_str());
  if (start && !start.isDirectory()) {
    const std::string original = basepath;
    basepath = FsHelpers::extractFolderPath(original);
    if (basepath.empty()) basepath = "/";
    loadFiles();
    const size_t slash = original.find_last_of('/');
    const std::string filename = original.substr(slash == std::string::npos ? 0 : slash + 1);
    const auto it = std::find(files.begin(), files.end(), filename);
    if (it != files.end()) selectorIndex = static_cast<size_t>(std::distance(files.begin(), it));
    resetPreviews();
  } else {
    if (!start) basepath = "/";
    loadFiles();
  }
  requestUpdate();
}

void FolioLibraryActivity::onExit() {
  retrieveAllProcessingBook.store(false);
  if (retrieveQueueOpen || retrieveQueueReading) retrieveQueueFile.close();
  if (retrieveThumbnailQueueWriting || retrieveThumbnailQueueReading) retrieveThumbnailQueueFile.close();
  if (retrieveScanDirectoryOpen) retrieveScanDirectory.close();
  retrieveQueueOpen = false;
  retrieveQueueReading = false;
  retrieveThumbnailQueueWriting = false;
  retrieveThumbnailQueueReading = false;
  retrieveScanDirectoryOpen = false;
  Storage.remove(RETRIEVE_QUEUE_PATH);
  Storage.remove(RETRIEVE_THUMB_QUEUE_PATH);
  if (recursiveSearchDirectoryOpen) recursiveSearchDirectory.close();
  recursiveSearchDirectoryOpen = false;
  recursiveSearchActive = false;
  recursiveSearchDirectories.clear();
  recursiveSearchMatches.clear();
  previews = {};
  allFiles.clear();
  files.clear();
  fileNameBuffer.reset();
  Activity::onExit();
}

void FolioLibraryActivity::loop() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  static const FolioNooirTheme folioPresentation;
  const FolioShelfLayout layout = folioPresentation.shelfLayout(renderer, metrics);
  const int listTop = layout.contentTop + layout.detailHeight;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const size_t pageStart = (selectorIndex / PAGE_SIZE) * PAGE_SIZE;
  if (pageStart != previewPageStart) resetPreviews();

  if (recursiveSearchActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      clearSearch();
      requestUpdate(true);
      return;
    }
    // Search in bounded batches. This keeps the input task responsive even
    // when the SD card contains many nested folders.
    processRecursiveSearch();
    return;
  }

  if (recursiveSearchNoResults) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        millis() >= recursiveSearchNoResultsUntilMs) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) clearSearch();
      recursiveSearchNoResults = false;
      requestUpdate(true);
    }
    return;
  }

  if (selectorIndex != observedSelectorIndex) {
    observedSelectorIndex = selectorIndex;
    selectionChangedMs = millis();
    retrievingMetadata = false;
    retrievingMetadataProgress = 0;
    retrievingMetadataCleanupOnCancel = false;
    retrievingMetadataIndex = SIZE_MAX;
    retrievingPopupRendered = false;
    forceMetadataRefresh = false;
    forceMetadataRefreshIndex = SIZE_MAX;
  }

  if (retrieveAllComplete) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      retrieveAllComplete = false;
      requestUpdate(true);
      return;
    }
    if (millis() >= retrieveAllCompleteUntilMs) {
      retrieveAllComplete = false;
      requestUpdate(true);
    }
    return;
  }
  if (retrievingAllBooks) {
    // isPressed also catches a Back press that happened while a synchronous
    // EPUB/XTC parser was running and the edge event was consumed meanwhile.
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Back)) {
      // The release belongs to the Stop action, not to the library behind it.
      // Consume it after we return so the menu does not open accidentally.
      swallowBackRelease = true;
      cancelRetrieveAllBooks();
      return;
    }
    processRetrieveAllBooks();
    return;
  }

  if (retrievingMetadata && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (retrievingMetadataCleanupOnCancel && retrievingMetadataIndex < files.size()) {
      clearBookCache(fullPath(retrievingMetadataIndex));
    }
    retrievingMetadata = false;
    retrievingPopupRendered = false;
    retrievingMetadataProgress = 0;
    retrievingMetadataIndex = SIZE_MAX;
    forceMetadataRefresh = false;
    forceMetadataRefreshIndex = SIZE_MAX;
    requestUpdate(true);
    return;
  }

  const bool menuActive = menuPopup.isActive();
  const bool menuBackPressed = menuActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (menuPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (menuBackPressed) swallowMenuBackRelease = true;
    return;
  }
  if (swallowMenuBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowMenuBackRelease = false;
    return;
  }

  const bool popupActive = bookActionsPopup.isActive();
  const bool popupConfirmPressed = popupActive && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool popupBackPressed = popupActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (bookActionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (popupConfirmPressed) swallowConfirmRelease = true;
    if (popupBackPressed) swallowBackRelease = true;
    return;
  }
  if (swallowBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowBackRelease = false;
    return;
  }

  // Child activities may consume the Confirm release that opened them. Reset
  // the guard as soon as the library is active again so long press works
  // repeatedly without requiring an extra tap.
  if (!bookActionsPopup.isActive() && !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    swallowConfirmRelease = false;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= 1000 &&
      !longPressActionShown) {
    longPressActionShown = true;
    swallowConfirmRelease = true;
    showBookActions();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    activateSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!searchQuery.empty() || recursiveSearchMode) {
      clearSearch();
      requestUpdate();
    } else if (basepath == "/") {
      showMenu();
    } else {
      searchQuery.clear();
      searchQueryFolded.clear();
      recursiveSearchMode = false;
      basepath = FsHelpers::extractFolderPath(basepath);
      if (basepath.empty()) basepath = "/";
      loadFiles();
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    activityManager.goToFolioShelf(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    activityManager.goToFolioShelf(2);
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    if (touchY < layout.contentTop) {
      const int tab = std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth()));
      if (tab == 0 && libraryFilter != LibraryFilter::All) {
        libraryFilter = LibraryFilter::All;
        applyLibraryFilter();
        requestUpdate();
        return;
      }
      if (tab == 1) activityManager.goToFolioShelf(1);
      if (tab == 2) activityManager.goToFolioShelf(2);
      return;
    }
  }
  int touchSelection = static_cast<int>(selectorIndex);
  const auto listTouch = handleListTouch(touchSelection, static_cast<int>(files.size()), listTop, listHeight, false);
  if (listTouch != ListTouchResult::None) {
    selectorIndex = static_cast<size_t>(touchSelection);
    if ((selectorIndex / PAGE_SIZE) * PAGE_SIZE != previewPageStart) resetPreviews();
    if (listTouch == ListTouchResult::Activated) activateSelected();
    else requestUpdate();
    return;
  }
  const int count = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), count);
    if ((selectorIndex / PAGE_SIZE) * PAGE_SIZE != previewPageStart) resetPreviews();
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), count);
    if ((selectorIndex / PAGE_SIZE) * PAGE_SIZE != previewPageStart) resetPreviews();
    requestUpdate();
  });
  loadNextPreview();
  // Metadata and cover extraction are deliberately selected-item work. Do not
  // scan/generate a background cover merely because the highlight moved: the
  // selected item must remain still for the debounce in loadSelectedMetadata().
  loadSelectedMetadata();
}

void FolioLibraryActivity::render(RenderLock&&) {
  // Keep the featured book and cover geometry stable. UI Scale is enabled
  // only for the file-browser list, controls, and popups below.
  renderer.setUiScaleTextEnabled(false);
  renderer.clearScreen();
  if (recursiveSearchActive) {
    renderer.setUiScaleTextEnabled(false);
    const std::string message = "Searching all folders\nPlease wait...\nScanned: " +
                                std::to_string(recursiveSearchScannedEntries) + "\nBooks found: " +
                                std::to_string(recursiveSearchMatches.size());
    GUI.drawPopup(renderer, message.c_str(), true);
    return;
  }
  if (recursiveSearchNoResults) {
    GUI.drawPopup(renderer, "Search complete\nNo matching books found\nTry another filename", true);
    return;
  }
  if (retrieveAllComplete) {
    renderer.setUiScaleTextEnabled(true);
    GUI.drawPopup(renderer, retrieveAllStatusMessage.empty() ? RETRIEVE_ALL_BOOKS_DONE_MESSAGE
                                                              : retrieveAllStatusMessage.c_str());
    return;
  }
  if (retrievingAllBooks) {
    // Keep this safety-critical progress dialog at a fixed, readable size.
    // A large UI Scale must not push the three lines outside the popup.
    renderer.setUiScaleTextEnabled(false);
    std::string messageLine1;
    std::string messageLine2;
    std::string messageLine3;
    int progress = 0;
    if (retrieveAllStage == RetrieveAllStage::Scanning) {
      messageLine1 = "Retrieve all book details";
      messageLine2 = "Scanning books";
      messageLine3 = retrieveAllTotal == 0 ? "Please be patient" :
                                             "Books found: " + std::to_string(retrieveAllTotal);
    } else if (retrieveAllStage == RetrieveAllStage::Metadata) {
      const uint32_t current = std::min(retrieveAllTotal, retrieveAllProcessed + 1);
      std::string name = retrieveAllCurrentPath;
      const size_t slash = name.find_last_of('/');
      if (slash != std::string::npos) name.erase(0, slash + 1);
      if (name.empty()) name = "book";
      const int currentProgress = retrieveAllTotal == 0
                                      ? 100
                                      : static_cast<int>((retrieveAllProcessed * 100u) / retrieveAllTotal);
      messageLine1 = "Retrieving " + std::to_string(current) + "/" +
                     std::to_string(retrieveAllTotal) + " (" + std::to_string(currentProgress) + "%)";
      const int nameWidth = std::max(40, renderer.getScreenWidth() - 80);
      messageLine2 = renderer.truncatedText(UI_10_FONT_ID, name.c_str(), nameWidth);
      if (retrieveAllProcessingBook.load()) {
        messageLine3 = "Reading metadata...";
      } else {
        messageLine3 = retrieveAllCurrentReady ? "Preparing metadata..." : "Checking metadata cache...";
      }
      progress = currentProgress;
    } else {
      const uint32_t total = retrieveAllThumbnailTotal;
      const uint32_t current = std::min(total, retrieveAllThumbnailProcessed + 1);
      std::string name = retrieveAllCurrentPath;
      const size_t slash = name.find_last_of('/');
      if (slash != std::string::npos) name.erase(0, slash + 1);
      if (name.empty()) name = "book";
      const int currentProgress = total == 0
                                      ? 100
                                      : static_cast<int>((retrieveAllThumbnailProcessed * 100u) / total);
      messageLine1 = "Preparing covers " + std::to_string(current) + "/" + std::to_string(total) + " (" +
                     std::to_string(currentProgress) + "%)";
      const int nameWidth = std::max(40, renderer.getScreenWidth() - 80);
      messageLine2 = renderer.truncatedText(UI_10_FONT_ID, name.c_str(), nameWidth);
      if (retrieveAllProcessingBook.load()) {
        messageLine3 = "Rendering thumbnail...";
      } else {
        messageLine3 = retrieveAllCurrentReady ? "Preparing thumbnail..." : "Checking thumbnail cache...";
      }
      progress = currentProgress;
    }
    const std::string message = messageLine1 + "\n" + messageLine2 + "\n" + messageLine3;
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, progress);
    const auto stopLabels = mappedInput.mapLabels(STOP_RETRIEVE_LABEL, "", "", "");
    GUI.drawButtonHints(renderer, stopLabels.btn1, stopLabels.btn2, stopLabels.btn3, stopLabels.btn4);
    // Fast popup updates are gentle on the panel, but hundreds of consecutive
    // passes can accumulate ghosting. Every 15 completed books, add one
    // half-refresh maintenance pass. It clears the panel without rebuilding
    // the shelf or allocating another book cache.
    constexpr uint32_t HALF_REFRESH_INTERVAL = 15;
    const uint32_t maintenanceProcessed = retrieveAllStage == RetrieveAllStage::Thumbnails
                                              ? retrieveAllThumbnailProcessed
                                              : retrieveAllProcessed;
    const bool needsMaintenanceRefresh =
        maintenanceProcessed >= HALF_REFRESH_INTERVAL &&
        (maintenanceProcessed / HALF_REFRESH_INTERVAL) >
            (retrieveAllLastHalfRefreshProcessed / HALF_REFRESH_INTERVAL);
    renderer.displayBuffer(needsMaintenanceRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    if (needsMaintenanceRefresh) retrieveAllLastHalfRefreshProcessed = maintenanceProcessed;
    retrievingAllBooksPopupRendered = true;
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  static const FolioNooirTheme folioPresentation;
  const FolioShelfLayout layout = folioPresentation.shelfLayout(renderer, metrics);
  renderer.setUiScaleTextEnabled(true);
  folioPresentation.drawShelfTabs(renderer, layout, 0);
  folioPresentation.drawShelfBattery(renderer, layout, metrics);
  renderer.setUiScaleTextEnabled(false);

  const int detailTop = layout.contentTop;
  const int detailHeight = layout.detailHeight;
  const int featuredTop = detailTop;
  const int featuredHeight = detailHeight;

  constexpr int detailPadding = 12;
  constexpr int detailCoverWidth = 126;
  const int coverX = detailPadding;
  const int coverY = featuredTop + 7;
  const int coverWidth = detailCoverWidth;
  const int coverHeight = featuredHeight - 20;
  const size_t slot = selectorIndex >= previewPageStart ? selectorIndex - previewPageStart : PAGE_SIZE;
  const Preview* selected = slot < PAGE_SIZE ? &previews[slot] : nullptr;
  bool drewCover = false;
  if (selected && selected->loaded && !selected->directory && !selected->coverBmpPath.empty()) {
    const std::string thumb = UITheme::getCoverThumbPath(selected->coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
    HalFile file;
    if (Storage.openFileForRead("FLIB", thumb, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        // Fit the cover inside its featured slot without stretching it, then
        // center it to keep the largest possible visible cover area.
        const int sourceWidth = bitmap.getWidth();
        const int sourceHeight = bitmap.getHeight();
        int drawWidth = sourceWidth;
        int drawHeight = sourceHeight;
        if (sourceWidth > coverWidth || sourceHeight > coverHeight) {
          if (static_cast<long long>(sourceWidth) * coverHeight >
              static_cast<long long>(sourceHeight) * coverWidth) {
            drawWidth = coverWidth;
            drawHeight = std::max(1, sourceHeight * coverWidth / sourceWidth);
          } else {
            drawHeight = coverHeight;
            drawWidth = std::max(1, sourceWidth * coverHeight / sourceHeight);
          }
        }
        const int drawX = coverX + (coverWidth - drawWidth) / 2;
        const int drawY = coverY + (coverHeight - drawHeight) / 2;
        renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
        renderer.drawRect(coverX, coverY, coverWidth, coverHeight);
        drewCover = true;
      }
    }
  }
  if (!drewCover) {
    renderer.drawRect(coverX + 8, coverY, coverWidth - 16, coverHeight);
    if (selected && selected->directory) {
      renderer.drawCenteredText(UI_12_FONT_ID, coverY + coverHeight / 2, "/", true);
    } else {
      const char* fallbackTitle = selected && selected->loaded
                                      ? selected->title.c_str()
                                      : (files.empty() ? "" : files[selectorIndex].c_str());
      const std::string label = renderer.truncatedText(UI_10_FONT_ID, fallbackTitle, coverWidth - 20);
      renderer.drawText(UI_10_FONT_ID, coverX + (coverWidth - renderer.getTextWidth(UI_10_FONT_ID, label.c_str())) / 2,
                        coverY + coverHeight / 2, label.c_str(), true);
    }
  }
  const int textX = detailPadding * 2 + detailCoverWidth;
  const int textWidth = renderer.getScreenWidth() - textX - detailPadding;
  const char* titleText = selected && selected->loaded ? selected->title.c_str()
                                                        : (files.empty() ? "" : files[selectorIndex].c_str());
  renderer.drawText(UI_12_FONT_ID, textX, featuredTop + 20,
                    renderer.truncatedText(UI_12_FONT_ID, titleText, textWidth).c_str(), true);
  if (selected && !selected->author.empty())
    renderer.drawText(UI_10_FONT_ID, textX, featuredTop + 55,
                      renderer.truncatedText(UI_10_FONT_ID, selected->author.c_str(), textWidth).c_str());
  const std::string synopsisPreview = selected ? SynopsisPreview::firstWords(selected->synopsis) : std::string{};
  const char* synopsis = synopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : synopsisPreview.c_str();
  const int synopsisY = featuredTop + 79;
  // Keep the state row and progress bar at the bottom of the featured
  // panel, leaving enough vertical room for five synopsis lines above it.
  const int progressTextY = featuredTop + featuredHeight - 37;
  constexpr int SYNOPSIS_PROGRESS_GAP_PX = 2;
  const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  const int synopsisMaxLines = std::clamp(
      (progressTextY - synopsisY - SYNOPSIS_PROGRESS_GAP_PX) / synopsisLineHeight, 1, 5);
  const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsis, textWidth, synopsisMaxLines);
  int synopsisDrawY = synopsisY;
  for (const auto& line : synopsisLines) {
    renderer.drawText(SMALL_FONT_ID, textX, synopsisDrawY, line.c_str());
    synopsisDrawY += synopsisLineHeight;
  }
  if (selected && !selected->directory) {
    const std::string selectedPath = fullPath(selectorIndex);
    const BookState* trackedState = BOOK_STATES.find(selectedPath);
    const RecentBook* trackedRecent = nullptr;
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    const auto recentIt = std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) {
      return book.path == selectedPath;
    });
    if (recentIt != recentBooks.end()) trackedRecent = &*recentIt;
    const uint8_t progress = std::max<uint8_t>(selected->progressPercent,
                                                trackedState ? trackedState->progressPercent : 0);
    const uint32_t seconds = std::max<uint32_t>(trackedState ? trackedState->readingSeconds : 0,
                                                trackedRecent ? trackedRecent->readingSeconds : 0);
    const uint16_t sessions = std::max<uint16_t>(trackedState ? trackedState->readingSessions : 0,
                                                 trackedRecent ? trackedRecent->readingSessions : 0);
    char progressLine[96];
    snprintf(progressLine, sizeof(progressLine), "%s - %u%% - %lu min - %u sessions",
             progress >= 100 ? tr(STR_COMPLETE) : (progress > 0 ? tr(STR_ONGOING) : tr(STR_NEW)), progress,
             static_cast<unsigned long>((seconds + 30) / 60), sessions);
    const std::string progressText = renderer.truncatedText(SMALL_FONT_ID, progressLine, textWidth);
    renderer.drawText(SMALL_FONT_ID, textX, progressTextY, progressText.c_str());
    const int progressY = featuredTop + featuredHeight - 14;
    renderer.drawRect(textX, progressY, textWidth, 12);
    const int fill = (textWidth - 2) * progress / 100;
    if (fill > 0) renderer.fillRect(textX + 1, progressY + 1, fill, 10);
  }
  renderer.drawLine(0, detailTop + detailHeight - 1, renderer.getScreenWidth() - 1, detailTop + detailHeight - 1);

  const int listTop = detailTop + detailHeight;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  renderer.setUiScaleTextEnabled(true);
  GUI.drawList(renderer, Rect{0, listTop, renderer.getScreenWidth(), listHeight}, files.size(), selectorIndex,
               [this](int index) {
                 std::string name = files[index];
                 if (!name.empty() && name.back() == '/') name.pop_back();
                 return name;
               },
               nullptr, [this](int index) { return UITheme::getFileIcon(files[index]); }, nullptr, false);

  if (menuPopup.processRender(renderer, mappedInput)) return;
  if (bookActionsPopup.processRender(renderer, mappedInput)) return;
  if (retrievingMetadata && retrievingMetadataIndex == selectorIndex) {
    // A selection must remain still for the full debounce interval before a
    // retrieval popup is allowed to appear. The guard also prevents a stale
    // render task from briefly showing the previous book's popup immediately
    // after the highlight moves.
    constexpr unsigned long SELECTION_DEBOUNCE_MS = 1500;
    if (millis() - selectionChangedMs >= SELECTION_DEBOUNCE_MS) {
      std::string bookName;
      if (retrievingMetadataIndex < files.size()) {
        const size_t bookSlot = retrievingMetadataIndex >= previewPageStart
                                    ? retrievingMetadataIndex - previewPageStart
                                    : PAGE_SIZE;
        if (bookSlot < PAGE_SIZE && !previews[bookSlot].title.empty()) {
          bookName = previews[bookSlot].title;
        } else {
          bookName = files[retrievingMetadataIndex];
        }
      }
      if (bookName.empty()) bookName = "book";
      const std::string prefix = std::string(tr(STR_RETRIEVING_BOOK_DETAILS)) + ": ";
      const int nameWidth = std::max(40, renderer.getScreenWidth() -
                                             renderer.getTextWidth(UI_10_FONT_ID, prefix.c_str()) - 100);
      const std::string message = prefix + "\n" + renderer.truncatedText(UI_10_FONT_ID, bookName.c_str(), nameWidth) +
                                  "\nProgress: " + std::to_string(retrievingMetadataProgress) + "%";
      const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
      GUI.fillPopupProgress(renderer, popup, retrievingMetadataProgress);
      const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      retrievingPopupRendered = true;
      return;
    }
  }
  // While a filename search is active, the first hardware button exits the
  // search and restores the normal Library view.  Keep the label in sync
  // with that action instead of showing Menu/Up, which is confusing at the
  // root of the library.
  const bool searchActive = !searchQuery.empty() || recursiveSearchMode;
  const auto labels = mappedInput.mapLabels(
      searchActive ? tr(STR_BACK) : (basepath == "/" ? tr(STR_MENU) : tr(STR_DIR_UP)), tr(STR_OPEN),
      tr(STR_RECENT), tr(STR_FINISHED));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
