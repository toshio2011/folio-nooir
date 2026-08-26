#pragma once

#include <Arduino.h>
#include <Rtc.h>

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable Rtc _sdkRtc;
  mutable uint8_t _cachedHour = 0;
  mutable uint8_t _cachedMinute = 0;
  mutable bool _hasCachedTime = false;
  // X4 boards do not expose the optional RTC. Keep the successful NTP/system
  // clock state so the same clock/date UI still works without that chip.
  mutable bool _systemTimeSynced = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // True if an RTC is present on this device
  bool isAvailable() const { return _available; }

  // True when either the hardware RTC or a successful Wi-Fi/NTP sync can
  // provide a usable clock value.
  bool hasUsableTime() const;

  // Restore the last known UTC epoch after a cold boot on boards without an
  // RTC. This is intentionally only a cached fallback; Sync now can replace
  // it with the current network time.
  bool restoreFromEpoch(int64_t epoch);

  // Get current hour (0-23) and minute (0-59).
  // Returns false until an RTC or a successful Wi-Fi/NTP sync is available.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // Return YYYYMMDD from the hardware RTC for grouping reading sessions.
  // Returns 0 when no RTC/system time has been initialized.
  uint32_t getDateKey() const;

  // Format time into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // utcOffsetQuarterHoursBiased: biased quarter-hour offset (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
  // use12Hour: when true, format as 12-hour clock with AM/PM suffix.
  // Returns false until an RTC or a successful Wi-Fi/NTP sync is available.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;

  // Sync the RTC/system clock from an NTP server. Requires WiFi to be connected.
  // Blocks for up to ~5s while waiting for SNTP response.
  // Returns true if the system time was successfully updated (and the RTC too,
  // when the board provides one).
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();
};
