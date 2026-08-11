#include "ReadingStatsStore.h"

#include <Logging.h>

#include <algorithm>
#include <iterator>

namespace {
// Return a monotonically increasing day number for a Gregorian date. This is
// used only when the statistics screen is opened, so it adds no work to the
// reader/page-turn path.
int64_t dayOrdinal(const uint32_t dateKey) {
  if (dateKey == 0) return -1;
  const int y = static_cast<int>(dateKey / 10000);
  const unsigned m = static_cast<unsigned>((dateKey / 100) % 100);
  const unsigned d = static_cast<unsigned>(dateKey % 100);
  if (y < 1 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  const int adjustedYear = y - (m <= 2 ? 1 : 0);
  const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
  const unsigned monthPrime = m > 2 ? m - 3 : m + 9;
  const unsigned dayOfYear = (153 * monthPrime + 2) / 5 + d - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra);
}
}  // namespace

void ReadingStatsStore::toJson(JsonDocument& doc) const {
  JsonArray array = doc["days"].to<JsonArray>();
  for (const auto& day : days) {
    JsonObject item = array.add<JsonObject>();
    item["date"] = day.dateKey;
    item["seconds"] = day.seconds;
    item["sessions"] = day.sessions;
    item["pages"] = day.pagesTurned;
  }
}

bool ReadingStatsStore::fromJson(JsonVariantConst doc) {
  days.clear();
  JsonArrayConst array = doc["days"].as<JsonArrayConst>();
  days.reserve(std::min(array.size(), MAX_DAYS));
  for (JsonObjectConst item : array) {
    ReadingDayStat day;
    day.dateKey = item["date"] | 0;
    day.seconds = item["seconds"] | 0;
    day.sessions = item["sessions"] | 0;
    day.pagesTurned = item["pages"] | 0;
    if (day.seconds == 0 && day.sessions == 0 && day.pagesTurned == 0) continue;
    days.push_back(day);
    if (days.size() >= MAX_DAYS) break;
  }
  std::sort(days.begin(), days.end(), [](const ReadingDayStat& a, const ReadingDayStat& b) {
    return a.dateKey < b.dateKey;
  });
  return true;
}

void ReadingStatsStore::recordSession(const uint32_t dateKey, const uint32_t elapsedSeconds,
                                      const uint32_t pagesTurned) {
  if (elapsedSeconds == 0 && pagesTurned == 0) return;

  auto it = std::find_if(days.begin(), days.end(),
                         [&](const ReadingDayStat& day) { return day.dateKey == dateKey; });
  if (it == days.end()) {
    if (days.size() >= MAX_DAYS) days.erase(days.begin());
    days.push_back(ReadingDayStat{dateKey, 0, 0, 0});
    it = std::prev(days.end());
  }

  it->seconds += std::min(elapsedSeconds, UINT32_MAX - it->seconds);
  if (it->sessions < UINT16_MAX) ++it->sessions;
  it->pagesTurned += std::min(pagesTurned, UINT32_MAX - it->pagesTurned);
  std::sort(days.begin(), days.end(), [](const ReadingDayStat& a, const ReadingDayStat& b) {
    return a.dateKey < b.dateKey;
  });
  if (!saveToFile()) LOG_ERR("RSTAT", "Failed to persist reading session statistics");
}

uint32_t ReadingStatsStore::totalSeconds() const {
  uint32_t total = 0;
  for (const auto& day : days) total += std::min(day.seconds, UINT32_MAX - total);
  return total;
}

uint32_t ReadingStatsStore::totalSessions() const {
  uint32_t total = 0;
  for (const auto& day : days) total += std::min<uint32_t>(day.sessions, UINT32_MAX - total);
  return total;
}

uint32_t ReadingStatsStore::totalPagesTurned() const {
  uint32_t total = 0;
  for (const auto& day : days) total += std::min(day.pagesTurned, UINT32_MAX - total);
  return total;
}

uint32_t ReadingStatsStore::secondsForDate(const uint32_t dateKey) const {
  const auto it = std::find_if(days.begin(), days.end(),
                               [&](const ReadingDayStat& day) { return day.dateKey == dateKey; });
  return it == days.end() ? 0 : it->seconds;
}

uint16_t ReadingStatsStore::sessionsForDate(const uint32_t dateKey) const {
  const auto it = std::find_if(days.begin(), days.end(),
                               [&](const ReadingDayStat& day) { return day.dateKey == dateKey; });
  return it == days.end() ? 0 : it->sessions;
}

uint32_t ReadingStatsStore::pagesForDate(const uint32_t dateKey) const {
  const auto it = std::find_if(days.begin(), days.end(),
                               [&](const ReadingDayStat& day) { return day.dateKey == dateKey; });
  return it == days.end() ? 0 : it->pagesTurned;
}

uint32_t ReadingStatsStore::currentStreakDays() const {
  int64_t previous = -1;
  uint32_t streak = 0;
  for (auto it = days.rbegin(); it != days.rend(); ++it) {
    const int64_t ordinal = dayOrdinal(it->dateKey);
    if (ordinal < 0) continue;
    if (previous < 0 || previous - ordinal == 1) {
      ++streak;
      previous = ordinal;
    } else {
      break;
    }
  }
  return streak;
}

uint32_t ReadingStatsStore::longestStreakDays() const {
  int64_t previous = -1;
  uint32_t current = 0;
  uint32_t longest = 0;
  for (const auto& day : days) {
    const int64_t ordinal = dayOrdinal(day.dateKey);
    if (ordinal < 0) continue;
    if (previous >= 0 && ordinal - previous == 1) {
      ++current;
    } else {
      current = 1;
    }
    previous = ordinal;
    longest = std::max(longest, current);
  }
  return longest;
}

bool ReadingStatsStore::clearAll() {
  if (days.empty()) return true;
  days.clear();
  if (!saveToFile()) {
    LOG_ERR("RSTAT", "Failed to clear reading statistics");
    return false;
  }
  return true;
}
