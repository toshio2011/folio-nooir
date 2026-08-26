#include "StatisticsSnapshot.h"

#include <HalClock.h>

#include <algorithm>
#include <climits>
#include <iterator>
#include <utility>

#include "RecentBooksStore.h"
#include "util/StatisticsDate.h"

namespace {
uint32_t saturatedAdd(const uint32_t a, const uint32_t b) { return a + std::min(b, UINT32_MAX - a); }

std::string filenameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name.empty() ? path : name;
}

const RecentBook* findRecent(const std::string& path) {
  const auto& recents = RECENT_BOOKS.getBooks();
  const auto it = std::find_if(recents.begin(), recents.end(), [&](const RecentBook& book) { return book.path == path; });
  return it == recents.end() ? nullptr : &*it;
}

BookStatus normalizedStatus(const BookStatus status, const uint8_t progress) {
  if (progress >= 100) return BookStatus::Finished;
  if (status == BookStatus::New && progress > 0) return BookStatus::Reading;
  return status;
}

StatisticsBookSnapshot makeBook(const BookState* state, const RecentBook* recent, const std::string& path) {
  StatisticsBookSnapshot book;
  book.path = path;
  book.title = recent && !recent->title.empty() ? recent->title : filenameFromPath(path);
  book.author = recent ? recent->author : std::string{};
  book.coverTemplatePath = recent ? recent->coverBmpPath : std::string{};
  book.progress = std::max<uint8_t>(state ? state->progressPercent : 0, recent ? recent->progressPercent : 0);
  book.status = normalizedStatus(state ? state->status : BookStatus::New, book.progress);
  book.readingSeconds = std::max<uint32_t>(state ? state->readingSeconds : 0, recent ? recent->readingSeconds : 0);
  book.sessions = std::max<uint16_t>(state ? state->readingSessions : 0, recent ? recent->readingSessions : 0);
  book.pages = std::max<uint32_t>(state ? state->pagesTurned : 0, recent ? recent->pagesTurned : 0);
  if (state) {
    book.startDate = state->startDate;
    book.finishDate = state->finishDate;
    book.lastOpenedDate = state->lastOpenedDate;
  }
  return book;
}

void addAchievement(std::vector<StatisticsAchievementSnapshot>& output, const StatisticsAchievementId id,
                    const StatisticsAchievementUnit unit, const uint32_t current, const uint32_t target) {
  output.push_back({id, unit, current, target, current >= target});
}
}  // namespace

