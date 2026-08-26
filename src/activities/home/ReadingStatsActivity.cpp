#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StatisticsCover.h"
#include "util/StatisticsDate.h"

namespace {
std::string durationText(const uint32_t seconds) {
  const uint32_t minutes = (seconds + 30) / 60;
  if (minutes < 60) return std::to_string(minutes) + " min";
  const uint32_t hours = minutes / 60;
  const uint32_t rest = minutes % 60;
  return rest == 0 ? std::to_string(hours) + " h" : std::to_string(hours) + " h " + std::to_string(rest) + " min";
}

std::string dateText(const uint32_t dateKey) {
  int year = 0;
  int month = 0;
  int day = 0;
  if (!StatisticsDate::split(dateKey, year, month, day)) return "-";
  char text[16];
  snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, day);
  return text;
}

const char* statusText(const BookStatus status) {
  switch (status) {
    case BookStatus::Reading:
      return tr(STR_STATS_STATUS_READING);
    case BookStatus::OnHold:
      return tr(STR_STATS_STATUS_ON_HOLD);
    case BookStatus::Finished:
      return tr(STR_FINISHED);
    case BookStatus::New:
    default:
      return tr(STR_STATS_STATUS_NEW);
  }
}

int monthSerial(const int year, const int month) { return year * 12 + month - 1; }

void splitMonthSerial(const int serial, int& year, int& month) {
  year = serial / 12;
  month = serial % 12 + 1;
}

uint32_t makeDateKey(const int year, const int month, const int day) {
  return static_cast<uint32_t>(year * 10000 + month * 100 + day);
}

StrId achievementTitleId(const StatisticsAchievementId id) {
  switch (id) {
    case StatisticsAchievementId::FirstSession:
      return StrId::STR_ACH_FIRST_SESSION;
    case StatisticsAchievementId::FirstStartedBook:
      return StrId::STR_ACH_FIRST_STEPS;
    case StatisticsAchievementId::FirstFinishedBook:
      return StrId::STR_ACH_FIRST_FINISH;
    case StatisticsAchievementId::Finished5:
      return StrId::STR_ACH_BOOKWORM_I;
    case StatisticsAchievementId::Finished10:
      return StrId::STR_ACH_BOOKWORM_II;
    case StatisticsAchievementId::Finished25:
      return StrId::STR_ACH_BOOKWORM_III;
    case StatisticsAchievementId::ReadingHour1:
      return StrId::STR_ACH_READING_HOUR_I;
    case StatisticsAchievementId::ReadingHours10:
      return StrId::STR_ACH_READING_HOUR_II;
    case StatisticsAchievementId::ReadingHours50:
      return StrId::STR_ACH_READING_HOUR_III;
    case StatisticsAchievementId::ReadingHours100:
      return StrId::STR_ACH_READING_HOUR_IV;
    case StatisticsAchievementId::Pages500:
      return StrId::STR_ACH_PAGE_TURNER_I;
    case StatisticsAchievementId::Pages1000:
      return StrId::STR_ACH_PAGE_TURNER_II;
    case StatisticsAchievementId::Pages5000:
      return StrId::STR_ACH_PAGE_TURNER_III;
    case StatisticsAchievementId::Sessions10:
      return StrId::STR_ACH_SESSION_I;
    case StatisticsAchievementId::Sessions50:
      return StrId::STR_ACH_SESSION_II;
    case StatisticsAchievementId::Sessions100:
      return StrId::STR_ACH_SESSION_III;
    case StatisticsAchievementId::Streak3:
      return StrId::STR_ACH_STREAK_I;
    case StatisticsAchievementId::Streak7:
      return StrId::STR_ACH_STREAK_II;
    case StatisticsAchievementId::Streak30:
      return StrId::STR_ACH_STREAK_III;
    case StatisticsAchievementId::ActiveDays30:
      return StrId::STR_ACH_ACTIVE_DAYS;
  }
  return StrId::STR_ACH_FIRST_SESSION;
}

std::string achievementProgress(const StatisticsAchievementSnapshot& achievement) {
  const uint32_t current = std::min(achievement.current, achievement.target);
  if (achievement.unit == StatisticsAchievementUnit::Seconds) {
    return durationText(current) + " / " + durationText(achievement.target);
  }
  const char* suffix = achievement.unit == StatisticsAchievementUnit::Days ? " days" : "";
  return std::to_string(current) + " / " + std::to_string(achievement.target) + suffix;
}

