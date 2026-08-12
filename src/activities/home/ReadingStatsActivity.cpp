#include "ReadingStatsActivity.h"

#include <HalClock.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "BookStateStore.h"
#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string durationText(const uint32_t seconds) {
  const uint32_t minutes = (seconds + 30) / 60;
  if (minutes < 60) return std::to_string(minutes) + " min";
  const uint32_t hours = minutes / 60;
  const uint32_t rest = minutes % 60;
  if (rest == 0) return std::to_string(hours) + " h";
  return std::to_string(hours) + " h " + std::to_string(rest) + " min";
}

std::string paceText(const uint32_t pages, const uint32_t seconds) {
  if (pages == 0 || seconds == 0) return "n/a";
  const uint32_t minutes = std::max<uint32_t>(1, (seconds + 30) / 60);
  return std::to_string(pages / minutes) + " pages/min";
}

std::string dateText(const uint32_t key) {
  if (key == 0) return "Unknown date";
  char buf[16];
  snprintf(buf, sizeof(buf), "%04lu-%02lu-%02lu", static_cast<unsigned long>(key / 10000),
           static_cast<unsigned long>((key / 100) % 100), static_cast<unsigned long>(key % 100));
  return buf;
}

const char* statusText(const BookStatus status) {
  switch (status) {
    case BookStatus::Reading:
      return "Reading";
    case BookStatus::OnHold:
      return "On hold";
    case BookStatus::Finished:
      return "Finished";
    case BookStatus::New:
    default:
      return "New";
  }
}

const RecentBook* findRecent(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto it = std::find_if(books.begin(), books.end(), [&](const RecentBook& book) { return book.path == path; });
  return it == books.end() ? nullptr : &*it;
}
}  // namespace

