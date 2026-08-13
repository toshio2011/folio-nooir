#include "ClockWeatherSyncService.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#include "CrossPointSettings.h"
#include "WeatherStore.h"
#include "network/HttpDownloader.h"

namespace {
std::mutex syncMutex;

bool parseCoordinate(const char* text, const double minValue, const double maxValue) {
  if (!text || !*text) return false;
  char* end = nullptr;
  const double value = std::strtod(text, &end);
  if (end == text || *end != '\0' || !std::isfinite(value)) return false;
  return value >= minValue && value <= maxValue;
}

bool appendResponse(std::string& body, const uint8_t* data, const size_t len) {
  // Current conditions should be a few hundred bytes. Refuse unexpectedly
  // large responses so a captive portal or proxy cannot consume the heap.
  constexpr size_t MAX_BODY = 4096;
  if (body.size() + len > MAX_BODY) return false;
  body.append(reinterpret_cast<const char*>(data), len);
  return true;
}

std::string urlEncode(const std::string& text) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(text.size() + 16);
  for (const unsigned char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else if (encoded.size() + 3 <= 512) {
      encoded.push_back('%');
      encoded.push_back(HEX_DIGITS[c >> 4]);
      encoded.push_back(HEX_DIGITS[c & 0x0F]);
    }
  }
  return encoded;
}

uint8_t offsetToQuarterHours(const int32_t offsetSeconds) {
  const int rounded = static_cast<int>((offsetSeconds + (offsetSeconds >= 0 ? 450 : -450)) / 900);
  return static_cast<uint8_t>(std::max(0, std::min(104, rounded + 48)));
}

int32_t settingsOffsetSeconds() {
  uint8_t offset = SETTINGS.clockUtcOffsetQ;
  if (offset > 104) offset = 48;
  return (static_cast<int32_t>(offset) - 48) * 900;
}

uint32_t dateKeyFromIso(const char* iso) {
  if (!iso || std::strlen(iso) < 10 || iso[4] != '-' || iso[7] != '-') return 0;
  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (std::sscanf(iso, "%4u-%2u-%2u", &year, &month, &day) != 3 || year < 2000 || month < 1 || month > 12 ||
      day < 1 || day > 31) {
    return 0;
  }
  return year * 10000UL + month * 100UL + day;
}
}  // namespace

ClockWeatherSyncResult ClockWeatherSyncService::sync(const bool syncClock, const bool syncWeather) {
  std::lock_guard<std::mutex> lock(syncMutex);
  ClockWeatherSyncResult result;

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CWS", "Clock/weather sync requested without Wi-Fi");
    result.failed = true;
    return result;
  }

  // Fetch weather first. Open-Meteo returns the selected location's current
  // UTC offset, which lets the clock/date timestamp be recorded in that same
  // location even when the user has just changed cities.
  if (syncWeather) {
    if (!WEATHER_STORE.hasLocation() ||
        !parseCoordinate(WEATHER_STORE.latitude, -90.0, 90.0) ||
        !parseCoordinate(WEATHER_STORE.longitude, -180.0, 180.0)) {
      LOG_ERR("CWS", "Weather location is not configured or invalid");
      result.weatherSkippedNoLocation = true;
    } else {
      result.weatherSynced = fetchWeather();
    }
  }

  // HalClock transparently uses the hardware RTC when present and the
  // Wi-Fi-synchronised system clock on X4-class boards without one.
  if (syncClock) {
    result.clockSynced = halClock.syncFromNTP();
    if (result.clockSynced) {
      SETTINGS.clockHasBeenSynced = 1;
      const time_t now = time(nullptr);
      WEATHER_STORE.lastClockSyncEpoch = now > 100000 ? static_cast<int64_t>(now) : 0;
      WEATHER_STORE.lastClockSyncDateKey = localDateKey(WEATHER_STORE.lastClockSyncEpoch);
      if (result.weatherSynced && WEATHER_STORE.lastClockSyncEpoch > 100000) {
        // The weather request may have run before NTP completed. Use the
        // freshly synced epoch for both timestamps so a new location cannot
        // inherit the previous boot's date or timezone.
        WEATHER_STORE.lastWeatherSyncEpoch = WEATHER_STORE.lastClockSyncEpoch;
        WEATHER_STORE.lastWeatherSyncDateKey = localDateKey(WEATHER_STORE.lastWeatherSyncEpoch);
      }
    }
  }

  if (result.weatherSynced && WEATHER_STORE.lastWeatherSyncEpoch > 100000) {
    WEATHER_STORE.lastWeatherSyncDateKey = localDateKey(WEATHER_STORE.lastWeatherSyncEpoch);
  }

  if (result.clockSynced || result.weatherSynced) {
    SETTINGS.saveToFile();
    WEATHER_STORE.saveToFile();
  }

  result.failed = !result.clockSynced && !result.weatherSynced && !result.weatherSkippedNoLocation;
  return result;
}

