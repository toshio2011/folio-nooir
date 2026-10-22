#include "RecentBooksStore.h"

#include <Epub.h>
#include <Cbz.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <iterator>
#include <mutex>
#include <utility>

#include "HalClock.h"
#include "BookMetadataOverridesStore.h"

namespace {
void applyMetadataOverride(RecentBook& book) {
  const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(book.path);
  if (!overrideData) return;
  book.title = overrideData->title;
  book.author = overrideData->author;
  book.synopsis = overrideData->synopsis;
}
}  // namespace

const char* recentBooksLoadResultName(const RecentBooksLoadResult result) {
  switch (result) {
    case RecentBooksLoadResult::NotLoaded: return "not_loaded";
    case RecentBooksLoadResult::Valid: return "valid";
    case RecentBooksLoadResult::Recovered: return "recovered";
    case RecentBooksLoadResult::MissingFirstBoot: return "missing_first_boot";
    case RecentBooksLoadResult::Uncertain: return "uncertain";
  }
  return "unknown";
}

void RecentBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : recentBooks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["synopsis"] = book.synopsis;
    obj["series"] = book.series;
    obj["xtcMetadataChecked"] = book.xtcMetadataChecked;
    obj["progressPercent"] = book.progressPercent;
    obj["readingSeconds"] = book.readingSeconds;
    obj["lastSessionSeconds"] = book.lastSessionSeconds;
    obj["dailyReadingSeconds"] = book.dailyReadingSeconds;
    obj["dailyReadingDateKey"] = book.dailyReadingDateKey;
    obj["readingSessions"] = book.readingSessions;
    obj["pagesTurned"] = book.pagesTurned;
  }
}

bool RecentBooksStore::parseJsonBooks(JsonVariantConst doc, std::vector<RecentBook>& parsedBooks) {
  // A syntactically valid JSON object without the Recent schema is not an
  // intentional empty history. Validate into a replacement vector so a bad
  // entry or allocation/parser failure cannot partially clear live records.
  if (!doc.is<JsonObjectConst>() || !doc["books"].is<JsonArrayConst>()) return false;
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  parsedBooks.reserve(arr.size());
  for (JsonVariantConst item : arr) {
    if (!item.is<JsonObjectConst>()) return false;
    JsonObjectConst obj = item.as<JsonObjectConst>();
    const char* path = obj["path"].as<const char*>();
    if (path == nullptr || path[0] == '\0') return false;
    RecentBook book;
    book.path = path;
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    book.synopsis = obj["synopsis"] | "";
    book.series = obj["series"] | "";
    book.xtcMetadataChecked = obj["xtcMetadataChecked"] | false;
    book.progressPercent = obj["progressPercent"] | uint8_t{0};
    book.readingSeconds = obj["readingSeconds"] | uint32_t{0};
    book.lastSessionSeconds = obj["lastSessionSeconds"] | uint32_t{0};
    book.dailyReadingSeconds = obj["dailyReadingSeconds"] | uint32_t{0};
    book.dailyReadingDateKey = obj["dailyReadingDateKey"] | uint32_t{0};
    book.readingSessions = obj["readingSessions"] | uint16_t{0};
    book.pagesTurned = obj["pagesTurned"] | uint32_t{0};
    applyMetadataOverride(book);
    parsedBooks.push_back(std::move(book));
  }

  return true;
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) {
  std::vector<RecentBook> parsedBooks;
  if (!parseJsonBooks(doc, parsedBooks)) return false;
  recentBooks.swap(parsedBooks);
  LOG_DBG("RBS", "Recent books parsed (%u entries)", static_cast<unsigned>(recentBooks.size()));
  return true;
}