void drawCenteredValue(const GfxRenderer& renderer, const int fontId, const int x, const int y, const int width,
                       const std::string& text, const EpdFontFamily::Style style = EpdFontFamily::BOLD) {
  const int textWidth = renderer.getTextWidth(fontId, text.c_str(), style);
  renderer.drawText(fontId, x + std::max(0, (width - textWidth) / 2), y, text.c_str(), true, style);
}

size_t wrapSelection(const size_t current, const size_t count, const int direction) {
  if (count == 0) return 0;
  const size_t normalized = current < count ? current : 0;
  if (direction > 0) return (normalized + 1) % count;
  return normalized == 0 ? count - 1 : normalized - 1;
}
}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  renderer.setUiScaleTextEnabled(true);
  snapshot = StatisticsSnapshot::build();
  activeTab = calendarEntry ? Tab::Calendar : Tab::Overview;
  directBookEntry = !entryBookPath.empty();
  if (directBookEntry) {
    activeTab = Tab::Books;
    selectedBook = snapshot.ensureFallbackBook(entryBookPath);
    bookDetail = true;
  }
  initializeCalendar();
  requestUpdate();
}

int ReadingStatsActivity::tabBarTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight;
}

int ReadingStatsActivity::contentTop() const { return tabBarTop() + 38; }

int ReadingStatsActivity::contentBottom() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getScreenHeight() - metrics.buttonHintsHeight - 4;
}

void ReadingStatsActivity::switchTab(const int direction) {
  if (bookDetail) return;
  activeTab = static_cast<Tab>((static_cast<int>(activeTab) + direction + 4) % 4);
  requestUpdate();
}

void ReadingStatsActivity::moveSelection(const int direction) {
  if (bookDetail) {
    if (directBookEntry || snapshot.books.empty()) return;
    selectedBook = wrapSelection(selectedBook, snapshot.books.size(), direction);
    requestUpdate();
    return;
  }
  switch (activeTab) {
    case Tab::Calendar:
      changeMonth(direction);
      break;
    case Tab::Books:
      if (!snapshot.books.empty()) {
        selectedBook = wrapSelection(selectedBook, snapshot.books.size(), direction);
        requestUpdate();
      }
      break;
    case Tab::Achievements:
      if (!snapshot.achievements.empty()) {
        selectedAchievement = wrapSelection(selectedAchievement, snapshot.achievements.size(), direction);
        requestUpdate();
      }
      break;
    case Tab::Overview:
      break;
  }
}

