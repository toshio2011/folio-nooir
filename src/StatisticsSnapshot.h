#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "BookStateStore.h"
#include "ReadingStatsStore.h"

struct StatisticsWeekDay {
  uint32_t seconds = 0;
  uint16_t sessions = 0;
  uint32_t pages = 0;
};

struct StatisticsOverview {
  uint32_t todaySeconds = 0;
  uint16_t todaySessions = 0;
  uint32_t todayPages = 0;
  uint32_t retainedSeconds = 0;
  uint32_t retainedSessions = 0;
  uint32_t retainedPages = 0;
  uint32_t trackedSeconds = 0;
  uint32_t trackedSessions = 0;
  uint32_t trackedPages = 0;
  uint32_t currentStreak = 0;
  uint32_t longestStreak = 0;
  uint16_t booksStarted = 0;
  uint16_t booksFinished = 0;
  uint16_t activeDays = 0;
  std::array<StatisticsWeekDay, 7> week{};
};

struct StatisticsBookSnapshot {
  std::string path;
  std::string title;
  std::string author;
  std::string coverTemplatePath;
  std::string coverPath220;
  BookStatus status = BookStatus::New;
  uint8_t progress = 0;
  uint32_t readingSeconds = 0;
  uint16_t sessions = 0;
  uint32_t pages = 0;
  uint32_t startDate = 0;
  uint32_t finishDate = 0;
  uint32_t lastOpenedDate = 0;
  bool coverChecked = false;
  bool coverAvailable = false;
};

enum class StatisticsAchievementId : uint8_t {
  FirstSession,
  FirstStartedBook,
  FirstFinishedBook,
  Finished5,
  Finished10,
  Finished25,
  ReadingHour1,
  ReadingHours10,
  ReadingHours50,
  ReadingHours100,
  Pages500,
  Pages1000,
  Pages5000,
  Sessions10,
  Sessions50,
  Sessions100,
  Streak3,
  Streak7,
  Streak30,
  ActiveDays30,
};

enum class StatisticsAchievementUnit : uint8_t { Count, Seconds, Days };

struct StatisticsAchievementSnapshot {
  StatisticsAchievementId id;
  StatisticsAchievementUnit unit = StatisticsAchievementUnit::Count;
  uint32_t current = 0;
  uint32_t target = 0;
  bool earned = false;
};

struct StatisticsSnapshotOptions {
  bool keepDailyHistory = true;
  bool keepAllBooks = true;
  bool evaluateAchievements = true;
  std::string selectedBookPath;
};

struct StatisticsSnapshot {
  uint32_t todayDateKey = 0;
  StatisticsOverview overview;
  std::vector<ReadingDayStat> days;
  std::vector<StatisticsBookSnapshot> books;
  std::vector<StatisticsAchievementSnapshot> achievements;

  static StatisticsSnapshot build(const StatisticsSnapshotOptions& options = {});
  StatisticsBookSnapshot* findBook(const std::string& path);
  const StatisticsBookSnapshot* findBook(const std::string& path) const;
  size_t ensureFallbackBook(const std::string& path);
};