StatisticsSnapshot StatisticsSnapshot::build(const StatisticsSnapshotOptions& options) {
  StatisticsSnapshot result;
  const uint32_t clockDate = halClock.getDateKey();
  result.todayDateKey = StatisticsDate::isValid(clockDate) ? clockDate : 0;

  if (options.keepDailyHistory) result.days.reserve(READING_STATS.getDays().size());
  const int64_t todayOrdinal = StatisticsDate::ordinal(result.todayDateKey);
  for (const auto& day : READING_STATS.getDays()) {
    if (!StatisticsDate::isValid(day.dateKey)) continue;
    if (options.keepDailyHistory) result.days.push_back(day);
    if (result.overview.activeDays < UINT16_MAX) ++result.overview.activeDays;
    if (todayOrdinal >= 0) {
      const int64_t dayOrdinal = StatisticsDate::ordinal(day.dateKey);
      const int64_t index = dayOrdinal - (todayOrdinal - 6);
      if (index >= 0 && index < 7) {
        auto& weekDay = result.overview.week[static_cast<size_t>(index)];
        weekDay.seconds = saturatedAdd(weekDay.seconds, day.seconds);
        weekDay.sessions = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, weekDay.sessions + day.sessions));
        weekDay.pages = saturatedAdd(weekDay.pages, day.pagesTurned);
      }
    }
  }

  result.overview.retainedSeconds = READING_STATS.totalSeconds();
  result.overview.retainedSessions = READING_STATS.totalSessions();
  result.overview.retainedPages = READING_STATS.totalPagesTurned();
  result.overview.currentStreak = READING_STATS.currentStreakDays();
  result.overview.longestStreak = READING_STATS.longestStreakDays();
  if (result.todayDateKey != 0) {
    result.overview.todaySeconds = READING_STATS.secondsForDate(result.todayDateKey);
    result.overview.todaySessions = READING_STATS.sessionsForDate(result.todayDateKey);
    result.overview.todayPages = READING_STATS.pagesForDate(result.todayDateKey);
  }

  if (options.keepAllBooks) result.books.reserve(BOOK_STATES.getBooks().size() + RECENT_BOOKS.getBooks().size());
  uint32_t bookSeconds = 0;
  uint32_t bookSessions = 0;
  uint32_t bookPages = 0;
  const auto consumeBook = [&](StatisticsBookSnapshot&& book) {
    bookSeconds = saturatedAdd(bookSeconds, book.readingSeconds);
    bookSessions = saturatedAdd(bookSessions, book.sessions);
    bookPages = saturatedAdd(bookPages, book.pages);
    if ((book.progress > 0 || book.readingSeconds > 0 || book.sessions > 0 || book.startDate != 0) &&
        result.overview.booksStarted < UINT16_MAX)
      ++result.overview.booksStarted;
    if ((book.progress >= 100 || book.status == BookStatus::Finished) && result.overview.booksFinished < UINT16_MAX)
      ++result.overview.booksFinished;
    if (options.keepAllBooks || (!options.selectedBookPath.empty() && book.path == options.selectedBookPath))
      result.books.push_back(std::move(book));
  };
  for (const auto& state : BOOK_STATES.getBooks()) consumeBook(makeBook(&state, findRecent(state.path), state.path));
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (!BOOK_STATES.find(recent.path)) consumeBook(makeBook(nullptr, &recent, recent.path));
  }

  const auto recentRank = [](const std::string& path) {
    const auto& recents = RECENT_BOOKS.getBooks();
    const auto it = std::find_if(recents.begin(), recents.end(), [&](const RecentBook& book) { return book.path == path; });
    return it == recents.end() ? recents.size() : static_cast<size_t>(std::distance(recents.begin(), it));
  };
  std::stable_sort(result.books.begin(), result.books.end(), [&](const auto& a, const auto& b) {
    if (a.lastOpenedDate != b.lastOpenedDate) return a.lastOpenedDate > b.lastOpenedDate;
    const size_t aRank = recentRank(a.path);
    const size_t bRank = recentRank(b.path);
    if (aRank != bRank) return aRank < bRank;
    return a.title < b.title;
  });

  result.overview.trackedSeconds = std::max(bookSeconds, result.overview.retainedSeconds);
  result.overview.trackedSessions = std::max(bookSessions, result.overview.retainedSessions);
  result.overview.trackedPages = std::max(bookPages, result.overview.retainedPages);
  if (options.evaluateAchievements) {
    result.achievements.reserve(20);
    addAchievement(result.achievements, StatisticsAchievementId::FirstSession, StatisticsAchievementUnit::Count,
                   result.overview.trackedSessions, 1);
    addAchievement(result.achievements, StatisticsAchievementId::FirstStartedBook,
                   StatisticsAchievementUnit::Count, result.overview.booksStarted, 1);
    addAchievement(result.achievements, StatisticsAchievementId::FirstFinishedBook,
                   StatisticsAchievementUnit::Count, result.overview.booksFinished, 1);
    addAchievement(result.achievements, StatisticsAchievementId::Finished5, StatisticsAchievementUnit::Count,
                   result.overview.booksFinished, 5);
    addAchievement(result.achievements, StatisticsAchievementId::Finished10, StatisticsAchievementUnit::Count,
                   result.overview.booksFinished, 10);
    addAchievement(result.achievements, StatisticsAchievementId::Finished25, StatisticsAchievementUnit::Count,
                   result.overview.booksFinished, 25);
    addAchievement(result.achievements, StatisticsAchievementId::ReadingHour1, StatisticsAchievementUnit::Seconds,
                   result.overview.trackedSeconds, 60UL * 60UL);
    addAchievement(result.achievements, StatisticsAchievementId::ReadingHours10,
                   StatisticsAchievementUnit::Seconds, result.overview.trackedSeconds, 10UL * 60UL * 60UL);
    addAchievement(result.achievements, StatisticsAchievementId::ReadingHours50,
                   StatisticsAchievementUnit::Seconds, result.overview.trackedSeconds, 50UL * 60UL * 60UL);
    addAchievement(result.achievements, StatisticsAchievementId::ReadingHours100,
                   StatisticsAchievementUnit::Seconds, result.overview.trackedSeconds, 100UL * 60UL * 60UL);
    addAchievement(result.achievements, StatisticsAchievementId::Pages500, StatisticsAchievementUnit::Count,
                   result.overview.trackedPages, 500);
    addAchievement(result.achievements, StatisticsAchievementId::Pages1000, StatisticsAchievementUnit::Count,
                   result.overview.trackedPages, 1000);
    addAchievement(result.achievements, StatisticsAchievementId::Pages5000, StatisticsAchievementUnit::Count,
                   result.overview.trackedPages, 5000);
    addAchievement(result.achievements, StatisticsAchievementId::Sessions10, StatisticsAchievementUnit::Count,
                   result.overview.trackedSessions, 10);
    addAchievement(result.achievements, StatisticsAchievementId::Sessions50, StatisticsAchievementUnit::Count,
                   result.overview.trackedSessions, 50);
    addAchievement(result.achievements, StatisticsAchievementId::Sessions100, StatisticsAchievementUnit::Count,
                   result.overview.trackedSessions, 100);
    addAchievement(result.achievements, StatisticsAchievementId::Streak3, StatisticsAchievementUnit::Days,
                   result.overview.longestStreak, 3);
    addAchievement(result.achievements, StatisticsAchievementId::Streak7, StatisticsAchievementUnit::Days,
                   result.overview.longestStreak, 7);
    addAchievement(result.achievements, StatisticsAchievementId::Streak30, StatisticsAchievementUnit::Days,
                   result.overview.longestStreak, 30);
    addAchievement(result.achievements, StatisticsAchievementId::ActiveDays30, StatisticsAchievementUnit::Days,
                   result.overview.activeDays, 30);
  }
  return result;
}

StatisticsBookSnapshot* StatisticsSnapshot::findBook(const std::string& path) {
  const auto it = std::find_if(books.begin(), books.end(), [&](const auto& book) { return book.path == path; });
  return it == books.end() ? nullptr : &*it;
}

const StatisticsBookSnapshot* StatisticsSnapshot::findBook(const std::string& path) const {
  const auto it = std::find_if(books.begin(), books.end(), [&](const auto& book) { return book.path == path; });
  return it == books.end() ? nullptr : &*it;
}

size_t StatisticsSnapshot::ensureFallbackBook(const std::string& path) {
  const auto it = std::find_if(books.begin(), books.end(), [&](const auto& book) { return book.path == path; });
  if (it != books.end()) return static_cast<size_t>(std::distance(books.begin(), it));
  books.push_back(makeBook(nullptr, nullptr, path));
  return books.size() - 1;
}
