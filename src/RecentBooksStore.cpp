#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <iterator>

#include "HalClock.h"

void RecentBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : recentBooks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["synopsis"] = book.synopsis;
    obj["progressPercent"] = book.progressPercent;
    obj["readingSeconds"] = book.readingSeconds;
    obj["lastSessionSeconds"] = book.lastSessionSeconds;
    obj["dailyReadingSeconds"] = book.dailyReadingSeconds;
    obj["dailyReadingDateKey"] = book.dailyReadingDateKey;
    obj["readingSessions"] = book.readingSessions;
  }
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'books' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  recentBooks.clear();
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  recentBooks.reserve(std::min(arr.size(), static_cast<size_t>(MAX_RECENT_BOOKS)));
  for (JsonObjectConst obj : arr) {
    if (getCount() >= MAX_RECENT_BOOKS) break;
    RecentBook book;
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    book.synopsis = obj["synopsis"] | "";
    book.progressPercent = obj["progressPercent"] | 0;
    book.readingSeconds = obj["readingSeconds"] | 0;
    book.lastSessionSeconds = obj["lastSessionSeconds"] | 0;
    book.dailyReadingSeconds = obj["dailyReadingSeconds"] | 0;
    book.dailyReadingDateKey = obj["dailyReadingDateKey"] | 0;
    book.readingSessions = obj["readingSessions"] | 0;
    recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", getCount());
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath, const std::string& synopsis) {
  // Drop stale entries first so a new add can't evict a valid book in their stead.
  const bool pruned = pruneMissing();

  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  RecentBook entry{path, title, author, coverBmpPath, synopsis.substr(0, 384)};
  if (it != recentBooks.end()) {
    if (entry.synopsis.empty()) entry.synopsis = it->synopsis;
    entry.progressPercent = it->progressPercent;
    entry.readingSeconds = it->readingSeconds;
    entry.lastSessionSeconds = it->lastSessionSeconds;
    entry.dailyReadingSeconds = it->dailyReadingSeconds;
    entry.dailyReadingDateKey = it->dailyReadingDateKey;
    entry.readingSessions = it->readingSessions;

    // Reopening the same book is common. If it is already at the front and
    // none of its cached presentation fields changed, there is nothing to
    // persist. This avoids an SD JSON write on every reader entry.
    const bool unchanged = it == recentBooks.begin() && it->title == entry.title && it->author == entry.author &&
                           it->coverBmpPath == entry.coverBmpPath && it->synopsis == entry.synopsis &&
                           it->progressPercent == entry.progressPercent && it->readingSeconds == entry.readingSeconds &&
                           it->lastSessionSeconds == entry.lastSessionSeconds &&
                           it->dailyReadingSeconds == entry.dailyReadingSeconds &&
                           it->dailyReadingDateKey == entry.dailyReadingDateKey &&
                           it->readingSessions == entry.readingSessions;
    if (unchanged && !pruned) return;
    recentBooks.erase(it);
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), std::move(entry));

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::recordReading(const std::string& path, uint8_t progress, uint32_t elapsedSeconds) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return;
  const uint8_t nextProgress = std::min<uint8_t>(progress, 100);
  // A reader opened and closed without advancing or spending time does not
  // change Recent data, so skip the filesystem write entirely.
  if (elapsedSeconds == 0 && it->progressPercent == nextProgress) return;
  it->progressPercent = nextProgress;
  if (elapsedSeconds > 0) {
    const uint32_t room = UINT32_MAX - it->readingSeconds;
    it->readingSeconds += std::min(elapsedSeconds, room);
    it->lastSessionSeconds = elapsedSeconds;
    const uint32_t today = halClock.getDateKey();
    if (today != 0) {
      if (it->dailyReadingDateKey != today) {
        it->dailyReadingDateKey = today;
        it->dailyReadingSeconds = 0;
      }
      const uint32_t dailyRoom = UINT32_MAX - it->dailyReadingSeconds;
      it->dailyReadingSeconds += std::min(elapsedSeconds, dailyRoom);
    }
    if (it->readingSessions < UINT16_MAX) ++it->readingSessions;
  }
  if (!saveToFile()) LOG_ERR("RBS", "Failed to persist reading statistics");
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath, const std::string& synopsis) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    if (!synopsis.empty()) book.synopsis = synopsis.substr(0, 384);
    saveToFile();
  }
}

void RecentBooksStore::refreshBookMetadata(const std::string& path, const std::string& title,
                                           const std::string& author, const std::string& coverBmpPath,
                                           const std::string& synopsis) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return;

  RecentBook& book = *it;
  // A device-side refresh must never erase a working synopsis because a
  // transient metadata pass returned an empty description. Explicit web
  // metadata edits use updateMetadata() and can still intentionally clear it.
  const std::string boundedSynopsis = synopsis.substr(0, 384);
  const std::string nextSynopsis = boundedSynopsis.empty() ? book.synopsis : boundedSynopsis;
  if (book.title == title && book.author == author && book.coverBmpPath == coverBmpPath &&
      book.synopsis == nextSynopsis) {
    return;
  }
  book.title = title;
  book.author = author;
  book.coverBmpPath = coverBmpPath;
  book.synopsis = nextSynopsis;
  if (!saveToFile()) LOG_ERR("RBS", "Failed to persist refreshed metadata: %s", path.c_str());
}

bool RecentBooksStore::updateMetadata(const std::string& path, const std::string& title, const std::string& author,
                                      const std::string& synopsis) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return false;
  it->title = title.substr(0, 192);
  it->author = author.substr(0, 128);
  // Metadata edited explicitly through the web UI should not be truncated;
  // the shelf's generated EPUB cache remains bounded separately.
  it->synopsis = synopsis;
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to save edited metadata: %s", path.c_str());
    return false;
  }
  return true;
}

bool RecentBooksStore::resetReading(const std::string& path) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return false;
  it->progressPercent = 0;
  it->readingSeconds = 0;
  it->lastSessionSeconds = 0;
  it->dailyReadingSeconds = 0;
  it->dailyReadingDateKey = 0;
  it->readingSessions = 0;
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to reset reading data: %s", path.c_str());
    return false;
  }
  return true;
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  recentBooks.erase(it);
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  }
  return true;
}

bool RecentBooksStore::clearAll() {
  if (recentBooks.empty()) return true;
  recentBooks.clear();
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist cleared recent books");
    return false;
  }
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  saveToFile();
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath(),
                      epub.getDescription().substr(0, 384)};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}