bool RecentBooksStore::loadFromFile() {
  std::lock_guard<std::mutex> lock(storeMutex);
  static constexpr size_t kMaxRecentJsonBytes = 65536;
  const std::string primaryPath = getFilePath();
  const std::string candidates[] = {primaryPath, primaryPath + ".bak", primaryPath + ".tmp"};
  static constexpr const char* kCandidateNames[] = {"primary", "backup", "temporary"};
  bool sawMalformedCandidate = false;
  bool sawUncertainCandidate = false;
  bool sawPresentCandidate = false;

  constexpr size_t kCandidateCount = sizeof(candidates) / sizeof(candidates[0]);
  for (size_t i = 0; i < kCandidateCount; ++i) {
    JsonDocument document;
    HalStorageReadStatus readStatus;
    DeserializationError parseError;
    {
      // Scope the temporary file string so it is released before copying the
      // parsed records into the bounded in-memory vector.
      String json;
      readStatus = Storage.readFileChecked(candidates[i].c_str(), json, kMaxRecentJsonBytes);
      if (readStatus == HalStorageReadStatus::NotFound || readStatus == HalStorageReadStatus::ParentMissing) {
        LOG_INF("RBS", "load_candidate=%s result=missing", kCandidateNames[i]);
        continue;
      }
      if (readStatus != HalStorageReadStatus::Success) {
        sawUncertainCandidate = true;
        LOG_ERR("RBS", "load_candidate=%s result=read_error status=%u", kCandidateNames[i],
                static_cast<unsigned>(readStatus));
        continue;
      }

      sawPresentCandidate = true;
      parseError = deserializeJson(document, json);
    }

    if (parseError) {
      sawMalformedCandidate = true;
      LOG_ERR("RBS", "load_candidate=%s result=parse_error reason=%s", kCandidateNames[i], parseError.c_str());
      continue;
    }
    std::vector<RecentBook> parsedBooks;
    if (!parseJsonBooks(document.as<JsonVariantConst>(), parsedBooks)) {
      sawMalformedCandidate = true;
      LOG_ERR("RBS", "load_candidate=%s result=schema_error", kCandidateNames[i]);
      continue;
    }
    // Release the parsed JSON DOM before swapping in the copied entries. This
    // keeps the short-lived load peak lower on the smaller X3 heap.
    document.clear();

    hasLoadedValidHistory = true;
    if (sawUncertainCandidate || i == 2) {
      // A valid fallback is still useful to render, but a failed read of an
      // earlier candidate means it could contain newer data. Keep every file
      // in place and refuse automatic writes until a complete read succeeds.
      // A temporary-only source is also write-blocked: the shared atomic
      // writer owns `.tmp` and can replace it before a new primary commits.
      loadResult.store(RecentBooksLoadResult::Uncertain);
      if (recentBooks.empty()) {
        recentBooks.swap(parsedBooks);
      } else {
        LOG_ERR("RBS", "load_fallback_kept_memory count=%u source=%s",
                static_cast<unsigned>(recentBooks.size()), kCandidateNames[i]);
      }
      LOG_ERR("RBS", "load_result=uncertain_fallback count=%u source=%s writes=blocked reason=%s",
              static_cast<unsigned>(recentBooks.size()), kCandidateNames[i],
              i == 2 ? "temporary_only_source" : "earlier_candidate_read_error");
    } else {
      recentBooks.swap(parsedBooks);
      loadResult.store(i == 0 ? RecentBooksLoadResult::Valid : RecentBooksLoadResult::Recovered);
      LOG_INF("RBS", "load_result=%s count=%u source=%s prior_candidate_error=%u",
              recentBooksLoadResultName(loadResult.load()), static_cast<unsigned>(recentBooks.size()),
              kCandidateNames[i], sawMalformedCandidate ? 1U : 0U);
    }
    return true;
  }

  // A confirmed first-boot absence is writable. Missing files after a valid
  // history was loaded are not: this can happen after a card replacement or
  // unexpected filesystem state, and must not turn the next book open into a
  // destructive empty-history save.
  if (!sawPresentCandidate && !sawMalformedCandidate && !sawUncertainCandidate && Storage.ready() &&
      !hasLoadedValidHistory && recentBooks.empty()) {
    loadResult.store(RecentBooksLoadResult::MissingFirstBoot);
    LOG_INF("RBS", "load_result=missing_first_boot count=0 writes=allowed");
    return false;
  }

  loadResult.store(RecentBooksLoadResult::Uncertain);
  LOG_ERR("RBS", "load_result=uncertain count=%u writes=blocked malformed=%u io_uncertain=%u storage_ready=%u",
          static_cast<unsigned>(recentBooks.size()), sawMalformedCandidate ? 1U : 0U,
          sawUncertainCandidate ? 1U : 0U, Storage.ready() ? 1U : 0U);
  return false;
}

