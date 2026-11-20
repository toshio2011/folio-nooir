#pragma once

#include <cstdint>

#include "CrossPointSettings.h"
#include "util/BleMappingProfiles.h"

class BleMappingProfileStore final {
 public:
  static bool list(bleinput::profiles::ProfileRecord* records, uint8_t& count);
  static bool switchTo(uint8_t profileId);
  static bool createEmpty(const char* name);
  static bool duplicate(uint8_t sourceProfileId, const char* name);
  static bool rename(uint8_t profileId, const char* name);
  static bool remove(uint8_t profileId);
  static bool saveActive(bool allowMappingRemoval = false);
  static bool loadActive();
  static uint8_t activeId();
  static bool isFactory(uint8_t profileId);
  static const char* factoryName(uint8_t profileId);
  static bool readFactoryMap(uint8_t profileId, CrossPointSettings::BleKeyMapEntry* map);

 private:
  static constexpr uint8_t CATALOG_SCHEMA_VERSION = 1;
  static constexpr uint8_t MAP_SCHEMA_VERSION = 2;
  static constexpr const char* DIRECTORY = "/.crosspoint/ble-mapping";
  static constexpr const char* CATALOG_PATH = "/.crosspoint/ble-mapping/catalog.json";

  static bool ensureCatalog(bleinput::profiles::ProfileRecord* records, uint8_t& count);
  static bool readCatalog(bleinput::profiles::ProfileRecord* records, uint8_t& count);
  static bool writeCatalog(const bleinput::profiles::ProfileRecord* records, uint8_t count);
  static bool readMap(uint8_t profileId, CrossPointSettings::BleKeyMapEntry* map,
                      uint8_t* controllerPreset = nullptr);
  static bool writeMap(uint8_t profileId, const CrossPointSettings::BleKeyMapEntry* map,
                       uint8_t controllerPreset);
  static bool removeMapFiles(uint8_t profileId);
};
