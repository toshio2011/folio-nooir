#pragma once

#include <cstdint>

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
  static const char* weatherDescription(int16_t weatherCode);

 private:
  static bool fetchWeather();
};

