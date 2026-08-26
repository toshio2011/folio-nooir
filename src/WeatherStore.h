#pragma once

#include <PersistableStore.h>

#include <cstdint>

// Small persistent cache for the one-shot clock/weather sync.  Keeping this
// separate from CrossPointSettings means opening the settings page or drawing
// the reader never touches the network cache.
class WeatherStore final : public PersistableStore<WeatherStore> {
  WeatherStore() = default;

  friend class PersistableStore<WeatherStore>;

 public:
  // A useful default for the original Nooir target; the Web UI can replace it
  // with any coordinates without requiring a firmware rebuild.
  char location[48] = "Kuala Lumpur";
  char latitude[16] = "3.1390";
  char longitude[16] = "101.6869";
  // IANA zone returned by the geocoding service (for example,
  // Asia/Kuala_Lumpur). The current UTC offset is stored separately because
  // the small e-ink clock only needs a compact numeric conversion at render
  // time.
  char timezone[48] = "Asia/Kuala_Lumpur";
  uint8_t fahrenheit = 0;
  int32_t locationUtcOffsetSeconds = 0;
  uint8_t hasLocationTimezone = 0;

  uint32_t lastClockSyncDateKey = 0;
  int64_t lastClockSyncEpoch = 0;
  uint32_t lastWeatherSyncDateKey = 0;
  int64_t lastWeatherSyncEpoch = 0;
  int16_t temperatureTenths = 0;
  int16_t weatherCode = -1;
  uint8_t hasWeather = 0;

  static const char* getFilePath() { return "/.crosspoint/weather.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool hasLocation() const { return latitude[0] != '\0' && longitude[0] != '\0'; }
  void clearWeatherCache();
  bool weatherIsFreshToday(uint32_t today) const {
    return today != 0 && lastWeatherSyncDateKey == today && hasWeather != 0;
  }
};

#define WEATHER_STORE WeatherStore::getInstance()