bool RecentBooksStore::saveToFile() const {
  std::lock_guard<std::mutex> lock(storeMutex);
  return saveToFileLocked();
}

bool RecentBooksStore::saveToFileLocked() const {
  if (!automaticWritesAllowed()) {
    LOG_ERR("RBS", "save_blocked load_result=%s", recentBooksLoadResultName(loadResult.load()));
    return false;
  }
  JsonDocument doc;
  toJson(doc);
  const bool saved = loadResult.load() == RecentBooksLoadResult::Recovered
                         ? PersistableStoreBase::writeRecoveredRecentBooksDocToFile(getFilePath(), doc)
                         : PersistableStoreBase::writeDocToFile(getFilePath(), doc);
  if (saved) {
    loadResult.store(RecentBooksLoadResult::Valid);
    return true;
  }
  loadResult.store(RecentBooksLoadResult::Uncertain);
  LOG_ERR("RBS", "save_result=failed load_result=uncertain writes=blocked");
  return false;
}

int RecentBooksStore::getCount() const {
  std::lock_guard<std::mutex> lock(storeMutex);
  return static_cast<int>(recentBooks.size());
}

bool RecentBooksStore::rejectUnsafeMutation(const char* operation) const {
  if (automaticWritesAllowed()) return false;
  LOG_ERR("RBS", "mutation_blocked operation=%s load_result=%s", operation,
          recentBooksLoadResultName(loadResult.load()));
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath, const std::string& synopsis,
                               const std::string& series, const bool xtcMetadataChecked) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("add_book")) return;
  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  RecentBook entry{path, title, author, coverBmpPath, synopsis.substr(0, 384), series.substr(0, 128)};
  entry.xtcMetadataChecked = FsHelpers::hasXtcExtension(path) && xtcMetadataChecked;
  applyMetadataOverride(entry);
  size_t oldIndex = 0;
  RecentBook oldEntry;
  const bool hadOldEntry = it != recentBooks.end();
  if (hadOldEntry) {
    oldIndex = static_cast<size_t>(std::distance(recentBooks.begin(), it));
    if (entry.title.empty()) entry.title = it->title;
    if (entry.author.empty()) entry.author = it->author;
    if (entry.coverBmpPath.empty()) entry.coverBmpPath = it->coverBmpPath;
    if (entry.synopsis.empty()) entry.synopsis = it->synopsis;
    if (entry.series.empty()) entry.series = it->series;
    entry.progressPercent = it->progressPercent;
    entry.readingSeconds = it->readingSeconds;
    entry.lastSessionSeconds = it->lastSessionSeconds;
    entry.dailyReadingSeconds = it->dailyReadingSeconds;
    entry.dailyReadingDateKey = it->dailyReadingDateKey;
    entry.readingSessions = it->readingSessions;
    entry.pagesTurned = it->pagesTurned;
    // Preserve an existing checked result, but do not promote a transient
    // parser failure into a completed metadata check.
    entry.xtcMetadataChecked = entry.xtcMetadataChecked || it->xtcMetadataChecked;

    // Reopening the same book is common. If it is already at the front and
    // none of its cached presentation fields changed, there is nothing to
    // persist. This avoids an SD JSON write on every reader entry.
    const bool unchanged = it == recentBooks.begin() && it->title == entry.title && it->author == entry.author &&
                           it->coverBmpPath == entry.coverBmpPath && it->synopsis == entry.synopsis &&
                           it->progressPercent == entry.progressPercent && it->readingSeconds == entry.readingSeconds &&
                           it->lastSessionSeconds == entry.lastSessionSeconds &&
                           it->dailyReadingSeconds == entry.dailyReadingSeconds &&
                           it->dailyReadingDateKey == entry.dailyReadingDateKey &&
                           it->readingSessions == entry.readingSessions &&
                           it->pagesTurned == entry.pagesTurned &&
                           it->xtcMetadataChecked == entry.xtcMetadataChecked && it->series == entry.series;
    if (unchanged) return;
    oldEntry = std::move(*it);
    recentBooks.erase(it);
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), std::move(entry));

  if (!saveToFileLocked()) {
    recentBooks.erase(recentBooks.begin());
    if (hadOldEntry) {
      recentBooks.insert(recentBooks.begin() + static_cast<std::ptrdiff_t>(oldIndex), std::move(oldEntry));
    }
    LOG_ERR("RBS", "add_book rolled back after persistence failure: %s", path.c_str());
  }
}