void ReadingStatsActivity::buildLines() {
  renderer.setUiScaleTextEnabled(true);
  lines.clear();
  firstLine = 0;

  if (!bookPath.empty()) {
    heading = "Book Statistics";
    const RecentBook* recent = findRecent(bookPath);
    const BookState* state = BOOK_STATES.find(bookPath);
    const std::string title = recent && !recent->title.empty() ? recent->title : bookPath;
    lines.push_back(renderer.truncatedText(UI_12_FONT_ID, title.c_str(), renderer.getScreenWidth() - 24));
    if (recent && !recent->author.empty()) lines.push_back(recent->author);

    const uint8_t progress = std::max<uint8_t>(state ? state->progressPercent : 0,
                                                recent ? recent->progressPercent : 0);
    const uint32_t seconds = std::max<uint32_t>(state ? state->readingSeconds : 0,
                                                recent ? recent->readingSeconds : 0);
    const uint16_t sessions = std::max<uint16_t>(state ? state->readingSessions : 0,
                                                 recent ? recent->readingSessions : 0);
    const uint32_t pages = std::max<uint32_t>(state ? state->pagesTurned : 0,
                                               recent ? recent->pagesTurned : 0);
    BookStatus displayStatus = state ? state->status : BookStatus::New;
    if (progress >= 100) {
      displayStatus = BookStatus::Finished;
    } else if (displayStatus == BookStatus::New && progress > 0) {
      displayStatus = BookStatus::Reading;
    }
    char progressLine[48];
    snprintf(progressLine, sizeof(progressLine), "Progress: %u%% - %s", progress,
             progress >= 100 ? "Complete" : (progress > 0 ? "Ongoing" : "New"));
    lines.emplace_back(progressLine);
    lines.emplace_back("Reading time: " + durationText(seconds));
    lines.emplace_back("Sessions: " + std::to_string(sessions));
    lines.emplace_back("Pages turned: " + std::to_string(pages));
    lines.emplace_back("Pace: " + paceText(pages, seconds));
    lines.emplace_back("Status: " + std::string(statusText(displayStatus)));
    if (state) {
      lines.emplace_back("Started: " + dateText(state->startDate));
      if (state->finishDate != 0) lines.emplace_back("Finished: " + dateText(state->finishDate));
      lines.emplace_back("Last read: " + dateText(state->lastOpenedDate));
    } else {
      lines.emplace_back("Dates: not recorded yet");
    }
    lines.emplace_back("");
    lines.emplace_back("Back returns to the bookshelf.");
    return;
  }

  if (calendarMode) {
    heading = "Reading Stats";
    const auto& days = READING_STATS.getDays();
    const size_t start = days.size() > 30 ? days.size() - 30 : 0;
    uint32_t totalSeconds = 0;
    uint32_t totalSessions = 0;
    uint32_t totalPages = 0;
    for (size_t i = start; i < days.size(); ++i) {
      totalSeconds += std::min(days[i].seconds, UINT32_MAX - totalSeconds);
      totalSessions += std::min<uint32_t>(days[i].sessions, UINT32_MAX - totalSessions);
      totalPages += std::min(days[i].pagesTurned, UINT32_MAX - totalPages);
    }
    lines.emplace_back("Last 30 recorded days");
    lines.emplace_back("Reading: " + durationText(totalSeconds) + "   Sessions: " +
                       std::to_string(totalSessions) + "   Pages: " + std::to_string(totalPages));
    lines.emplace_back("");
    if (days.empty()) {
      lines.emplace_back("No completed reading sessions yet.");
    } else {
      for (size_t i = days.size(); i > start; --i) {
        const auto& day = days[i - 1];
        const uint32_t minutes = (day.seconds + 30) / 60;
        const uint8_t barLength = static_cast<uint8_t>(std::min<uint32_t>(20, (minutes + 4) / 5));
        std::string bar(barLength, '#');
        if (bar.empty()) bar = "-";
        lines.emplace_back(dateText(day.dateKey) + "  " + durationText(day.seconds) + "  " +
                           std::to_string(day.sessions) + " session" + (day.sessions == 1 ? "" : "s") +
                           "  " + std::to_string(day.pagesTurned) + " pages  " + bar);
      }
    }
    lines.emplace_back("");
    lines.emplace_back("Sessions are saved when you leave a book.");
    return;
  }

  heading = "Reading Statistics";
  uint32_t bookSeconds = 0;
  uint32_t bookSessions = 0;
  uint32_t bookPages = 0;
  uint16_t reading = 0;
  uint16_t onHold = 0;
  uint16_t finished = 0;
  uint16_t started = 0;
  auto accumulateBook = [&](uint8_t progress, BookStatus status, uint32_t seconds, uint16_t sessions,
                            uint32_t pages) {
    bookSeconds += std::min(seconds, UINT32_MAX - bookSeconds);
    bookSessions += std::min<uint32_t>(sessions, UINT32_MAX - bookSessions);
    bookPages += std::min(pages, UINT32_MAX - bookPages);
    if (progress > 0) ++started;
    if (status == BookStatus::Reading) ++reading;
    if (status == BookStatus::OnHold) ++onHold;
    if (status == BookStatus::Finished || progress >= 100) ++finished;
  };
  for (const auto& book : BOOK_STATES.getBooks()) {
    const RecentBook* recent = findRecent(book.path);
    const uint8_t progress = std::max<uint8_t>(book.progressPercent, recent ? recent->progressPercent : 0);
    const uint32_t seconds = std::max<uint32_t>(book.readingSeconds, recent ? recent->readingSeconds : 0);
    const uint16_t sessions = std::max<uint16_t>(book.readingSessions, recent ? recent->readingSessions : 0);
    const uint32_t pages = std::max<uint32_t>(book.pagesTurned, recent ? recent->pagesTurned : 0);
    BookStatus status = book.status;
    if (progress >= 100) status = BookStatus::Finished;
    else if (status == BookStatus::New && progress > 0) status = BookStatus::Reading;
    accumulateBook(progress, status, seconds, sessions, pages);
  }
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (BOOK_STATES.find(recent.path)) continue;
    const BookStatus status = recent.progressPercent >= 100
                                  ? BookStatus::Finished
                                  : (recent.progressPercent > 0 ? BookStatus::Reading : BookStatus::New);
    accumulateBook(recent.progressPercent, status, recent.readingSeconds, recent.readingSessions,
                   recent.pagesTurned);
  }

  const uint32_t trackedSeconds = std::max(bookSeconds, READING_STATS.totalSeconds());
  const uint32_t trackedSessions = std::max(bookSessions, READING_STATS.totalSessions());
  const uint32_t trackedPages = std::max(bookPages, READING_STATS.totalPagesTurned());
  lines.emplace_back("Total reading: " + durationText(trackedSeconds));
  lines.emplace_back("Sessions: " + std::to_string(trackedSessions));
  lines.emplace_back("Pages turned: " + std::to_string(trackedPages));
  lines.emplace_back("Pace: " + paceText(trackedPages, trackedSeconds));
  lines.emplace_back("Average session: " +
                     durationText(trackedSessions == 0 ? 0 : trackedSeconds / trackedSessions));
  lines.emplace_back("Streak: " + std::to_string(READING_STATS.currentStreakDays()) + " days   Best: " +
                     std::to_string(READING_STATS.longestStreakDays()) + " days");
  lines.emplace_back("Started: " + std::to_string(started) + "   Finished: " + std::to_string(finished));
  lines.emplace_back("Reading: " + std::to_string(reading) + "   On hold: " + std::to_string(onHold));

  const uint32_t today = halClock.getDateKey();
  if (today != 0) {
    lines.emplace_back("Today: " + durationText(READING_STATS.secondsForDate(today)) + "   " +
                       std::to_string(READING_STATS.pagesForDate(today)) + " pages");
  } else {
    lines.emplace_back("Today: unavailable (clock not synced)");
  }

  lines.emplace_back("");
  lines.emplace_back("Recent days:");
  const auto& days = READING_STATS.getDays();
  const size_t start = days.size() > 7 ? days.size() - 7 : 0;
  for (size_t i = days.size(); i > start; --i) {
    const auto& day = days[i - 1];
    lines.emplace_back(dateText(day.dateKey) + "  " + durationText(day.seconds) + "  " +
                       std::to_string(day.sessions) + " session" + (day.sessions == 1 ? "" : "s") + "  " +
                       std::to_string(day.pagesTurned) + " pages");
  }
  if (days.empty()) lines.emplace_back("No completed reading sessions yet.");
}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  buildLines();
  requestUpdate();
}

