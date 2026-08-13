#pragma once

#include <cstdint>
#include <string>

struct ClockWeatherLocation {
  std::string name;
  std::string latitude;
  std::string longitude;
  std::string timezone;
};

struct ClockWeatherSyncResult {
  bool clockSynced = false;
  bool weatherSynced = false;
  bool weatherSkippedNoLocation = false;
  bool failed = false;
};

// Performs a single, bounded clock/weather update while Wi-Fi is already up.
// The caller decides whether Wi-Fi should remain active (web server) or be
// turned off after the activity returns (device settings action).
class ClockWeatherSyncService final {
 public:
  static ClockWeatherSyncResult sync(bool syncClock, bool syncWeather);
  static ClockWeatherSyncResult syncDueOnWifiConnection();
  static bool resolveLocation(const std::string& query, ClockWeatherLocation& result);
  static int32_t locationOffsetSeconds();
  static uint8_t locationOffsetQuarterHours();
  static uint32_t localDateKey(int64_t utcEpoch = 0);
  static const char* weatherDescription(int16_t weatherCode);

 private:
  static bool fetchWeather();
};
