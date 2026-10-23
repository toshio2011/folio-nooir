#include <ArduinoJson.h>
#include <HalStorage.h>
#include <RecentBooksStore.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <BookMetadataOverridesStore.h>

namespace {

constexpr const char* kPrimary = "/.crosspoint/recent.json";
constexpr const char* kBackup = "/.crosspoint/recent.json.bak";
constexpr const char* kTemporary = "/.crosspoint/recent.json.tmp";

std::string makeHistory(const size_t count, const bool includeOptionalFields = true) {
  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  for (size_t index = 0; index < count; ++index) {
    JsonObject book = books.add<JsonObject>();
    const std::string path = "/manga/Book " + std::to_string(index) + (index % 2 ? ".xtc" : ".xtch");
    book["path"] = path;
    book["title"] = "Book " + std::to_string(index);
    book["author"] = "Author";
    book["progressPercent"] = static_cast<uint8_t>(index % 100);
    if (includeOptionalFields) {
      book["series"] = "Series " + std::to_string(index);
      book["xtcMetadataChecked"] = (index % 2) == 0;
    }
  }
  String serialized;
  serializeJson(doc, serialized);
  return serialized.c_str();
}

std::string makeBookWithField(const std::string& field, const std::string& encodedValue) {
  return "{\"books\":[{\"path\":\"/manga/Typed.xtc\",\"title\":\"Typed\",\"author\":\"Author\",\"" +
         field + "\":" + encodedValue + "}]}";
}

std::string readTruncatedFixture() {
  std::ifstream input(RECENT_BOOKS_FIXTURE_PATH, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

}  // namespace

// The suite intentionally runs in one process so the test exercises recovery
// from an already-populated singleton after later reads fail.
TEST(RecentBooksStore, ProtectsHistoryAcrossCorruptionRecoveryAndFailedWrites) {
  Storage.reset();
  RecentBooksStore& store = RECENT_BOOKS;

  // A genuinely empty SD card on first boot is distinct from an unreadable or
  // malformed history and is the only empty state that permits initial writes.
  EXPECT_FALSE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::MissingFirstBoot);
  EXPECT_TRUE(store.automaticWritesAllowed());
  store.addBook("/manga/First.xtch", "First", "", "");
  ASSERT_EQ(Storage.writeCount, 1U);
  ASSERT_EQ(store.getCount(), 1);
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  EXPECT_EQ(store.getCount(), 1);

  // A long valid history remains intact, and newer fields round-trip. Older
  // entries with the optional XTC fields omitted continue to load as defaults.
  Storage.files[kPrimary] = makeHistory(28);
  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  ASSERT_EQ(store.getCount(), 28);
  std::atomic<bool> mutationStarted{false};
  std::atomic<bool> mutationFinished{false};
  std::atomic<bool> countStarted{false};
  std::atomic<bool> countFinished{false};
  bool mutationWaitedForView = false;
  bool countWaitedForView = false;
  std::thread mutation;
  std::thread countQuery;
  {
    auto books = store.getBooks();
    EXPECT_EQ(books[0].series, "Series 0");
    EXPECT_TRUE(books[0].xtcMetadataChecked);

    // A live read view keeps mutations behind the same mutex. This no-op
    // mutation must wait for the view to release its lock.
    mutation = std::thread([&] {
      mutationStarted.store(true);
      store.markXtcMetadataChecked("/manga/Book 0.xtch");
      mutationFinished.store(true);
    });
    countQuery = std::thread([&] {
      countStarted.store(true);
      (void)store.getCount();
      countFinished.store(true);
    });
    while (!mutationStarted.load() || !countStarted.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mutationWaitedForView = !mutationFinished.load();
    countWaitedForView = !countFinished.load();
  }
  mutation.join();
  countQuery.join();
  EXPECT_TRUE(mutationWaitedForView);
  EXPECT_TRUE(countWaitedForView);
  Storage.files[kPrimary] = makeHistory(2, false);
  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getCount(), 2);
  EXPECT_TRUE(store.getBooks()[0].series.empty());
  EXPECT_FALSE(store.getBooks()[0].xtcMetadataChecked);

  // Invalid schema/item JSON is rejected transactionally instead of clearing
  // the populated vector or accepting a partial prefix.
  const std::string retainedPath = store.getBooks()[0].path;
  Storage.files[kPrimary] = "{\"books\":[{\"path\":\"/manga/good.xtc\"},{\"title\":\"no path\"}]}";
  EXPECT_FALSE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
  ASSERT_EQ(store.getCount(), 2);
  EXPECT_EQ(store.getBooks()[0].path, retainedPath);

  // The supplied attachment is truncated mid-string. Use this sanitized
  // equivalent fixture, never the user's personal library file, as the
  // persisted primary. Failed load must retain existing memory and all
  // recovery candidates; attempted mutation/save cannot replace them.
  const std::string truncated = readTruncatedFixture();
  ASSERT_FALSE(truncated.empty());
  Storage.files[kPrimary] = truncated;
  Storage.files[kBackup] = "{\"books\":[";
  Storage.files[kTemporary] = "{\"books\":[";
  const std::string originalPrimary = Storage.files[kPrimary];
  const std::string originalBackup = Storage.files[kBackup];
  const std::string originalTemporary = Storage.files[kTemporary];
  const std::string memoryPathBeforeFailure = store.getBooks()[0].path;
  EXPECT_FALSE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
  EXPECT_FALSE(store.automaticWritesAllowed());
  ASSERT_EQ(store.getCount(), 2);
  EXPECT_EQ(store.getBooks()[0].path, memoryPathBeforeFailure);
  const size_t writesBeforeBlockedMutations = Storage.writeCount;
  store.addBook("/manga/Should Not Replace History.xtc", "Unsafe", "", "");
  store.recordReading(memoryPathBeforeFailure, 91, 30, 4);
  EXPECT_FALSE(store.saveToFile());
  EXPECT_EQ(Storage.writeCount, writesBeforeBlockedMutations);
  EXPECT_EQ(Storage.files[kPrimary], originalPrimary);
  EXPECT_EQ(Storage.files[kBackup], originalBackup);
  EXPECT_EQ(Storage.files[kTemporary], originalTemporary);
  EXPECT_EQ(store.getBooks()[0].path, memoryPathBeforeFailure);

  // A valid backup can recover a malformed main file. Files remain in place
  // until a later explicit/automatic save is allowed by a complete load.
  Storage.files[kPrimary] = truncated;
  Storage.files[kBackup] = makeHistory(4);
  Storage.files.erase(kTemporary);
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Recovered);
  EXPECT_EQ(store.getCount(), 4);
  EXPECT_EQ(Storage.files[kPrimary], truncated);
  EXPECT_EQ(Storage.files[kBackup], makeHistory(4));