bool ClockWeatherSyncService::resolveLocation(const std::string& query, ClockWeatherLocation& result) {
  if (WiFi.status() != WL_CONNECTED || query.empty()) return false;

  std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=";
  url += urlEncode(query);
  url += "&count=1&language=en&format=json";
  std::string body;
  body.reserve(1024);
  if (!HttpDownloader::fetchUrl(url, [&body](const uint8_t* data, const size_t len) {
        return appendResponse(body, data, len);
      })) {
    LOG_ERR("CWS", "Location lookup failed");
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("CWS", "Location JSON parse failed");
    return false;
  }
  const JsonArrayConst results = doc["results"].as<JsonArrayConst>();
  if (results.isNull() || results.size() == 0) {
    LOG_INF("CWS", "No location match for '%s'", query.c_str());
    return false;
  }
  const JsonObjectConst row = results[0].as<JsonObjectConst>();
  const double latitude = row["latitude"] | 999.0;
  const double longitude = row["longitude"] | 999.0;
  const char* timezone = row["timezone"] | "";
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90.0 || latitude > 90.0 ||
      longitude < -180.0 || longitude > 180.0 || !*timezone) {
    return false;
  }

  result.name = row["name"] | query.c_str();
  const char* admin1 = row["admin1"] | "";
  const char* country = row["country"] | "";
  if (*admin1 && result.name != admin1) result.name += std::string(", ") + admin1;
  if (*country && result.name.find(country) == std::string::npos) result.name += std::string(", ") + country;
  if (result.name.size() > 47) result.name.resize(47);
  char coordinate[24];
  snprintf(coordinate, sizeof(coordinate), "%.6f", latitude);
  result.latitude = coordinate;
  snprintf(coordinate, sizeof(coordinate), "%.6f", longitude);
  result.longitude = coordinate;
  result.timezone = timezone;
  return true;
}

uint8_t ClockWeatherSyncService::locationOffsetQuarterHours() {
  return offsetToQuarterHours(locationOffsetSeconds());
}

int32_t ClockWeatherSyncService::locationOffsetSeconds() {
  return WEATHER_STORE.hasLocationTimezone ? WEATHER_STORE.locationUtcOffsetSeconds : settingsOffsetSeconds();
}

uint32_t ClockWeatherSyncService::localDateKey(const int64_t utcEpoch) {
  const time_t now = utcEpoch > 100000 ? static_cast<time_t>(utcEpoch) : time(nullptr);
  if (now <= 100000) return halClock.getDateKey();
  const time_t local = now + locationOffsetSeconds();
  struct tm timeinfo;
  gmtime_r(&local, &timeinfo);
  return static_cast<uint32_t>(timeinfo.tm_year + 1900) * 10000UL +
         static_cast<uint32_t>(timeinfo.tm_mon + 1) * 100UL + static_cast<uint32_t>(timeinfo.tm_mday);
}

