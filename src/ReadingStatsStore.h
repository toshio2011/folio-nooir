#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <vector>

// One compact aggregate per calendar day. A date key of 0 means the device
// did not have a trusted clock when the session was committed.
struct ReadingDayStat {
  uint32_t dateKey = 0;
  uint32_t seconds = 0;
  uint16_t sessions = 0;
  uint32_t pagesTurned = 0;
};

class ReadingStatsStore : public PersistableStore<ReadingStatsStore> {
 private:
  std::vector<ReadingDayStat> days;
  static constexpr size_t MAX_DAYS = 730;

  ReadingStatsStore() = default;
  ~ReadingStatsStore() = default;
  friend class PersistableStore<ReadingStatsStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/reading-stats.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Commit one completed reader session. This is intentionally called on
  // reader exit/sleep, never from the page-turn path.
  void recordSession(uint32_t dateKey, uint32_t elapsedSeconds, uint32_t pagesTurned = 0);

  uint32_t totalSeconds() const;
  uint32_t totalSessions() const;
  uint32_t totalPagesTurned() const;
  uint32_t secondsForDate(uint32_t dateKey) const;
  uint16_t sessionsForDate(uint32_t dateKey) const;
  uint32_t pagesForDate(uint32_t dateKey) const;
  uint32_t currentStreakDays() const;
  uint32_t longestStreakDays() const;
  const std::vector<ReadingDayStat>& getDays() const { return days; }
  bool clearAll();
};

#define READING_STATS ReadingStatsStore::getInstance()