void ReadingStatsActivity::handleConfirm() {
  if (bookDetail) return;
  if (activeTab == Tab::Books && !snapshot.books.empty()) {
    bookDetail = true;
    requestUpdate();
  } else if (activeTab == Tab::Calendar) {
    cycleCalendarDay();
  }
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (bookDetail && !directBookEntry) {
      bookDetail = false;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (!bookDetail && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    switchTab(-1);
    return;
  }
  if (!bookDetail && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    switchTab(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    moveSelection(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    moveSelection(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleConfirm();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (!bookDetail && swipe == MappedInputManager::SwipeDir::Left) {
    switchTab(1);
    return;
  }
  if (!bookDetail && swipe == MappedInputManager::SwipeDir::Right) {
    switchTab(-1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Up) {
    moveSelection(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    moveSelection(-1);
    return;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) handleTap(x, y);
}

void ReadingStatsActivity::handleTap(const int x, const int y) {
  const int pageWidth = renderer.getScreenWidth();
  if (!bookDetail && y >= tabBarTop() && y < contentTop()) {
    activeTab = static_cast<Tab>(std::clamp(x * 4 / std::max(1, pageWidth), 0, 3));
    requestUpdate();
    return;
  }
  if (bookDetail) return;
  if (activeTab == Tab::Calendar && calendarMonth != 0) {
    const int side = UITheme::getInstance().getMetrics().contentSidePadding;
    const int gridTop = contentTop() + 62;
    const int gridWidth = pageWidth - side * 2;
    const int cellWidth = std::max(1, gridWidth / 7);
    const int cellHeight = std::max(1, std::min(330, contentBottom() - gridTop - 100) / 6);
    if (x >= side && x < side + cellWidth * 7 && y >= gridTop && y < gridTop + cellHeight * 6) {
      const int firstWeekday = StatisticsDate::weekdayMondayFirst(makeDateKey(calendarYear, calendarMonth, 1));
      const int slot = (y - gridTop) / cellHeight * 7 + (x - side) / cellWidth;
      const int day = slot - firstWeekday + 1;
      if (day >= 1 && day <= StatisticsDate::daysInMonth(calendarYear, calendarMonth)) {
        selectedCalendarDay = day;
        requestUpdate();
      }
    }
    return;
  }
  if (activeTab == Tab::Books && !snapshot.books.empty()) {
    const int listTop = contentTop() + 236;
    const int rowHeight = 42;
    const int rows = std::max(1, (contentBottom() - listTop) / rowHeight);
    const size_t first = selectedBook / static_cast<size_t>(rows) * static_cast<size_t>(rows);
    if (y >= listTop && y < listTop + rows * rowHeight) {
      const size_t tapped = first + static_cast<size_t>((y - listTop) / rowHeight);
      if (tapped < snapshot.books.size()) {
        if (tapped == selectedBook) bookDetail = true;
        selectedBook = tapped;
        requestUpdate();
      }
    }
    return;
  }
  if (activeTab == Tab::Achievements && !snapshot.achievements.empty()) {
    const int side = UITheme::getInstance().getMetrics().contentSidePadding;
    const int gap = 10;
    const int cardWidth = (pageWidth - side * 2 - gap) / 2;
    const int cardHeight = 100;
    const int rows = std::max(1, (contentBottom() - contentTop()) / cardHeight);
    const size_t perPage = static_cast<size_t>(rows * 2);
    const size_t first = selectedAchievement / perPage * perPage;
    const int col = x < side + cardWidth ? 0 : 1;
    const int row = (y - contentTop()) / cardHeight;
    if (row >= 0 && row < rows) {
      const size_t tapped = first + static_cast<size_t>(row * 2 + col);
      if (tapped < snapshot.achievements.size()) {
        selectedAchievement = tapped;
        requestUpdate();
      }
    }
  }
}

void ReadingStatsActivity::initializeCalendar() {
  int year = 0;
  int month = 0;
  int day = 0;
  const bool hasCurrentDate = StatisticsDate::split(snapshot.todayDateKey, year, month, day);
  int earliest = 0;
  int latest = 0;
  for (const auto& stat : snapshot.days) {
    int statYear = 0;
    int statMonth = 0;
    int statDay = 0;
    if (!StatisticsDate::split(stat.dateKey, statYear, statMonth, statDay)) continue;
    const int serial = monthSerial(statYear, statMonth);
    if (earliest == 0 || serial < earliest) earliest = serial;
    if (serial > latest) latest = serial;
  }
  const int current = hasCurrentDate ? monthSerial(year, month) : 0;
  if (earliest == 0 && current == 0) return;
  minCalendarMonth = earliest != 0 ? earliest : current;
  maxCalendarMonth = std::max(latest, current);
  splitMonthSerial(current != 0 ? current : latest, calendarYear, calendarMonth);
  selectedCalendarDay = hasCurrentDate ? day : 1;
  prepareCalendarMonth(hasCurrentDate);
}

void ReadingStatsActivity::prepareCalendarMonth(const bool preserveDay) {
  if (calendarMonth == 0) return;
  calendarDays.fill(ReadingDayStat{});
  calendarMaxSeconds = 0;
  for (const auto& stat : snapshot.days) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (!StatisticsDate::split(stat.dateKey, year, month, day) || year != calendarYear || month != calendarMonth)
      continue;
    auto& target = calendarDays[static_cast<size_t>(day - 1)];
    target.dateKey = stat.dateKey;
    target.seconds += std::min(stat.seconds, UINT32_MAX - target.seconds);
    target.sessions = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, target.sessions + stat.sessions));
    target.pagesTurned += std::min(stat.pagesTurned, UINT32_MAX - target.pagesTurned);
    calendarMaxSeconds = std::max(calendarMaxSeconds, target.seconds);
  }
  const int monthDays = StatisticsDate::daysInMonth(calendarYear, calendarMonth);
  selectedCalendarDay = std::clamp(selectedCalendarDay, 1, std::max(1, monthDays));
  if (!preserveDay) {
    selectedCalendarDay = 1;
    for (int day = 1; day <= monthDays; ++day) {
      if (calendarDays[static_cast<size_t>(day - 1)].dateKey != 0) {
        selectedCalendarDay = day;
        break;
      }
    }
  }
}

void ReadingStatsActivity::changeMonth(const int direction) {
  if (calendarMonth == 0) return;
  const int current = monthSerial(calendarYear, calendarMonth);
  const int next = std::clamp(current + direction, minCalendarMonth, maxCalendarMonth);
  if (next == current) return;
  splitMonthSerial(next, calendarYear, calendarMonth);
  prepareCalendarMonth(false);
  requestUpdate();
}

void ReadingStatsActivity::cycleCalendarDay() {
  if (calendarMonth == 0) return;
  const int monthDays = StatisticsDate::daysInMonth(calendarYear, calendarMonth);
  for (int offset = 1; offset <= monthDays; ++offset) {
    const int candidate = (selectedCalendarDay - 1 + offset) % monthDays;
    if (calendarDays[static_cast<size_t>(candidate)].dateKey != 0) {
      selectedCalendarDay = candidate + 1;
      requestUpdate();
      return;
    }
  }
}

ReadingDayStat ReadingStatsActivity::selectedCalendarStat() const {
  if (selectedCalendarDay < 1 || selectedCalendarDay > 31) return {};
  return calendarDays[static_cast<size_t>(selectedCalendarDay - 1)];
}

void ReadingStatsActivity::renderTabs() const {
  static constexpr StrId LABELS[] = {StrId::STR_STATS_OVERVIEW, StrId::STR_STATS_CALENDAR, StrId::STR_STATS_BOOKS,
                                     StrId::STR_STATS_ACHIEVEMENTS};
  const int width = renderer.getScreenWidth();
  const int y = tabBarTop() + 8;
  const int tabWidth = width / 4;
  for (int i = 0; i < 4; ++i) {
    const char* label = I18N.get(LABELS[i]);
    const bool selected = static_cast<int>(activeTab) == i;
    const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label, style);
    renderer.drawText(SMALL_FONT_ID, i * tabWidth + (tabWidth - textWidth) / 2, y, label, true, style);
    if (selected) renderer.drawLine(i * tabWidth + 10, y + 20, (i + 1) * tabWidth - 11, y + 20, 2, true);
  }
}

void ReadingStatsActivity::drawSevenDayChart(const int x, const int y, const int width, const int height) const {
  uint32_t maximum = 0;
  for (const auto& day : snapshot.overview.week) maximum = std::max(maximum, day.seconds);
  const int gap = std::max(3, width / 70);
  const int barWidth = std::max(4, (width - gap * 6) / 7);
  const int graphHeight = std::max(20, height - 22);
  const int todayWeekday = StatisticsDate::weekdayMondayFirst(snapshot.todayDateKey);
  static constexpr const char* DAYS[] = {"M", "T", "W", "T", "F", "S", "S"};
  renderer.drawLine(x, y + graphHeight, x + width, y + graphHeight);
  for (int i = 0; i < 7; ++i) {
    const int bx = x + i * (barWidth + gap);
    const int barHeight = maximum == 0
                              ? 0
                              : std::max(2, static_cast<int>(static_cast<uint64_t>(snapshot.overview.week[i].seconds) *
                                                             static_cast<uint64_t>(graphHeight - 4) / maximum));
    if (barHeight > 0) {
      renderer.fillRectDither(bx, y + graphHeight - barHeight, barWidth, barHeight,
                              i == 6 ? Color::Black : Color::DarkGray);
    }
    const int weekday = todayWeekday < 0 ? i : (todayWeekday - 6 + i + 14) % 7;
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, DAYS[weekday]);
    renderer.drawText(SMALL_FONT_ID, bx + (barWidth - labelWidth) / 2, y + graphHeight + 5, DAYS[weekday]);
  }
}

void ReadingStatsActivity::renderOverview() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int top = contentTop();

  renderer.drawText(UI_10_FONT_ID, side, top + 4, tr(STR_TODAY), true, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_18_FONT_ID, side, top + 30, durationText(snapshot.overview.todaySeconds).c_str(), true,
                    EpdFontFamily::BOLD);
  const int smallMetricWidth = width / 4;
  drawCenteredValue(renderer, UI_12_FONT_ID, side + width / 2, top + 30, smallMetricWidth,
                    std::to_string(snapshot.overview.todayPages));
  drawCenteredValue(renderer, UI_12_FONT_ID, side + width * 3 / 4, top + 30, smallMetricWidth,
                    std::to_string(snapshot.overview.todaySessions));
  drawCenteredValue(renderer, SMALL_FONT_ID, side + width / 2, top + 58, smallMetricWidth, tr(STR_PAGES));
  drawCenteredValue(renderer, SMALL_FONT_ID, side + width * 3 / 4, top + 58, smallMetricWidth, tr(STR_SESSIONS));
  renderer.drawLine(side + width / 2, top + 20, side + width / 2, top + 72);
  renderer.drawLine(side, top + 86, side + width, top + 86);

  renderer.drawText(SMALL_FONT_ID, side, top + 101, tr(STR_STATS_LAST_SEVEN_DAYS), true, EpdFontFamily::BOLD);
  uint32_t weekSeconds = 0;
  for (const auto& day : snapshot.overview.week) weekSeconds += std::min(day.seconds, UINT32_MAX - weekSeconds);
  const std::string weekTotal = durationText(weekSeconds);
  renderer.drawText(SMALL_FONT_ID, side + width - renderer.getTextWidth(SMALL_FONT_ID, weekTotal.c_str()), top + 101,
                    weekTotal.c_str());
  drawSevenDayChart(side, top + 126, width, 150);

  const int kpiTop = top + 300;
  const int columnWidth = width / 4;
  const int rowHeight = std::max(82, (contentBottom() - kpiTop - 24) / 2);
  const char* labels[8] = {tr(STR_STATS_STREAK_SHORT), tr(STR_STATS_BEST_SHORT), tr(STR_STATS_STARTED),
                           tr(STR_STATS_FINISHED_DATE), tr(STR_STATS_RETAINED_TIME), tr(STR_PAGES), tr(STR_SESSIONS),
                           tr(STR_STATS_AVERAGE_SHORT)};
  const std::string values[8] = {
      std::to_string(snapshot.overview.currentStreak) + " d", std::to_string(snapshot.overview.longestStreak) + " d",
      std::to_string(snapshot.overview.booksStarted), std::to_string(snapshot.overview.booksFinished),
      durationText(snapshot.overview.retainedSeconds), std::to_string(snapshot.overview.retainedPages),
      std::to_string(snapshot.overview.retainedSessions),
      durationText(snapshot.overview.retainedSessions == 0
                       ? 0
                       : snapshot.overview.retainedSeconds / snapshot.overview.retainedSessions)};
  for (int i = 0; i < 8; ++i) {
    const int row = i / 4;
    const int col = i % 4;
    const int cellX = side + col * columnWidth;
    const int cellY = kpiTop + row * rowHeight;
    if (col > 0) renderer.drawLine(cellX, cellY + 8, cellX, cellY + rowHeight - 8);
    if (row > 0 && col == 0) renderer.drawLine(side, cellY, side + width, cellY);
    drawCenteredValue(renderer, SMALL_FONT_ID, cellX, cellY + 18, columnWidth,
                      renderer.truncatedText(SMALL_FONT_ID, labels[i], columnWidth - 8), EpdFontFamily::BOLD);
    drawCenteredValue(renderer, UI_10_FONT_ID, cellX, cellY + 52, columnWidth,
                      renderer.truncatedText(UI_10_FONT_ID, values[i].c_str(), columnWidth - 8));
  }
  renderer.drawText(SMALL_FONT_ID, side, contentBottom() - 18, tr(STR_STATS_RETENTION_NOTE));
}

void ReadingStatsActivity::renderCalendar() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int top = contentTop();
  if (calendarMonth == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + 80, tr(STR_STATS_CLOCK_UNAVAILABLE));
    renderer.drawCenteredText(SMALL_FONT_ID, top + 120, tr(STR_STATS_NO_DATED_HISTORY));
    return;
  }

  char monthText[16];
  snprintf(monthText, sizeof(monthText), "%04d-%02d", calendarYear, calendarMonth);
  renderer.drawCenteredText(UI_12_FONT_ID, top + 5, monthText, true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, side, top + 8, minCalendarMonth < monthSerial(calendarYear, calendarMonth) ? "^" : "");
  renderer.drawText(SMALL_FONT_ID, side + width - 8, top + 8,
                    monthSerial(calendarYear, calendarMonth) < maxCalendarMonth ? "v" : "");

  static constexpr const char* WEEKDAYS[] = {"M", "T", "W", "T", "F", "S", "S"};
  const int gridTop = top + 62;
  const int cellWidth = std::max(1, width / 7);
  const int cellHeight = std::max(1, std::min(330, contentBottom() - gridTop - 100) / 6);
  for (int col = 0; col < 7; ++col)
    drawCenteredValue(renderer, SMALL_FONT_ID, side + col * cellWidth, top + 38, cellWidth, WEEKDAYS[col]);

  const int firstWeekday = StatisticsDate::weekdayMondayFirst(makeDateKey(calendarYear, calendarMonth, 1));
  const int days = StatisticsDate::daysInMonth(calendarYear, calendarMonth);
  for (int day = 1; day <= days; ++day) {
    const int slot = firstWeekday + day - 1;
    const int row = slot / 7;
    const int col = slot % 7;
    const int x = side + col * cellWidth;
    const int y = gridTop + row * cellHeight;
    const auto& stat = calendarDays[static_cast<size_t>(day - 1)];
    bool whiteText = false;
    if (stat.seconds > 0 && calendarMaxSeconds > 0) {
      const uint32_t intensity = static_cast<uint32_t>(static_cast<uint64_t>(stat.seconds) * 100UL /
                                                       calendarMaxSeconds);
      const Color color = intensity <= 25 ? Color::LightGray : (intensity <= 60 ? Color::DarkGray : Color::Black);
      renderer.fillRectDither(x + 2, y + 2, cellWidth - 4, cellHeight - 4, color);
      whiteText = color == Color::Black;
    }
    const bool today = snapshot.todayDateKey == makeDateKey(calendarYear, calendarMonth, day);
    if (day == selectedCalendarDay) {
      renderer.drawRect(x, y, cellWidth, cellHeight, 2, true);
    } else if (today) {
      renderer.drawRect(x, y, cellWidth, cellHeight, 1, true);
    }
    renderer.drawText(SMALL_FONT_ID, x + 6, y + 6, std::to_string(day).c_str(), !whiteText,
                      day == selectedCalendarDay ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  const int summaryTop = gridTop + cellHeight * 6 + 18;
  const auto selected = selectedCalendarStat();
  renderer.drawText(SMALL_FONT_ID, side, summaryTop,
                    dateText(makeDateKey(calendarYear, calendarMonth, selectedCalendarDay)).c_str(), true,
                    EpdFontFamily::BOLD);
  const int summaryWidth = width / 3;
  const std::string values[] = {durationText(selected.seconds), std::to_string(selected.pagesTurned),
                                std::to_string(selected.sessions)};
  const char* labels[] = {tr(STR_STATS_READING_TIME), tr(STR_PAGES), tr(STR_SESSIONS)};
  for (int i = 0; i < 3; ++i) {
    const int x = side + i * summaryWidth;
    if (i > 0) renderer.drawLine(x, summaryTop + 26, x, summaryTop + 80);
    drawCenteredValue(renderer, UI_10_FONT_ID, x, summaryTop + 32, summaryWidth, values[i]);
    drawCenteredValue(renderer, SMALL_FONT_ID, x, summaryTop + 60, summaryWidth, labels[i]);
  }
}

void ReadingStatsActivity::renderBooks() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int top = contentTop();
  if (snapshot.books.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + 100, tr(STR_STATS_NO_TRACKED_BOOKS));
    return;
  }

  auto& book = snapshot.books[selectedBook];
  const bool hasCover = ensureStatisticsCover(book);
  const int coverWidth = std::min(124, width / 3);
  const Rect coverBounds(side, top + 8, coverWidth, 190);
  const bool coverDrawn = hasCover && drawStatisticsCover(renderer, book.coverPath220, coverBounds);
  if (hasCover && !coverDrawn) book.coverAvailable = false;
  const int detailX = coverDrawn ? side + coverWidth + 16 : side;
  const int detailWidth = side + width - detailX;
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), detailWidth, 2);
  int y = top + 10;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, detailX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  if (!book.author.empty()) {
    renderer.drawText(SMALL_FONT_ID, detailX, y + 4,
                      renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), detailWidth).c_str());
    y += 28;
  }
  renderer.drawText(UI_10_FONT_ID, detailX, y + 8,
                    (std::to_string(book.progress) + "%  ·  " + statusText(book.status)).c_str(), true,
                    EpdFontFamily::BOLD);
  const int barY = y + 40;
  renderer.drawRect(detailX, barY, detailWidth, 12);
  if (book.progress > 0) renderer.fillRect(detailX + 2, barY + 2, (detailWidth - 4) * book.progress / 100, 8);
  renderer.drawText(SMALL_FONT_ID, detailX, barY + 28,
                    (durationText(book.readingSeconds) + "  ·  " + std::to_string(book.sessions) + " " +
                     tr(STR_SESSION_PLURAL))
                        .c_str());
  renderer.drawText(SMALL_FONT_ID, detailX, barY + 52,
                    (std::to_string(book.pages) + " " + tr(STR_PAGES)).c_str());

  const int listTop = top + 236;
  renderer.drawLine(side, listTop - 10, side + width, listTop - 10);
  const int rowHeight = 42;
  const int rows = std::max(1, (contentBottom() - listTop) / rowHeight);
  const size_t first = selectedBook / static_cast<size_t>(rows) * static_cast<size_t>(rows);
  for (int row = 0; row < rows; ++row) {
    const size_t index = first + static_cast<size_t>(row);
    if (index >= snapshot.books.size()) break;
    const auto& item = snapshot.books[index];
    const int rowY = listTop + row * rowHeight;
    if (index == selectedBook) renderer.fillRect(side, rowY + 4, 4, rowHeight - 8);
    const std::string progress = std::to_string(item.progress) + "%";
    const int progressWidth = renderer.getTextWidth(SMALL_FONT_ID, progress.c_str());
    const std::string title = renderer.truncatedText(UI_10_FONT_ID, item.title.c_str(), width - progressWidth - 24);
    renderer.drawText(UI_10_FONT_ID, side + 12, rowY + 8, title.c_str(), true,
                      index == selectedBook ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, side + width - progressWidth, rowY + 10, progress.c_str());
    renderer.drawLine(side + 10, rowY + rowHeight - 1, side + width, rowY + rowHeight - 1);
  }
}

