#include "WeatherStore.h"

#include <cstring>

namespace {
void copyField(char* dst, const char* src, size_t capacity) {
  if (!src) src = "";
  strncpy(dst, src, capacity - 1);
  dst[capacity - 1] = '\0';
}
}  // namespace

void WeatherStore::toJson(JsonDocument& doc) const {
  doc["location"] = location;
  doc["latitude"] = latitude;
  doc["longitude"] = longitude;
  doc["fahrenheit"] = fahrenheit != 0;
  doc["locationUtcOffsetSeconds"] = locationUtcOffsetSeconds;
  doc["hasLocationTimezone"] = hasLocationTimezone != 0;
  doc["lastClockSyncDate"] = lastClockSyncDateKey;
  doc["lastClockSyncEpoch"] = lastClockSyncEpoch;
  doc["lastWeatherSyncDate"] = lastWeatherSyncDateKey;
  doc["lastWeatherSyncEpoch"] = lastWeatherSyncEpoch;
  doc["temperatureTenths"] = temperatureTenths;
  doc["weatherCode"] = weatherCode;
  doc["hasWeather"] = hasWeather != 0;
}

bool WeatherStore::fromJson(JsonVariantConst doc) {
  copyField(location, doc["location"] | "Kuala Lumpur", sizeof(location));
  copyField(latitude, doc["latitude"] | "3.1390", sizeof(latitude));
  copyField(longitude, doc["longitude"] | "101.6869", sizeof(longitude));
  fahrenheit = doc["fahrenheit"] | static_cast<uint8_t>(0);
  fahrenheit = fahrenheit ? 1 : 0;
  locationUtcOffsetSeconds = doc["locationUtcOffsetSeconds"] | static_cast<int32_t>(0);
  hasLocationTimezone = (doc["hasLocationTimezone"] | false) ? 1 : 0;
  lastClockSyncDateKey = doc["lastClockSyncDate"] | static_cast<uint32_t>(0);
  lastClockSyncEpoch = doc["lastClockSyncEpoch"].isNull() ? 0 : doc["lastClockSyncEpoch"].as<int64_t>();
  lastWeatherSyncDateKey = doc["lastWeatherSyncDate"] | static_cast<uint32_t>(0);
  lastWeatherSyncEpoch = doc["lastWeatherSyncEpoch"].isNull() ? 0 : doc["lastWeatherSyncEpoch"].as<int64_t>();
  // Older builds could persist the Unix epoch when X4 had no RTC. Treat
  // those sentinel values as unknown so the UI does not report a fake 1970
  // sync; the next successful manual sync will replace them.
  if (lastClockSyncDateKey < 20000101UL) lastClockSyncDateKey = 0;
  if (lastWeatherSyncDateKey < 20000101UL) lastWeatherSyncDateKey = 0;
  if (lastClockSyncEpoch < 100000) lastClockSyncEpoch = 0;
  if (lastWeatherSyncEpoch < 100000) lastWeatherSyncEpoch = 0;
  temperatureTenths = doc["temperatureTenths"] | static_cast<int16_t>(0);
  weatherCode = doc["weatherCode"] | static_cast<int16_t>(-1);
  hasWeather = (doc["hasWeather"] | false) ? 1 : 0;
  return true;
}
