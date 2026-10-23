#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum class RecentBooksLoadResult : uint8_t {
  NotLoaded,
  Valid,
  Recovered,
  MissingFirstBoot,
  Uncertain,
};

const char* recentBooksLoadResultName(RecentBooksLoadResult result);

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  std::string synopsis;
  // Optional XTC/XTCH series value, bounded when persisted to recent.json.
  std::string series;
  uint8_t progressPercent = 0;
  uint32_t readingSeconds = 0;
  uint32_t lastSessionSeconds = 0;
  uint32_t dailyReadingSeconds = 0;
  uint32_t dailyReadingDateKey = 0;
  uint16_t readingSessions = 0;
  uint32_t pagesTurned = 0;
  // Old recent.json files omit this key and remain valid. Checked absent or
  // invalid trailers are cached; transient storage failures remain retryable.
  bool xtcMetadataChecked = false;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore : public PersistableStore<RecentBooksStore> {
 private:
  std::vector<RecentBook> recentBooks;
  mutable std::atomic<RecentBooksLoadResult> loadResult{RecentBooksLoadResult::NotLoaded};
  bool hasLoadedValidHistory = false;

  RecentBooksStore() = default;
  ~RecentBooksStore() = default;

  friend class PersistableStore<RecentBooksStore>;

  // Called only by the CRTP persistence layer while storeMutex is held.
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  bool saveToFileLocked() const;
  static bool parseJsonBooks(JsonVariantConst doc, std::vector<RecentBook>& parsedBooks);
  bool rejectUnsafeMutation(const char* operation) const;

 public:
  // A no-allocation, read-only view that keeps the store locked for the full
  // lifetime of the view. Keep a named view alive while retaining iterators or
  // references into its books.
  class RecentBooksReadView {
   public:
    using const_iterator = std::vector<RecentBook>::const_iterator;

    RecentBooksReadView(const RecentBooksReadView&) = delete;
    RecentBooksReadView& operator=(const RecentBooksReadView&) = delete;
    RecentBooksReadView(RecentBooksReadView&&) noexcept = default;
    RecentBooksReadView& operator=(RecentBooksReadView&&) = delete;

    const_iterator begin() const noexcept { return books_.begin(); }
    const_iterator end() const noexcept { return books_.end(); }
    const_iterator cbegin() const noexcept { return books_.cbegin(); }
    const_iterator cend() const noexcept { return books_.cend(); }
    const RecentBook& operator[](size_t index) const { return books_[index]; }
    const RecentBook& front() const { return books_.front(); }
    size_t size() const noexcept { return books_.size(); }
    bool empty() const noexcept { return books_.empty(); }
    void copyTo(std::vector<RecentBook>& destination) const { destination = books_; }

   private:
    friend class RecentBooksStore;
    explicit RecentBooksReadView(const RecentBooksStore& store)
        : lock_(store.storeMutex), books_(store.recentBooks) {}

    std::unique_lock<std::mutex> lock_;
    const std::vector<RecentBook>& books_;
  };

  static const char* getFilePath() { return "/.crosspoint/recent.json"; }
  // Recent history has stronger durability rules than disposable cache stores.
  // A failed/corrupt load is kept distinct from an intentional empty list so
  // callers cannot overwrite existing history from an empty startup singleton.
  bool loadFromFile();
  bool saveToFile() const;
  RecentBooksLoadResult getLoadResult() const { return loadResult.load(); }
  bool automaticWritesAllowed() const {
    const RecentBooksLoadResult result = loadResult.load();
    return result == RecentBooksLoadResult::Valid || result == RecentBooksLoadResult::Recovered ||
           result == RecentBooksLoadResult::MissingFirstBoot;
  }

  // Add a book to the recent list (moves to front if already exists)
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath, const std::string& synopsis = {}, const std::string& series = {},
               bool xtcMetadataChecked = false);

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath, const std::string& synopsis = {});
  // Replace all presentation metadata, including an intentionally empty
  // synopsis. Used by Refresh Book Cache after rereading the source file.
  void refreshBookMetadata(const std::string& path, const std::string& title, const std::string& author,
                           const std::string& coverBmpPath, const std::string& synopsis);
  // Cache optional XTC synopsis/series and the parser checked state. A
  // transient read failure stays retryable; progress and bookmarks survive.
  void refreshXtcMetadata(const std::string& path, const std::string& title, const std::string& author,
                          const std::string& coverBmpPath, const std::string& synopsis, const std::string& series,
                          bool metadataChecked = false);
  void markXtcMetadataChecked(const std::string& path);
  bool updateMetadata(const std::string& path, const std::string& title, const std::string& author,
                      const std::string& synopsis);
  bool resetReading(const std::string& path);

  // Persist one completed reader session. This is called only when leaving a
  // book, keeping SD writes away from the page-turn path.
  void recordReading(const std::string& path, uint8_t progressPercent, uint32_t elapsedSeconds,
                     uint32_t pagesTurned = 0);

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

  // Get a locked, read-only list of recent books (most recent first).
  RecentBooksReadView getBooks() const { return RecentBooksReadView(*this); }

  // Copy one record while holding the store lock, then release it before any
  // caller performs SD/network work. This keeps long-running web responses
  // from blocking Recent mutations without allocating a full-list snapshot.
  bool getBookAt(size_t index, RecentBook& destination) const;

  // Get the count of recent books
  int getCount() const;

  RecentBook getDataFromBook(std::string path) const;
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()
