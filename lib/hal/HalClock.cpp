#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>

#include <TaskWatchdog.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;  // Singleton instance

void HalClock::begin() {
  _available = _sdkRtc.begin();
  _systemTimeSynced = false;
  _hasCachedTime = false;
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
}

bool HalClock::hasUsableTime() const {
  if (_available || _hasCachedTime) return true;
  if (!_systemTimeSynced) return false;
  const time_t now = time(nullptr);
  return now > 100000;
}

bool HalClock::restoreFromEpoch(const int64_t epoch) {
  if (epoch <= 100000) return false;
  const timeval tv{static_cast<time_t>(epoch), 0};
  if (settimeofday(&tv, nullptr) != 0) return false;
  _systemTimeSynced = true;
  _lastPollMs = 0;
  const time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  _cachedHour = static_cast<uint8_t>(timeinfo.tm_hour);
  _cachedMinute = static_cast<uint8_t>(timeinfo.tm_min);
  _hasCachedTime = true;
  LOG_DBG("CLK", "Restored cached system time from %lld", static_cast<long long>(epoch));
  return true;
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) {
    if (!_systemTimeSynced) return false;
    const time_t now = time(nullptr);
    if (now <= 100000) return false;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    hour = static_cast<uint8_t>(timeinfo.tm_hour);
    minute = static_cast<uint8_t>(timeinfo.tm_min);
    return true;
  }

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) {
    if (!_hasCachedTime) return false;
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }
  _cachedHour = dt.hour;
  _cachedMinute = dt.minute;
  _lastPollMs = now;
  _hasCachedTime = true;
  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

uint32_t HalClock::getDateKey() const {
  if (!_available) {
    if (!_systemTimeSynced) return 0;
    const time_t now = time(nullptr);
    if (now <= 100000) return 0;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    return static_cast<uint32_t>(timeinfo.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(timeinfo.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(timeinfo.tm_mday);
  }
  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) {
    if (!_systemTimeSynced) return 0;
    const time_t now = time(nullptr);
    if (now <= 100000) return 0;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    return static_cast<uint32_t>(timeinfo.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(timeinfo.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(timeinfo.tm_mday);
  }
  return static_cast<uint32_t>(dt.year) * 10000UL + static_cast<uint32_t>(dt.month) * 100UL + dt.day;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    // The web server registers the main task with the watchdog. NTP can wait
    // for several seconds, so feed it between polls instead of letting a
    // slow response reboot the device and drop the browser session.
    resetTaskWatchdogIfSubscribed();
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now = time(nullptr);
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);

      Rtc::DateTime dt;
      dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
      dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
      dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
      dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
      dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
      dt.second = static_cast<uint8_t>(timeinfo.tm_sec);
      dt.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
      _systemTimeSynced = true;
      _lastPollMs = 0;
      _cachedHour = dt.hour;
      _cachedMinute = dt.minute;
      _hasCachedTime = true;
      if (_available && !_sdkRtc.set(dt)) {
        LOG_ERR("CLK", "System time synced but RTC write failed");
        return true;
      }
      LOG_INF("CLK", _available ? "RTC/system clock set to %04u-%02u-%02u %02u:%02u:%02u UTC"
                                : "System clock set to %04u-%02u-%02u %02u:%02u:%02u UTC",
              dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
      return true;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
