#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

enum class BookStatus : uint8_t { New = 0, Reading = 1, OnHold = 2, Finished = 3 };

struct BookState {
  std::string path;
  BookStatus status = BookStatus::New;
  uint8_t progressPercent = 0;
  uint32_t startDate = 0;
  uint32_t finishDate = 0;
  uint32_t lastOpenedDate = 0;
  uint32_t readingSeconds = 0;
  uint16_t readingSessions = 0;
  uint32_t pagesTurned = 0;
};

class BookStateStore : public PersistableStore<BookStateStore> {
 private:
  std::vector<BookState> books;
  BookStateStore() = default;
  ~BookStateStore() = default;
  friend class PersistableStore<BookStateStore>;

  BookState& ensure(const std::string& path);

 public:
  static const char* getFilePath() { return "/.crosspoint/book-states.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  const BookState* find(const std::string& path) const;
  const std::vector<BookState>& getBooks() const { return books; }
  void recordReading(const std::string& path, uint8_t progress, uint32_t elapsedSeconds, uint32_t pagesTurned = 0);
  // Update fields exposed by the web book editor. Values are normalized but
  // otherwise kept exactly as supplied so users can correct dates manually.
  bool updateEditable(const std::string& path, BookStatus status, uint8_t progress, uint32_t startDate,
                      uint32_t finishDate);
  void setStatus(const std::string& path, BookStatus status);
  void reset(const std::string& path);
  bool removeByPath(const std::string& path);
  bool clearAll();
  void updatePath(const std::string& oldPath, const std::string& newPath);
};

#define BOOK_STATES BookStateStore::getInstance()