  // The first subsequent save from recovered history keeps the only known-good
  // backup at its canonical path and quarantines the malformed source primary.
  store.addBook("/manga/Recovered Save.xtc", "Recovered Save", "", "");
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  EXPECT_EQ(Storage.files[kBackup], makeHistory(4));
  EXPECT_NE(Storage.files[kPrimary], truncated);
  EXPECT_NE(Storage.files[kPrimary].find("Recovered Save"), std::string::npos);
  bool foundQuarantinedPrimary = false;
  for (const auto& [path, contents] : Storage.files) {
    if (path.find(std::string(kPrimary) + ".recovered-primary-") == 0 && contents == truncated) {
      foundQuarantinedPrimary = true;
      break;
    }
  }
  EXPECT_TRUE(foundQuarantinedPrimary);

  // A primary read error is not evidence of corruption. A fallback may be
  // shown, but writes stay blocked because primary could contain newer data.
  Storage.files[kPrimary] = makeHistory(7);
  Storage.files[kBackup] = makeHistory(4);
  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getCount(), 7);
  Storage.readErrors[kPrimary] = true;
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
  EXPECT_EQ(store.getCount(), 7);
  EXPECT_FALSE(store.automaticWritesAllowed());
  EXPECT_EQ(Storage.files[kPrimary], makeHistory(7));
  EXPECT_EQ(Storage.files[kBackup], makeHistory(4));
  const size_t writesBeforeReadErrorMutations = Storage.writeCount;
  store.addBook("/manga/Transient Read.xtc", "Transient Read", "", "");
  store.recordReading("/manga/Book 0.xtch", 99, 50, 7);
  EXPECT_EQ(Storage.writeCount, writesBeforeReadErrorMutations);
  EXPECT_EQ(Storage.files[kPrimary], makeHistory(7));
  EXPECT_EQ(Storage.files[kBackup], makeHistory(4));
  EXPECT_EQ(store.getCount(), 7);

  // A valid temporary file can be rendered during recovery, but must remain
  // protected because it is also the generic writer's next staging path.
  Storage.readErrors.clear();
  Storage.files.clear();
  Storage.files[kTemporary] = makeHistory(6);
  const std::string temporaryOnly = Storage.files[kTemporary];
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
  EXPECT_EQ(store.getCount(), 7);
  const size_t writesBeforeTempOnlyMutation = Storage.writeCount;
  store.addBook("/manga/Keep Temp.xtch", "Keep Temp", "", "");
  EXPECT_EQ(Storage.writeCount, writesBeforeTempOnlyMutation);
  EXPECT_EQ(Storage.files[kTemporary], temporaryOnly);

  // Unavailable storage and all-candidate disappearance after a previously
  // valid load remain uncertain, never mistaken for first boot.
  Storage.sdReady = false;
  EXPECT_FALSE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
  EXPECT_EQ(store.getCount(), 7);
  Storage.sdReady = true;
  Storage.files.clear();
  EXPECT_FALSE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
  EXPECT_EQ(store.getCount(), 7);

  // A syntactically and structurally valid empty history is intentional, and
  // a subsequent save failure rolls the in-memory list back as well.
  Storage.files[kPrimary] = "{\"books\":[]}";
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  EXPECT_EQ(store.getCount(), 0);
  Storage.failWrite = true;
  store.addBook("/manga/Failed Save.xtch", "Failed Save", "", "");
  EXPECT_EQ(store.getCount(), 0);
  EXPECT_EQ(Storage.files[kPrimary], "{\"books\":[]}");
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
}