void ReadingStatsActivity::renderBookDetail() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int top = metrics.topPadding + metrics.headerHeight + 12;
  if (snapshot.books.empty()) return;
  auto& book = snapshot.books[selectedBook];
  const bool hasCover = ensureStatisticsCover(book);
  const int coverWidth = std::min(150, width / 3);
  const Rect coverBounds(side, top, coverWidth, 230);
  const bool coverDrawn = hasCover && drawStatisticsCover(renderer, book.coverPath220, coverBounds);
  if (hasCover && !coverDrawn) book.coverAvailable = false;
  const int detailX = coverDrawn ? side + coverWidth + 18 : side;
  const int detailWidth = side + width - detailX;
  const auto lines = renderer.wrappedText(UI_12_FONT_ID, book.title.c_str(), detailWidth, 3);
  int y = top + 4;
  for (const auto& line : lines) {
    renderer.drawText(UI_12_FONT_ID, detailX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  if (!book.author.empty()) {
    renderer.drawText(UI_10_FONT_ID, detailX, y + 6,
                      renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), detailWidth).c_str());
    y += 36;
  }
  renderer.drawText(UI_10_FONT_ID, detailX, y + 12,
                    (std::to_string(book.progress) + "%  ·  " + statusText(book.status)).c_str(), true,
                    EpdFontFamily::BOLD);
  const int barY = y + 46;
  renderer.drawRect(detailX, barY, detailWidth, 14);
  if (book.progress > 0) renderer.fillRect(detailX + 2, barY + 2, (detailWidth - 4) * book.progress / 100, 10);

  const int statsTop = top + 258;
  renderer.drawLine(side, statsTop - 16, side + width, statsTop - 16);
  const int columnWidth = width / 2;
  const char* labels[] = {tr(STR_STATS_READING_TIME), tr(STR_SESSIONS), tr(STR_PAGES), tr(STR_STATS_STATUS),
                          tr(STR_STATS_STARTED), tr(STR_STATS_FINISHED_DATE), tr(STR_STATS_LAST_READ)};
  const std::string values[] = {durationText(book.readingSeconds), std::to_string(book.sessions),
                                std::to_string(book.pages), statusText(book.status), dateText(book.startDate),
                                dateText(book.finishDate), dateText(book.lastOpenedDate)};
  for (int i = 0; i < 7; ++i) {
    const int row = i / 2;
    const int col = i % 2;
    const int x = side + col * columnWidth;
    const int cellY = statsTop + row * 80;
    if (col > 0) renderer.drawLine(x, cellY + 4, x, cellY + 68);
    renderer.drawText(SMALL_FONT_ID, x + 10, cellY + 8, labels[i], true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + 10, cellY + 36,
                      renderer.truncatedText(UI_10_FONT_ID, values[i].c_str(), columnWidth - 20).c_str());
  }
}