void RecentBooksStore::recordReading(const std::string& path, uint8_t progress, uint32_t elapsedSeconds,
                                     const uint32_t pagesTurned) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("record_reading")) return;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return;
  const uint8_t nextProgress = std::min<uint8_t>(progress, 100);
  // A reader opened and closed without advancing or spending time does not
  // change Recent data, so skip the filesystem write entirely.
  if (elapsedSeconds == 0 && pagesTurned == 0 && it->progressPercent == nextProgress) return;
  const uint8_t oldProgress = it->progressPercent;
  const uint32_t oldReadingSeconds = it->readingSeconds;
  const uint32_t oldLastSessionSeconds = it->lastSessionSeconds;
  const uint32_t oldDailyReadingSeconds = it->dailyReadingSeconds;
  const uint32_t oldDailyReadingDateKey = it->dailyReadingDateKey;
  const uint16_t oldReadingSessions = it->readingSessions;
  const uint32_t oldPagesTurned = it->pagesTurned;
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
  it->pagesTurned += std::min<uint32_t>(pagesTurned, UINT32_MAX - it->pagesTurned);
  if (!saveToFileLocked()) {
    it->progressPercent = oldProgress;
    it->readingSeconds = oldReadingSeconds;
    it->lastSessionSeconds = oldLastSessionSeconds;
    it->dailyReadingSeconds = oldDailyReadingSeconds;
    it->dailyReadingDateKey = oldDailyReadingDateKey;
    it->readingSessions = oldReadingSessions;
    it->pagesTurned = oldPagesTurned;
    LOG_ERR("RBS", "Failed to persist reading statistics; reverted in-memory update");
  }
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath, const std::string& synopsis) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("update_book")) return;
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    const std::string oldTitle = book.title;
    const std::string oldAuthor = book.author;
    const std::string oldCoverBmpPath = book.coverBmpPath;
    const std::string oldSynopsis = book.synopsis;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    if (!synopsis.empty()) book.synopsis = synopsis.substr(0, 384);
    applyMetadataOverride(book);
    if (!saveToFileLocked()) {
      book.title = oldTitle;
      book.author = oldAuthor;
      book.coverBmpPath = oldCoverBmpPath;
      book.synopsis = oldSynopsis;
      LOG_ERR("RBS", "update_book rolled back after persistence failure: %s", path.c_str());
    }
  }
}

void RecentBooksStore::refreshBookMetadata(const std::string& path, const std::string& title,
                                           const std::string& author, const std::string& coverBmpPath,
                                           const std::string& synopsis) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("refresh_metadata")) return;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return;

  RecentBook& book = *it;
  const std::string oldTitle = book.title;
  const std::string oldAuthor = book.author;
  const std::string oldCoverBmpPath = book.coverBmpPath;
  const std::string oldSynopsis = book.synopsis;
  const std::string nextTitle = title.empty() ? book.title : title;
  const std::string nextAuthor = author.empty() ? book.author : author;
  const std::string nextCover = coverBmpPath.empty() ? book.coverBmpPath : coverBmpPath;
  // A device-side refresh must never erase a working synopsis because a
  // transient metadata pass returned an empty description. Explicit web
  // metadata edits use updateMetadata() and can still intentionally clear it.
  const std::string boundedSynopsis = synopsis.substr(0, 384);
  const std::string nextSynopsis = boundedSynopsis.empty() ? book.synopsis : boundedSynopsis;
  if (book.title == nextTitle && book.author == nextAuthor && book.coverBmpPath == nextCover &&
      book.synopsis == nextSynopsis) {
    return;
  }
  book.title = nextTitle;
  book.author = nextAuthor;
  book.coverBmpPath = nextCover;
  book.synopsis = nextSynopsis;
  applyMetadataOverride(book);
  if (!saveToFileLocked()) {
    book.title = oldTitle;
    book.author = oldAuthor;
    book.coverBmpPath = oldCoverBmpPath;
    book.synopsis = oldSynopsis;
    LOG_ERR("RBS", "Refreshed metadata rolled back after persistence failure: %s", path.c_str());
  }
}

