#include "ReadingStatsStore.h"

#include <Logging.h>

#include <algorithm>
#include <iterator>

void ReadingStatsStore::toJson(JsonDocument& doc) const {
  JsonArray array = doc["days"].to<JsonArray>();
  for (const auto& day : days) {
    JsonObject item = array.add<JsonObject>();
    item["date"] = day.dateKey;
    item["seconds"] = day.seconds;
    item["sessions"] = day.sessions;
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
    if (day.seconds == 0 && day.sessions == 0) continue;
    days.push_back(day);
    if (days.size() >= MAX_DAYS) break;
  }
  std::sort(days.begin(), days.end(), [](const ReadingDayStat& a, const ReadingDayStat& b) {
    return a.dateKey < b.dateKey;
  });
  return true;
}

void ReadingStatsStore::recordSession(const uint32_t dateKey, const uint32_t elapsedSeconds) {
  if (elapsedSeconds == 0) return;

  auto it = std::find_if(days.begin(), days.end(),
                         [&](const ReadingDayStat& day) { return day.dateKey == dateKey; });
  if (it == days.end()) {
    if (days.size() >= MAX_DAYS) days.erase(days.begin());
    days.push_back(ReadingDayStat{dateKey, 0, 0});
    it = std::prev(days.end());
  }

  it->seconds += std::min(elapsedSeconds, UINT32_MAX - it->seconds);
  if (it->sessions < UINT16_MAX) ++it->sessions;
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

bool ReadingStatsStore::clearAll() {
  if (days.empty()) return true;
  days.clear();
  if (!saveToFile()) {
    LOG_ERR("RSTAT", "Failed to clear reading statistics");
    return false;
  }
  return true;
}
