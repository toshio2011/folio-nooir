#include <ArduinoJson.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

TEST(ReadingStatsStore, SkipsEmptySessionWritesAndPersistsRealSessionOnce) {
  Storage.reset();
  ReadingStatsStore& store = READING_STATS;
  JsonDocument empty;
  empty["days"].to<JsonArray>();
  ASSERT_TRUE(store.fromJson(empty.as<JsonVariantConst>()));
  Storage.writeCount = 0;

  store.recordSession(20261028, 0, 0);
  EXPECT_EQ(Storage.writeCount, 0U);
  EXPECT_EQ(store.totalSessions(), 0U);

  store.recordSession(20261028, 45, 3);
  EXPECT_EQ(Storage.writeCount, 1U);
  EXPECT_EQ(store.totalSeconds(), 45U);
  EXPECT_EQ(store.totalSessions(), 1U);
  EXPECT_EQ(store.totalPagesTurned(), 3U);

  Storage.writeCount = 0;
  store.recordSession(20261028, 0, 0);
  EXPECT_EQ(Storage.writeCount, 0U);
  EXPECT_EQ(store.totalSessions(), 1U);
}
