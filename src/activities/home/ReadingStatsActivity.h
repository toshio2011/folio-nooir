#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "StatisticsSnapshot.h"
#include "activities/Activity.h"

class ReadingStatsActivity final : public Activity {
 public:
  enum class Tab : uint8_t { Overview, Calendar, Books, Achievements };

  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath = {},
                       bool calendarMode = false);

  ~ReadingStatsActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Snapshot construction retains per-day and per-book vectors; keep the
  // optional BLE controller out of this memory-sensitive screen.
  bool bluetoothResourceSensitive() const override { return true; }

 private:
  std::string entryBookPath;
  bool calendarEntry = false;
  bool directBookEntry = false;
  bool bookDetail = false;
  Tab activeTab = Tab::Overview;
  StatisticsSnapshot snapshot;
  size_t selectedBook = 0;
  size_t selectedAchievement = 0;
  int calendarYear = 0;
  int calendarMonth = 0;
  int selectedCalendarDay = 1;
  int minCalendarMonth = 0;
  int maxCalendarMonth = 0;
  uint32_t calendarMaxSeconds = 0;
  std::array<ReadingDayStat, 31> calendarDays{};

  int contentTop() const;
  int contentBottom() const;
  int tabBarTop() const;
  void switchTab(int direction);
  void moveSelection(int direction);
  void handleConfirm();
  void handleTap(int x, int y);
  void initializeCalendar();
  void prepareCalendarMonth(bool preserveDay = false);
  void changeMonth(int direction);
  void cycleCalendarDay();
  ReadingDayStat selectedCalendarStat() const;

  void renderTabs() const;
  void renderOverview();
  void renderCalendar();
  void renderBooks();
  void renderBookDetail();
  void renderAchievements();
  void drawSevenDayChart(int x, int y, int width, int height) const;
};
