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

TEST(BookStateStore, ResetClearsReadingStateButKeepsRating) {
  Storage.reset();
  BookStateStore& store = BOOK_STATES;
  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/books/Reset.epub";
  book["status"] = static_cast<uint8_t>(BookStatus::Finished);
  book["progress"] = 100;
  book["startDate"] = uint32_t{20261001};
  book["finishDate"] = uint32_t{20261020};
  book["lastOpenedDate"] = uint32_t{20261020};
  book["readingSeconds"] = uint32_t{900};
  book["sessions"] = uint16_t{7};
  book["pagesTurned"] = uint32_t{123};
  book["rating"] = uint8_t{5};
  ASSERT_TRUE(store.fromJson(doc.as<JsonVariantConst>()));

  ASSERT_TRUE(store.reset("/books/Reset.epub"));
  const BookState* reset = store.find("/books/Reset.epub");
  ASSERT_NE(reset, nullptr);
  EXPECT_EQ(reset->status, BookStatus::New);
  EXPECT_EQ(reset->progressPercent, 0);
  EXPECT_EQ(reset->startDate, 0U);
  EXPECT_EQ(reset->finishDate, 0U);
  EXPECT_EQ(reset->lastOpenedDate, 0U);
  EXPECT_EQ(reset->readingSeconds, 0U);
  EXPECT_EQ(reset->readingSessions, 0U);
  EXPECT_EQ(reset->pagesTurned, 0U);
  EXPECT_EQ(reset->rating, 5);
  EXPECT_GT(Storage.writeCount, 0U);
}

TEST(BookStateStore, FailedResetRestoresPreviousMemoryState) {
  Storage.reset();
  BookStateStore& store = BOOK_STATES;
  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/books/Failed.epub";
  book["status"] = static_cast<uint8_t>(BookStatus::Reading);
  book["progress"] = 42;
  book["readingSeconds"] = uint32_t{300};
  book["sessions"] = uint16_t{2};
  book["pagesTurned"] = uint32_t{31};
  book["rating"] = uint8_t{4};
  ASSERT_TRUE(store.fromJson(doc.as<JsonVariantConst>()));
  Storage.failWrite = true;

  EXPECT_FALSE(store.reset("/books/Failed.epub"));
  const BookState* restored = store.find("/books/Failed.epub");
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->status, BookStatus::Reading);
  EXPECT_EQ(restored->progressPercent, 42);
  EXPECT_EQ(restored->readingSeconds, 300U);
  EXPECT_EQ(restored->readingSessions, 2U);
  EXPECT_EQ(restored->pagesTurned, 31U);
  EXPECT_EQ(restored->rating, 4);
}