void ReadingStatsActivity::movePage(const int direction) {
  renderer.setUiScaleTextEnabled(true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const size_t pageLines = static_cast<size_t>(std::max(1, (bottom - top) / renderer.getLineHeight(UI_10_FONT_ID)));
  const size_t maxStart = lines.size() > pageLines ? lines.size() - pageLines : 0;
  if (direction > 0) firstLine = std::min(maxStart, firstLine + pageLines);
  else firstLine = firstLine > pageLines ? firstLine - pageLines : 0;
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    movePage(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    movePage(-1);
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) movePage(1);
  if (swipe == MappedInputManager::SwipeDir::Down) movePage(-1);
}

void ReadingStatsActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, heading.c_str());

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const size_t pageLines = static_cast<size_t>(std::max(1, (bottom - top) / lineHeight));
  for (size_t row = 0; row < pageLines && firstLine + row < lines.size(); ++row) {
    const std::string line = renderer.truncatedText(UI_10_FONT_ID, lines[firstLine + row].c_str(), width);
    const EpdFontFamily::Style style = row == 0 && !bookPath.empty() ? EpdFontFamily::BOLD
                                                                       : EpdFontFamily::REGULAR;
    renderer.drawText(UI_10_FONT_ID, side, top + static_cast<int>(row) * lineHeight, line.c_str(), true, style);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