void ReadingStatsActivity::renderAchievements() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int gap = 10;
  const int cardWidth = (width - gap) / 2;
  const int cardHeight = 100;
  const int rows = std::max(1, (contentBottom() - contentTop()) / cardHeight);
  const size_t perPage = static_cast<size_t>(rows * 2);
  const size_t first = selectedAchievement / perPage * perPage;
  for (size_t offset = 0; offset < perPage; ++offset) {
    const size_t index = first + offset;
    if (index >= snapshot.achievements.size()) break;
    const auto& achievement = snapshot.achievements[index];
    const int row = static_cast<int>(offset / 2);
    const int col = static_cast<int>(offset % 2);
    const int x = side + col * (cardWidth + gap);
    const int y = contentTop() + row * cardHeight;
    if (index == selectedAchievement) renderer.drawRoundedRect(x, y + 2, cardWidth, cardHeight - 5, 2, 8, true);
    const int badgeX = x + 8;
    const int badgeY = y + 12;
    if (achievement.earned) {
      renderer.fillRoundedRect(badgeX, badgeY, 34, 34, 8, Color::Black);
      renderer.drawLine(badgeX + 9, badgeY + 17, badgeX + 15, badgeY + 24, 2, false);
      renderer.drawLine(badgeX + 15, badgeY + 24, badgeX + 26, badgeY + 11, 2, false);
    } else {
      renderer.drawRoundedRect(badgeX, badgeY, 34, 34, 1, 8, true);
      renderer.drawLine(badgeX + 13, badgeY + 17, badgeX + 21, badgeY + 17, 1, true);
    }
    const int textX = badgeX + 42;
    const int textWidth = cardWidth - 58;
    renderer.drawText(SMALL_FONT_ID, textX, y + 10,
                      renderer.truncatedText(SMALL_FONT_ID, I18N.get(achievementTitleId(achievement.id)), textWidth)
                          .c_str(),
                      true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, textX, y + 34,
                      achievement.earned ? tr(STR_STATS_EARNED) : tr(STR_STATS_LOCKED));
    const std::string progress = achievementProgress(achievement);
    renderer.drawText(SMALL_FONT_ID, x + 10, y + 58,
                      renderer.truncatedText(SMALL_FONT_ID, progress.c_str(), cardWidth - 20).c_str());
    const int barWidth = cardWidth - 20;
    const int barY = y + 82;
    renderer.drawRect(x + 10, barY, barWidth, 8);
    const uint32_t filled = achievement.target == 0 ? 0 : std::min(achievement.current, achievement.target);
    if (filled > 0) renderer.fillRect(x + 12, barY + 2, (barWidth - 4) * filled / achievement.target, 4);
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 bookDetail ? tr(STR_BOOK_STATISTICS) : tr(STR_STATISTICS));
  if (bookDetail) {
    renderBookDetail();
  } else {
    renderTabs();
    switch (activeTab) {
      case Tab::Overview:
        renderOverview();
        break;
      case Tab::Calendar:
        renderCalendar();
        break;
      case Tab::Books:
        renderBooks();
        break;
      case Tab::Achievements:
        renderAchievements();
        break;
    }
  }

  const char* confirm = !bookDetail && activeTab == Tab::Books
                            ? tr(STR_STATS_DETAILS)
                            : (!bookDetail && activeTab == Tab::Calendar ? tr(STR_STATS_NEXT_ACTIVE_DAY) : "");
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, bookDetail ? "" : tr(STR_STATS_PREVIOUS_TAB),
                                             bookDetail ? "" : tr(STR_STATS_NEXT_TAB));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
