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
#include <utility>

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

std::string makeDurableHistory() {
  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/manga/Original.xtch";
  book["title"] = "Original title";
  book["author"] = "Original author";
  book["coverBmpPath"] = "/.crosspoint/original/cover.bmp";
  book["synopsis"] = "Original synopsis";
  book["series"] = "Original series";
  book["xtcMetadataChecked"] = true;
  book["progressPercent"] = 73;
  book["readingSeconds"] = uint32_t{12345};
  book["lastSessionSeconds"] = uint32_t{321};
  book["dailyReadingSeconds"] = uint32_t{987};
  book["dailyReadingDateKey"] = uint32_t{20261023};
  book["readingSessions"] = uint16_t{42};
  book["pagesTurned"] = uint32_t{333};
  String serialized;
  serializeJson(doc, serialized);
  return serialized.c_str();
}

void expectOriginalBookData(const RecentBook& book) {
  EXPECT_EQ(book.path, "/manga/Original.xtch");
  EXPECT_EQ(book.title, "Original title");
  EXPECT_EQ(book.author, "Original author");
  EXPECT_EQ(book.coverBmpPath, "/.crosspoint/original/cover.bmp");
  EXPECT_EQ(book.synopsis, "Original synopsis");
  EXPECT_EQ(book.series, "Original series");
  EXPECT_TRUE(book.xtcMetadataChecked);
  EXPECT_EQ(book.progressPercent, 73);
  EXPECT_EQ(book.readingSeconds, 12345U);
  EXPECT_EQ(book.lastSessionSeconds, 321U);
  EXPECT_EQ(book.dailyReadingSeconds, 987U);
  EXPECT_EQ(book.dailyReadingDateKey, 20261023U);
  EXPECT_EQ(book.readingSessions, 42U);
  EXPECT_EQ(book.pagesTurned, 333U);
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

  // Moving the lock-backed view transfers ownership without copying any book
  // strings, and explicit reset releases the store for the next operation.
  RecentBooksStore::RecentBooksReadView movedView;
  {
    auto sourceView = store.getBooks();
    ASSERT_TRUE(sourceView.ownsLock());
    movedView = std::move(sourceView);
    EXPECT_FALSE(sourceView.ownsLock());
    EXPECT_TRUE(movedView.ownsLock());
    EXPECT_EQ(movedView.size(), 28U);
  }
  movedView.reset();
  EXPECT_FALSE(movedView.ownsLock());
  EXPECT_EQ(store.getCount(), 28);

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

TEST(RecentBooksStore, RecoversPreviousHistoryAfterPowerLossMovingPrimaryToBackup) {
  Storage.reset();
  RecentBooksStore& store = RECENT_BOOKS;
  const std::string original = makeDurableHistory();
  Storage.files[kPrimary] = original;

  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  ASSERT_EQ(store.getCount(), 1);

  // The first rename for a save with no existing backup is the committed
  // primary moving to .bak. Throw immediately after that filesystem step to
  // model power loss before the staged replacement can become the primary.
  Storage.armCrash(TestStorage::CrashPoint::AfterFirstRename);
  EXPECT_THROW(store.addBook("/manga/Added.xtch", "Added title", "Added author", "", "", "", true),
               SimulatedPowerLoss);
  ASSERT_FALSE(Storage.files.contains(kPrimary));
  ASSERT_EQ(Storage.files[kBackup], original);
  ASSERT_TRUE(Storage.files.contains(kTemporary));

  // Reload the singleton as a reboot would. The canonical backup wins over
  // the staged, uncommitted Recent document, retaining every user field.
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Recovered);
  ASSERT_EQ(store.getCount(), 1);
  RecentBook recovered;
  ASSERT_TRUE(store.getBookAt(0, recovered));
  expectOriginalBookData(recovered);
  EXPECT_EQ(Storage.files[kBackup], original);
  EXPECT_TRUE(Storage.files.contains(kTemporary));
}

TEST(RecentBooksStore, LoadsCommittedHistoryAfterPowerLossPromotingStagedTemporaryFile) {
  Storage.reset();
  RecentBooksStore& store = RECENT_BOOKS;
  const std::string original = makeDurableHistory();
  Storage.files[kPrimary] = original;

  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  ASSERT_EQ(store.getCount(), 1);

  // The second rename promotes the fully read-back-validated .tmp after the
  // old primary has already been retained as .bak.
  Storage.armCrash(TestStorage::CrashPoint::AfterSecondRename);
  EXPECT_THROW(store.addBook("/manga/Added.xtch", "Added title", "Added author", "", "", "", true),
               SimulatedPowerLoss);

  ASSERT_TRUE(Storage.files.contains(kPrimary));
  ASSERT_EQ(Storage.files[kBackup], original);
  EXPECT_FALSE(Storage.files.contains(kTemporary));

  // Reload the primary after the simulated reset. The committed new record
  // and all fields of the previous record must both survive.
  ASSERT_TRUE(store.loadFromFile());
  EXPECT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  ASSERT_EQ(store.getCount(), 2);
  RecentBook added;
  ASSERT_TRUE(store.getBookAt(0, added));
  EXPECT_EQ(added.path, "/manga/Added.xtch");
  EXPECT_EQ(added.title, "Added title");
  EXPECT_EQ(added.author, "Added author");
  EXPECT_EQ(added.series, "");
  EXPECT_TRUE(added.xtcMetadataChecked);
  RecentBook originalAfterCommit;
  ASSERT_TRUE(store.getBookAt(1, originalAfterCommit));
  expectOriginalBookData(originalAfterCommit);
  EXPECT_EQ(Storage.files[kBackup], original);
}

TEST(RecentBooksStore, BackupRotationDoesNotScanLegacyOrphanSiblingNames) {
  Storage.reset();
  RecentBooksStore& store = RECENT_BOOKS;
  const std::string original = makeDurableHistory();
  Storage.files[kPrimary] = original;
  Storage.files[kBackup] = "{\"books\":[]}";
  for (size_t index = 0; index < 1000; ++index) {
    Storage.files[std::string(kBackup) + ".previous-" + std::to_string(index)] = "preserved";
  }

  ASSERT_TRUE(store.loadFromFile());
  ASSERT_EQ(store.getLoadResult(), RecentBooksLoadResult::Valid);
  const size_t existsBeforeSave = Storage.existsCount;
  store.addBook("/manga/Added.xtch", "Added title", "Added author", "", "", "", true);

  // The old linear allocator made up to 1,000 exists() calls as orphaned
  // .previous-N files accumulated. Rotation now probes only its nonce name.
  EXPECT_LT(Storage.existsCount - existsBeforeSave, 10u);
  EXPECT_EQ(Storage.files[kBackup], original);
  EXPECT_EQ(Storage.files[std::string(kBackup) + ".previous-999"], "preserved");
  bool foundPriorBackup = false;
  for (const auto& [path, contents] : Storage.files) {
    if (path.find(std::string(kBackup) + ".previous-") == 0 && contents == "{\"books\":[]}") {
      foundPriorBackup = true;
      break;
    }
  }
  EXPECT_TRUE(foundPriorBackup);
  EXPECT_EQ(Storage.files.size(), 1003u);
}

const BookMetadataOverride* BookMetadataOverridesStore::find(const std::string&) const { return nullptr; }
