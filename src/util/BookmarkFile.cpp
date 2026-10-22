#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <utility>

#include "BookmarkUtil.h"

namespace {
enum class BookmarkKind { Unknown, Epub, Xtc, Cbz };

constexpr const char* kXtcPagePrefix = "xtc-page:";

BookmarkKind kindForBookPath(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return BookmarkKind::Epub;
  if (FsHelpers::hasXtcExtension(path)) return BookmarkKind::Xtc;
  if (FsHelpers::hasCbzExtension(path)) return BookmarkKind::Cbz;
  return BookmarkKind::Unknown;
}

BookmarkKind kindForEntry(const BookmarkEntry& bookmark) {
  if (bookmark.xpath.compare(0, 9, kXtcPagePrefix) == 0) return BookmarkKind::Xtc;
  if (!bookmark.xpath.empty()) return BookmarkKind::Epub;
  if (bookmark.computedChapterPageCount > 0) return BookmarkKind::Cbz;
  return BookmarkKind::Unknown;
}

bool loadAll(const std::string& path, std::vector<BookmarkEntry>& bookmarks) {
  bookmarks.clear();
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) return false;

  JsonArray arr = doc["bookmarks"].as<JsonArray>();
  bookmarks.reserve(arr.size());
  for (JsonObject obj : arr) {
    bookmarks.emplace_back();
    auto& bookmark = bookmarks.back();
    bookmark.xpath = obj["xpath"] | "";
    bookmark.percentage = obj["percentage"] | static_cast<float>(0);
    bookmark.summary = obj["summary"] | "";
    bookmark.computedSpineIndex = obj["si"] | static_cast<uint16_t>(0);
    bookmark.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    bookmark.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
  }
  return true;
}

bool saveAll(const std::string& path, const std::vector<BookmarkEntry>& bookmarks) {
  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  for (const auto& bookmark : bookmarks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = bookmark.xpath;
    obj["percentage"] = bookmark.percentage;
    obj["summary"] = bookmark.summary;
    obj["si"] = bookmark.computedSpineIndex;
    obj["pc"] = bookmark.computedChapterPageCount;
    obj["pp"] = bookmark.computedChapterProgress;
  }
  return PersistableStoreBase::writeDocToFile(path.c_str(), doc);
}
}  // namespace

bool BookmarkFile::load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks) {
  bookmarks.clear();
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  std::vector<BookmarkEntry> loaded;
  if (!loadAll(path, loaded)) return false;

  const BookmarkKind requestedKind = kindForBookPath(bookPath);
  const bool containsTypedEntries = std::any_of(loaded.begin(), loaded.end(), [](const BookmarkEntry& bookmark) {
    return kindForEntry(bookmark) != BookmarkKind::Unknown;
  });
  if (requestedKind == BookmarkKind::Unknown || !containsTypedEntries) {
    // Keep legacy records readable when their format cannot be inferred from
    // the saved fields (or the caller uses a format without typed bookmarks).
    bookmarks = std::move(loaded);
  } else {
    bookmarks.reserve(loaded.size());
    for (auto& bookmark : loaded) {
      if (kindForEntry(bookmark) == requestedKind) bookmarks.push_back(std::move(bookmark));
    }
  }

  LOG_DBG("BKM", "Loaded %zu bookmarks from file", bookmarks.size());
  return true;
}

bool BookmarkFile::save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks) {
  LOG_DBG("BKM", "Saving %zu bookmarks to file", bookmarks.size());
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  const BookmarkKind requestedKind = kindForBookPath(bookPath);
  std::vector<BookmarkEntry> merged;

  // Bookmark files are historically keyed by path without the extension, so
  // same-named EPUB, XTC/XTCH, and CBZ files can share one JSON file. Preserve
  // other formats' records when a reader saves its own list. This extra read
  // only happens on an explicit bookmark save, never during Home rendering.
  if (requestedKind != BookmarkKind::Unknown && Storage.exists(path.c_str())) {
    std::vector<BookmarkEntry> existing;
    if (!loadAll(path, existing)) {
      LOG_ERR("BKM", "Could not read existing bookmarks before save: %s", path.c_str());
      return false;
    }
    const bool containsTypedEntries = std::any_of(existing.begin(), existing.end(), [](const BookmarkEntry& bookmark) {
      return kindForEntry(bookmark) != BookmarkKind::Unknown;
    });
    if (containsTypedEntries) {
      merged.reserve(existing.size() + bookmarks.size());
      for (auto& bookmark : existing) {
        if (kindForEntry(bookmark) != requestedKind) merged.push_back(std::move(bookmark));
      }
    }
  }
  merged.insert(merged.end(), bookmarks.begin(), bookmarks.end());

  // writeDocToFile ensures /.crosspoint; the bookmarks subdirectory is ours.
  Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str());
  return saveAll(path, merged);
}
