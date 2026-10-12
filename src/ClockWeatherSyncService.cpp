#include "ClockWeatherSyncService.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <atomic>
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
// Low bit records connection state; upper bits are a monotonically advancing
// station-session key. This lets GOT_IP and main-task sync requests share one
// attempt even if they arrive close together.
std::atomic<uint32_t> wifiSessionState{0};
std::atomic<bool> wifiConnectPending{false};
uint32_t ntpAttemptedSession = UINT32_MAX;
bool ntpLastResult = false;

void logSyncMemory(const char* stage) {
  LOG_INF("NETMEM", "stage=%s free=%u largest=%u", stage, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

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

bool parseDigits(const char* text, const size_t count, unsigned& value) {
  value = 0;
  for (size_t i = 0; i < count; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    value = value * 10 + static_cast<unsigned>(text[i] - '0');
  }
  return true;
}

int64_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const int adjustedMonth = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned dayOfYear = static_cast<unsigned>((153 * adjustedMonth + 2) / 5) + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

bool parseWeatherUtcEpoch(const char* iso, const int32_t utcOffsetSeconds, int64_t& epoch) {
  if (!iso || (utcOffsetSeconds < -43200 || utcOffsetSeconds > 50400)) return false;
  const size_t len = strnlen(iso, 24);
  if ((len != 16 && len != 19) || iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':' ||
      (len == 19 && (iso[16] != ':' || iso[19] != '\0'))) {
    return false;
  }
  unsigned year, month, day, hour, minute, second = 0;
  if (!parseDigits(iso, 4, year) || !parseDigits(iso + 5, 2, month) || !parseDigits(iso + 8, 2, day) ||
      !parseDigits(iso + 11, 2, hour) || !parseDigits(iso + 14, 2, minute) ||
      (len == 19 && !parseDigits(iso + 17, 2, second))) {
    return false;
  }
  if (year < 2000 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
  static constexpr unsigned monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned maxDay = monthDays[month - 1];
  const bool leapYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leapYear) ++maxDay;
  if (day < 1 || day > maxDay) return false;

  epoch = daysFromCivil(static_cast<int>(year), month, day) * 86400 + hour * 3600 + minute * 60 + second -
          utcOffsetSeconds;
  return epoch > 100000;
}

uint32_t currentWifiSession() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  uint32_t state = wifiSessionState.load(std::memory_order_acquire);
  while ((state & 1U) == 0) {
    const uint32_t connectedState = ((state & ~1U) + 2U) | 1U;
    if (wifiSessionState.compare_exchange_weak(state, connectedState, std::memory_order_acq_rel)) {
      state = connectedState;
      break;
    }
  }
  return state & ~1U;
}

bool syncNtpOnceForWifiSession() {
  const uint32_t session = currentWifiSession();
  if (session == 0) return false;
  if (ntpAttemptedSession == session) return ntpLastResult;
  // Latch before starting SNTP so concurrent lifecycle entry points cannot
  // issue a second request during this connection, including after a timeout.
  ntpAttemptedSession = session;
  ntpLastResult = halClock.syncFromNTP();
  return ntpLastResult;
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
  logSyncMemory("sync_begin");

  // Fetch weather first. Open-Meteo returns the selected location's current
  // UTC offset, which lets the clock/date timestamp be recorded in that same
  // location even when the user has just changed cities.
  bool weatherUpdatedClock = false;
  if (syncWeather) {
    if (!WEATHER_STORE.hasLocation() ||
        !parseCoordinate(WEATHER_STORE.latitude, -90.0, 90.0) ||
        !parseCoordinate(WEATHER_STORE.longitude, -180.0, 180.0)) {
      LOG_ERR("CWS", "Weather location is not configured or invalid");
      result.weatherSkippedNoLocation = true;
    } else {
      result.weatherSynced = fetchWeather(weatherUpdatedClock);
    }
    logSyncMemory("weather_done");
  }
  result.clockSynced = weatherUpdatedClock;

  // HalClock transparently uses the hardware RTC when present and the
  // Wi-Fi-synchronised system clock on X4-class boards without one.
  if (syncClock) {
    result.clockSynced = syncNtpOnceForWifiSession() || result.clockSynced;
    logSyncMemory("clock_done");
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
    logSyncMemory("persist_done");
  }

  result.failed = !result.clockSynced && !result.weatherSynced && !result.weatherSkippedNoLocation;
  logSyncMemory("sync_done");
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
  // NTP is handled once for every station session by the GOT_IP event path.
  // This legacy due-check now covers only optional daily weather refresh.
  const bool weatherDue = SETTINGS.weatherSyncEnabled &&
                          // X4 has no RTC date. In that case refresh once per
                          // Wi-Fi session (Wi-Fi is never kept on by this
                          // service), which keeps the cached weather useful
                          // without pretending that a day boundary is known.
                          (!WEATHER_STORE.hasWeather || today == 0 ||
                           WEATHER_STORE.lastWeatherSyncDateKey != today);
  if (!weatherDue) return result;

  LOG_INF("CWS", "Auto weather sync on Wi-Fi connection");
  return sync(false, true);
}

void ClockWeatherSyncService::notifyWifiConnectedEvent() {
  uint32_t state = wifiSessionState.load(std::memory_order_acquire);
  while ((state & 1U) == 0) {
    const uint32_t connectedState = ((state & ~1U) + 2U) | 1U;
    if (wifiSessionState.compare_exchange_weak(state, connectedState, std::memory_order_acq_rel)) break;
  }
  wifiConnectPending.store(true, std::memory_order_release);
}

void ClockWeatherSyncService::notifyWifiDisconnectedEvent() {
  wifiSessionState.fetch_and(~1U, std::memory_order_acq_rel);
  wifiConnectPending.store(false, std::memory_order_release);
}

void ClockWeatherSyncService::processPendingWifiConnection() {
  if (!wifiConnectPending.exchange(false, std::memory_order_acq_rel) || WiFi.status() != WL_CONNECTED) return;
  LOG_INF("CLK", "Wi-Fi session ready; starting one-shot NTP sync");
  const ClockWeatherSyncResult result = sync(true, false);
  if (!result.clockSynced) LOG_ERR("CLK", "Wi-Fi one-shot NTP unavailable; keeping last valid clock");
}

bool ClockWeatherSyncService::fetchWeather(bool& clockUpdated) {
  clockUpdated = false;
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
  const int32_t utcOffset = doc["utc_offset_seconds"] | static_cast<int32_t>(INT32_MIN);
  if (utcOffset >= -43200 && utcOffset <= 50400) {
    WEATHER_STORE.locationUtcOffsetSeconds = utcOffset;
    WEATHER_STORE.hasLocationTimezone = 1;
  }
  const char* timezone = doc["timezone"] | "";
  if (*timezone) {
    strncpy(WEATHER_STORE.timezone, timezone, sizeof(WEATHER_STORE.timezone) - 1);
    WEATHER_STORE.timezone[sizeof(WEATHER_STORE.timezone) - 1] = '\0';
  }
  const char* observedAt = current["time"] | "";
  int64_t observedEpoch = 0;
  if (*timezone && utcOffset >= -43200 && utcOffset <= 50400 &&
      parseWeatherUtcEpoch(observedAt, utcOffset, observedEpoch)) {
    clockUpdated = halClock.setUtcEpoch(observedEpoch);
    if (clockUpdated) {
      SETTINGS.clockHasBeenSynced = 1;
      WEATHER_STORE.lastClockSyncEpoch = observedEpoch;
      WEATHER_STORE.lastClockSyncDateKey = localDateKey(observedEpoch);
      LOG_INF("CLK", "Shared system clock updated from Weather: epoch=%lld offset=%ld zone=%.32s",
              static_cast<long long>(observedEpoch), static_cast<long>(utcOffset), timezone);
    }
  } else {
    LOG_DBG("CLK", "Weather response has no valid local time/timezone; clock unchanged");
  }
  const uint32_t rtcDate = halClock.getDateKey();
  WEATHER_STORE.lastWeatherSyncDateKey = localDateKey(observedEpoch);
  if (WEATHER_STORE.lastWeatherSyncDateKey == 0) {
    WEATHER_STORE.lastWeatherSyncDateKey = rtcDate != 0 ? rtcDate : dateKeyFromIso(observedAt);
  }
  // On a board that has not synced its system clock yet, time(nullptr) can
  // still be the Unix epoch. Never persist that as a real weather timestamp;
  // the API date from Open-Meteo remains the reliable fallback.
  const time_t now = time(nullptr);
  WEATHER_STORE.lastWeatherSyncEpoch = observedEpoch > 100000
                                           ? observedEpoch
                                           : (now > 100000 ? static_cast<int64_t>(now) : 0);
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
