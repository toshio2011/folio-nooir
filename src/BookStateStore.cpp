#include "BookStateStore.h"

#include <algorithm>

#include "HalClock.h"
#include "Logging.h"

void BookStateStore::toJson(JsonDocument& doc) const {
  JsonArray array = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    JsonObject item = array.add<JsonObject>();
    item["path"] = book.path;
    item["status"] = static_cast<uint8_t>(book.status);
    item["progress"] = book.progressPercent;
    item["startDate"] = book.startDate;
    item["finishDate"] = book.finishDate;
    item["lastOpenedDate"] = book.lastOpenedDate;
    item["readingSeconds"] = book.readingSeconds;
    item["sessions"] = book.readingSessions;
    item["pagesTurned"] = book.pagesTurned;
  }
}

bool BookStateStore::fromJson(JsonVariantConst doc) {
  books.clear();
  JsonArrayConst array = doc["books"].as<JsonArrayConst>();
  books.reserve(std::min<size_t>(array.size(), 128));
  for (JsonObjectConst item : array) {
    const char* path = item["path"] | "";
    if (!path[0]) continue;
    BookState state;
    state.path = path;
    state.status = static_cast<BookStatus>(std::min<uint8_t>(item["status"] | 0, 3));
    state.progressPercent = std::min<uint8_t>(item["progress"] | 0, 100);
    state.startDate = item["startDate"] | 0;
    state.finishDate = item["finishDate"] | 0;
    state.lastOpenedDate = item["lastOpenedDate"] | 0;
    state.readingSeconds = item["readingSeconds"] | 0;
    state.readingSessions = item["sessions"] | 0;
    state.pagesTurned = item["pagesTurned"] | 0;
    books.push_back(std::move(state));
  }
  return true;
}

BookState& BookStateStore::ensure(const std::string& path) {
  auto it = std::find_if(books.begin(), books.end(), [&](const BookState& state) { return state.path == path; });
  if (it != books.end()) return *it;
  books.push_back(BookState{});
  books.back().path = path;
  return books.back();
}

const BookState* BookStateStore::find(const std::string& path) const {
  const auto it = std::find_if(books.begin(), books.end(), [&](const BookState& state) { return state.path == path; });
  return it == books.end() ? nullptr : &*it;
}

void BookStateStore::recordReading(const std::string& path, const uint8_t progress, const uint32_t elapsedSeconds,
                                   const uint32_t pagesTurned) {
  BookState& state = ensure(path);
  const uint32_t today = halClock.getDateKey();
  state.progressPercent = std::min<uint8_t>(progress, 100);
  state.lastOpenedDate = today;
  if (state.progressPercent >= 100) {
    state.status = BookStatus::Finished;
    if (state.finishDate == 0) state.finishDate = today;
  } else if (state.progressPercent > 0 && state.status != BookStatus::OnHold) {
    state.status = BookStatus::Reading;
    if (state.startDate == 0) state.startDate = today;
    state.finishDate = 0;
  }
  if (elapsedSeconds > 0) {
    state.readingSeconds += std::min<uint32_t>(elapsedSeconds, UINT32_MAX - state.readingSeconds);
    if (state.readingSessions < UINT16_MAX) ++state.readingSessions;
  }
  state.pagesTurned += std::min<uint32_t>(pagesTurned, UINT32_MAX - state.pagesTurned);
  if (!saveToFile()) LOG_ERR("BST", "Failed to save reading state");
}

bool BookStateStore::updateEditable(const std::string& path, const BookStatus status, const uint8_t progress,
                                    const uint32_t startDate, const uint32_t finishDate) {
  if (path.empty()) return false;
  BookState& state = ensure(path);
  state.progressPercent = std::min<uint8_t>(progress, 100);
  state.status = status;
  state.startDate = startDate;
  state.finishDate = finishDate;
  if (state.progressPercent >= 100) state.status = BookStatus::Finished;
  if (state.status == BookStatus::Finished) state.progressPercent = 100;
  if (state.status != BookStatus::Finished && state.progressPercent < 100) state.finishDate = finishDate;
  if (!saveToFile()) {
    LOG_ERR("BST", "Failed to save edited book state: %s", path.c_str());
    return false;
  }
  return true;
}

void BookStateStore::setStatus(const std::string& path, const BookStatus status) {
  BookState& state = ensure(path);
  const uint32_t today = halClock.getDateKey();
  state.status = status;
  if (status == BookStatus::Finished) {
    state.progressPercent = 100;
    if (state.startDate == 0) state.startDate = today;
    state.finishDate = today;
  } else {
    if (state.progressPercent >= 100) state.progressPercent = 99;
    if (status == BookStatus::Reading && state.startDate == 0) state.startDate = today;
    if (status != BookStatus::Finished) state.finishDate = 0;
  }
  saveToFile();
}

void BookStateStore::reset(const std::string& path) {
  BookState& state = ensure(path);
  const std::string savedPath = state.path;
  state = BookState{};
  state.path = savedPath;
  saveToFile();
}

bool BookStateStore::removeByPath(const std::string& path) {
  const auto it = std::find_if(books.begin(), books.end(), [&](const BookState& state) { return state.path == path; });
  if (it == books.end()) return true;
  books.erase(it);
  if (!saveToFile()) {
    LOG_ERR("BST", "Failed to persist removed book state: %s", path.c_str());
    return false;
  }
  return true;
}

bool BookStateStore::clearAll() {
  if (books.empty()) return true;
  books.clear();
  if (!saveToFile()) {
    LOG_ERR("BST", "Failed to persist cleared book states");
    return false;
  }
  return true;
}

void BookStateStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  auto it = std::find_if(books.begin(), books.end(), [&](const BookState& state) { return state.path == oldPath; });
  if (it == books.end()) return;
  it->path = newPath;
  saveToFile();
}
