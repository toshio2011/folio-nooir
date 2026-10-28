#include <ArduinoJson.h>
#include <BookStateStore.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

TEST(BookStateStore, SkipsUnchangedReadingWriteButPersistsProgressAndSessionChanges) {
  Storage.reset();
  BookStateStore& store = BOOK_STATES;
  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/books/Unchanged.epub";
  book["status"] = static_cast<uint8_t>(BookStatus::Reading);
  book["progress"] = 50;
  book["startDate"] = uint32_t{20261022};
  book["lastOpenedDate"] = uint32_t{20261022};
  book["readingSeconds"] = uint32_t{120};
  book["sessions"] = uint16_t{2};
  book["pagesTurned"] = uint32_t{8};
  ASSERT_TRUE(store.fromJson(doc.as<JsonVariantConst>()));

  store.recordReading("/books/Unchanged.epub", 50, 0, 0);
  EXPECT_EQ(Storage.writeCount, 0U);

  store.recordReading("/books/Unchanged.epub", 51, 0, 0);
  EXPECT_GT(Storage.writeCount, 0U);
  const BookState* changed = store.find("/books/Unchanged.epub");
  ASSERT_NE(changed, nullptr);
  EXPECT_EQ(changed->progressPercent, 51);

  Storage.writeCount = 0;
  store.recordReading("/books/Unchanged.epub", 51, 10, 2);
  EXPECT_GT(Storage.writeCount, 0U);
  const BookState* sessionUpdated = store.find("/books/Unchanged.epub");
  ASSERT_NE(sessionUpdated, nullptr);
  EXPECT_EQ(sessionUpdated->readingSeconds, 130U);
  EXPECT_EQ(sessionUpdated->readingSessions, 3U);
  EXPECT_EQ(sessionUpdated->pagesTurned, 10U);
}

TEST(BookStateStore, PersistsFirstReadingObservationForAnUnknownBook) {
  Storage.reset();
  BookStateStore& store = BOOK_STATES;
  store.recordReading("/books/New.epub", 0, 0, 0);
  EXPECT_GT(Storage.writeCount, 0U);
  const BookState* created = store.find("/books/New.epub");
  ASSERT_NE(created, nullptr);
  EXPECT_EQ(created->lastOpenedDate, 20261022U);
}