ClockWeatherSyncResult ClockWeatherSyncService::syncDueOnWifiConnection() {
  ClockWeatherSyncResult result;
  if (WiFi.status() != WL_CONNECTED) {
    result.failed = true;
    return result;
  }

  const uint32_t today = localDateKey();
  // X4-class boards do not have the SDK RTC.  Do not keep attempting an
  // impossible clock sync on every Wi-Fi connection; weather can still sync.
  const bool clockDue = SETTINGS.clockSyncEnabled &&
                        (!SETTINGS.clockHasBeenSynced || !halClock.hasUsableTime() || today == 0 ||
                         WEATHER_STORE.lastClockSyncDateKey != today);
  const bool weatherDue = SETTINGS.weatherSyncEnabled &&
                          // X4 has no RTC date. In that case refresh once per
                          // Wi-Fi session (Wi-Fi is never kept on by this
                          // service), which keeps the cached weather useful
                          // without pretending that a day boundary is known.
                          (!WEATHER_STORE.hasWeather || today == 0 ||
                           WEATHER_STORE.lastWeatherSyncDateKey != today);
  if (!clockDue && !weatherDue) return result;

  LOG_INF("CWS", "Auto sync on Wi-Fi connection (clock=%d weather=%d)", clockDue, weatherDue);
  return sync(clockDue, weatherDue);
}

bool ClockWeatherSyncService::fetchWeather() {
  std::string url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += WEATHER_STORE.latitude;
  url += "&longitude=";
  url += WEATHER_STORE.longitude;
  url += "&current=temperature_2m,weather_code&timezone=";
  url += WEATHER_STORE.timezone[0] ? urlEncode(WEATHER_STORE.timezone) : "auto";
  url += "&temperature_unit=";
  url += WEATHER_STORE.fahrenheit ? "fahrenheit" : "celsius";

  std::string body;
  body.reserve(1024);
  if (!HttpDownloader::fetchUrl(url, [&body](const uint8_t* data, const size_t len) {
        return appendResponse(body, data, len);
      })) {
    LOG_ERR("CWS", "Weather request failed");
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    LOG_ERR("CWS", "Weather JSON parse failed: %s", error.c_str());
    return false;
  }

  const JsonObjectConst current = doc["current"].as<JsonObjectConst>();
  if (current.isNull() || current["temperature_2m"].isNull()) {
    LOG_ERR("CWS", "Weather response has no current conditions");
    return false;
  }

  const float temperature = current["temperature_2m"].as<float>();
  if (!std::isfinite(temperature)) return false;
  const int roundedTenths = static_cast<int>(std::lround(temperature * 10.0f));
  WEATHER_STORE.temperatureTenths = static_cast<int16_t>(std::max(-999, std::min(999, roundedTenths)));
  WEATHER_STORE.weatherCode = current["weather_code"] | static_cast<int16_t>(-1);
  const int32_t utcOffset = doc["utc_offset_seconds"] | static_cast<int32_t>(0);
  if (utcOffset >= -43200 && utcOffset <= 50400) {
    WEATHER_STORE.locationUtcOffsetSeconds = utcOffset;
    WEATHER_STORE.hasLocationTimezone = 1;
  }
  const char* timezone = doc["timezone"] | "";
  if (*timezone) {
    strncpy(WEATHER_STORE.timezone, timezone, sizeof(WEATHER_STORE.timezone) - 1);
    WEATHER_STORE.timezone[sizeof(WEATHER_STORE.timezone) - 1] = '\0';
  }
  const uint32_t rtcDate = halClock.getDateKey();
  const char* observedAt = current["time"] | "";
  WEATHER_STORE.lastWeatherSyncDateKey = localDateKey();
  if (WEATHER_STORE.lastWeatherSyncDateKey == 0) {
    WEATHER_STORE.lastWeatherSyncDateKey = rtcDate != 0 ? rtcDate : dateKeyFromIso(observedAt);
  }
  // On a board that has not synced its system clock yet, time(nullptr) can
  // still be the Unix epoch. Never persist that as a real weather timestamp;
  // the API date from Open-Meteo remains the reliable fallback.
  const time_t now = time(nullptr);
  WEATHER_STORE.lastWeatherSyncEpoch = now > 100000 ? static_cast<int64_t>(now) : 0;
  WEATHER_STORE.hasWeather = 1;
  LOG_INF("CWS", "Weather cached: %d.%d %s, code %d", WEATHER_STORE.temperatureTenths / 10,
          std::abs(WEATHER_STORE.temperatureTenths % 10), WEATHER_STORE.fahrenheit ? "F" : "C",
          WEATHER_STORE.weatherCode);
  return true;
}

const char* ClockWeatherSyncService::weatherDescription(const int16_t code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code >= 85 && code <= 86) return "Snow showers";
  if (code >= 95 && code <= 99) return "Thunderstorm";
  return "Unknown";
}