void RecentBooksStore::refreshXtcMetadata(const std::string& path, const std::string& title,
                                          const std::string& author, const std::string& coverBmpPath,
                                          const std::string& synopsis, const std::string& series,
                                          const bool metadataChecked) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("refresh_xtc_metadata")) return;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return;

  RecentBook& book = *it;
  const std::string oldTitle = book.title;
  const std::string oldAuthor = book.author;
  const std::string oldCoverBmpPath = book.coverBmpPath;
  const std::string oldSynopsis = book.synopsis;
  const std::string oldSeries = book.series;
  const bool oldMetadataChecked = book.xtcMetadataChecked;
  const std::string nextTitle = title.empty() ? book.title : title;
  const std::string nextAuthor = author.empty() ? book.author : author;
  const std::string nextCover = coverBmpPath.empty() ? book.coverBmpPath : coverBmpPath;
  const std::string nextSynopsis = synopsis.empty() ? book.synopsis : synopsis.substr(0, 384);
  const std::string nextSeries = series.empty() ? book.series : series.substr(0, 128);
  const bool nextMetadataChecked = book.xtcMetadataChecked || metadataChecked;
  if (book.title == nextTitle && book.author == nextAuthor && book.coverBmpPath == nextCover &&
      book.synopsis == nextSynopsis && book.series == nextSeries &&
      book.xtcMetadataChecked == nextMetadataChecked) {
    return;
  }
  book.title = nextTitle;
  book.author = nextAuthor;
  book.coverBmpPath = nextCover;
  book.synopsis = nextSynopsis;
  book.series = nextSeries;
  book.xtcMetadataChecked = nextMetadataChecked;
  applyMetadataOverride(book);
  if (!saveToFileLocked()) {
    book.title = oldTitle;
    book.author = oldAuthor;
    book.coverBmpPath = oldCoverBmpPath;
    book.synopsis = oldSynopsis;
    book.series = oldSeries;
    book.xtcMetadataChecked = oldMetadataChecked;
    LOG_ERR("RBS", "Refreshed XTC metadata rolled back after persistence failure: %s", path.c_str());
  }
}

void RecentBooksStore::markXtcMetadataChecked(const std::string& path) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("mark_xtc_metadata_checked")) return;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end() || it->xtcMetadataChecked) return;
  it->xtcMetadataChecked = true;
  if (!saveToFileLocked()) {
    it->xtcMetadataChecked = false;
    LOG_ERR("RBS", "XTC metadata check rolled back after persistence failure: %s", path.c_str());
  }
}

bool RecentBooksStore::updateMetadata(const std::string& path, const std::string& title, const std::string& author,
                                      const std::string& synopsis) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("update_metadata")) return false;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return false;
  const std::string oldTitle = it->title;
  const std::string oldAuthor = it->author;
  const std::string oldSynopsis = it->synopsis;
  it->title = title.substr(0, 192);
  it->author = author.substr(0, 128);
  // Metadata edited explicitly through the web UI should not be truncated;
  // the shelf's generated EPUB cache remains bounded separately.
  it->synopsis = synopsis;
  if (!saveToFileLocked()) {
    it->title = oldTitle;
    it->author = oldAuthor;
    it->synopsis = oldSynopsis;
    LOG_ERR("RBS", "Failed to save edited metadata: %s", path.c_str());
    return false;
  }
  return true;
}