TEST(RecentBooksStore, RejectsInvalidPresentFieldsWithoutReplacingLoadedHistory) {
  Storage.reset();
  RecentBooksStore& store = RECENT_BOOKS;
  Storage.files[kPrimary] = makeHistory(2);
  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  ASSERT_EQ(store.getCount(), 2);
  const std::string retainedPath = store.getBooks()[0].path;

  const std::pair<std::string, std::string> invalidFields[] = {
      {"title", "42"},
      {"author", "null"},
      {"coverBmpPath", "false"},
      {"synopsis", "[]"},
      {"series", "{\"name\":\"Series\"}"},
      {"xtcMetadataChecked", "\"true\""},
      {"progressPercent", "101"},
      {"progressPercent", "-1"},
      {"progressPercent", "1.5"},
      {"progressPercent", "\"50\""},
      {"readingSeconds", "-1"},
      {"lastSessionSeconds", "4294967296"},
      {"dailyReadingSeconds", "1.5"},
      {"dailyReadingDateKey", "\"20261023\""},
      {"readingSessions", "65536"},
      {"readingSessions", "-1"},
      {"pagesTurned", "4294967296"},
  };

  for (const auto& [field, value] : invalidFields) {
    SCOPED_TRACE(field + "=" + value);
    Storage.files[kPrimary] = makeBookWithField(field, value);
    EXPECT_FALSE(store.loadFromFile());
    EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Uncertain);
    EXPECT_FALSE(store.automaticWritesAllowed());
    ASSERT_EQ(store.getCount(), 2);
    EXPECT_EQ(store.getBooks()[0].path, retainedPath);
  }

  // The full unsigned range remains valid for accumulated counters and date
  // keys. Progress and the narrower session count retain their domain limits.
  Storage.files[kPrimary] =
      "{\"books\":[{\"path\":\"/manga/Bounds.xtc\",\"progressPercent\":100,"
      "\"readingSeconds\":4294967295,\"lastSessionSeconds\":4294967295,"
      "\"dailyReadingSeconds\":4294967295,\"dailyReadingDateKey\":4294967295,"
      "\"readingSessions\":65535,\"pagesTurned\":4294967295}]}";
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  ASSERT_EQ(store.getCount(), 1);
  const RecentBook& loaded = store.getBooks()[0];
  EXPECT_EQ(loaded.progressPercent, 100);
  EXPECT_EQ(loaded.readingSeconds, UINT32_MAX);
  EXPECT_EQ(loaded.lastSessionSeconds, UINT32_MAX);
  EXPECT_EQ(loaded.dailyReadingSeconds, UINT32_MAX);
  EXPECT_EQ(loaded.dailyReadingDateKey, UINT32_MAX);
  EXPECT_EQ(loaded.readingSessions, UINT16_MAX);
  EXPECT_EQ(loaded.pagesTurned, UINT32_MAX);
}

const BookMetadataOverride* BookMetadataOverridesStore::find(const std::string&) const { return nullptr; }
