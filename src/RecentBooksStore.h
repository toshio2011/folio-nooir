#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  std::string synopsis;
  uint8_t progressPercent = 0;
  uint32_t readingSeconds = 0;
  uint32_t lastSessionSeconds = 0;
  uint32_t dailyReadingSeconds = 0;
  uint32_t dailyReadingDateKey = 0;
  uint16_t readingSessions = 0;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore : public PersistableStore<RecentBooksStore> {
 private:
  std::vector<RecentBook> recentBooks;

  static constexpr int MAX_RECENT_BOOKS = 10;

  RecentBooksStore() = default;
  ~RecentBooksStore() = default;

  friend class PersistableStore<RecentBooksStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/recent.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Add a book to the recent list (moves to front if already exists)
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath, const std::string& synopsis = {});

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath, const std::string& synopsis = {});
  // Replace all presentation metadata, including an intentionally empty
  // synopsis. Used by Refresh Book Cache after rereading the source file.
  void refreshBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                           const std::string& coverBmpPath, const std::string& synopsis);
  bool updateMetadata(const std::string& path, const std::string& title, const std::string& author,
                      const std::string& synopsis);
  bool resetReading(const std::string& path);

  // Persist one completed reader session. This is called only when leaving a
  // book, keeping SD writes away from the page-turn path.
  void recordReading(const std::string& path, uint8_t progressPercent, uint32_t elapsedSeconds);

  // Remove the entry whose path matches (used when a book is removed from recents or finished/read).
  // Returns true if an entry was found and removed (no-op + false otherwise).
  // Persistence is best-effort: a failed save is logged, not reflected in the return.
  bool removeByPath(const std::string& path);

  // Clear the Recent/Finished library without touching ebook files.
  bool clearAll();

  // Repoint an entry's path (and coverBmpPath, if it lived under the old cache dir) after the
  // backing file and cache dir were moved on disk. No-op if no entry matches oldPath.
  // Persists on success. Keeps the entry's list position (does not reorder).
  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  // True if the book's backing file is no longer present on the SD card.
  static bool isMissing(const RecentBook& book);

  // Remove entries whose backing file is no longer on the SD card.
  // Returns true if any entry was removed. Does not persist — caller decides.
  bool pruneMissing();

  // Get the list of recent books (most recent first)
  const std::vector<RecentBook>& getBooks() const { return recentBooks; }

  // Get the count of recent books
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  RecentBook getDataFromBook(std::string path) const;
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