bool RecentBooksStore::resetReading(const std::string& path) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("reset_reading")) return false;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return false;
  const uint8_t oldProgress = it->progressPercent;
  const uint32_t oldReadingSeconds = it->readingSeconds;
  const uint32_t oldLastSessionSeconds = it->lastSessionSeconds;
  const uint32_t oldDailyReadingSeconds = it->dailyReadingSeconds;
  const uint32_t oldDailyReadingDateKey = it->dailyReadingDateKey;
  const uint16_t oldReadingSessions = it->readingSessions;
  const uint32_t oldPagesTurned = it->pagesTurned;
  it->progressPercent = 0;
  it->readingSeconds = 0;
  it->lastSessionSeconds = 0;
  it->dailyReadingSeconds = 0;
  it->dailyReadingDateKey = 0;
  it->readingSessions = 0;
  it->pagesTurned = 0;
  if (!saveToFileLocked()) {
    it->progressPercent = oldProgress;
    it->readingSeconds = oldReadingSeconds;
    it->lastSessionSeconds = oldLastSessionSeconds;
    it->dailyReadingSeconds = oldDailyReadingSeconds;
    it->dailyReadingDateKey = oldDailyReadingDateKey;
    it->readingSessions = oldReadingSessions;
    it->pagesTurned = oldPagesTurned;
    LOG_ERR("RBS", "Failed to reset reading data: %s", path.c_str());
    return false;
  }
  return true;
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("remove_by_path")) return false;
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  const size_t oldIndex = static_cast<size_t>(std::distance(recentBooks.begin(), it));
  RecentBook removed = std::move(*it);
  recentBooks.erase(it);
  if (!saveToFileLocked()) {
    recentBooks.insert(recentBooks.begin() + static_cast<std::ptrdiff_t>(oldIndex), std::move(removed));
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
    return false;
  }
  return true;
}

bool RecentBooksStore::clearAll() {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("clear_all")) return false;
  if (recentBooks.empty()) return true;
  std::vector<RecentBook> previous;
  previous.swap(recentBooks);
  if (!saveToFileLocked()) {
    recentBooks.swap(previous);
    LOG_ERR("RBS", "Failed to persist cleared recent books");
    return false;
  }
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (rejectUnsafeMutation("update_path")) return;
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  const std::string oldBookPath = it->path;
  const std::string oldCoverPath = it->coverBmpPath;
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  if (!saveToFileLocked()) {
    it->path = oldBookPath;
    it->coverBmpPath = oldCoverPath;
    LOG_ERR("RBS", "update_path rolled back after persistence failure: %s", oldPath.c_str());
  }
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

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
    RecentBook result{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath(),
                      epub.getDescription().substr(0, 384)};
    applyMetadataOverride(result);
    return result;
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      const std::string& synopsis = xtc.getDescription();
      const std::string& series = xtc.getSeries();
      RecentBook result{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath(), synopsis.substr(0, 384),
                        series.substr(0, 128)};
      result.xtcMetadataChecked = xtc.hasLoadedExtendedMetadata();
      applyMetadataOverride(result);
      return result;
    }
  } else if (FsHelpers::hasCbzExtension(lastBookFileName)) {
    // Recent must remain lightweight: only use the already-written CBZ
    // metadata/thumbnail cache and never rescan the archive during shelf boot.
    Cbz cbz(path, "/.crosspoint");
    if (cbz.loadCachedMetadataOnly()) {
      RecentBook result{path, cbz.getTitle(), cbz.getAuthor(), cbz.getThumbBmpPath(), cbz.getSynopsis().substr(0, 384)};
      applyMetadataOverride(result);
      return result;
    }
    const size_t dot = lastBookFileName.find_last_of('.');
    RecentBook result{path, dot == std::string::npos ? lastBookFileName : lastBookFileName.substr(0, dot), "", ""};
    applyMetadataOverride(result);
    return result;
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    RecentBook result{path, lastBookFileName, "", ""};
    applyMetadataOverride(result);
    return result;
  }
  return RecentBook{path, "", "", ""};
}
