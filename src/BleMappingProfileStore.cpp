#include "BleMappingProfileStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "BleInput.h"
#include "MappedInputManager.h"

namespace {
using bleinput::profiles::ProfileRecord;
constexpr uint8_t kProfileCapacity = bleinput::profiles::kMaxProfiles;

std::string mapPath(const uint8_t profileId) {
  char filename[24];
  std::snprintf(filename, sizeof(filename), "/map_%03u.json", static_cast<unsigned>(profileId));
  return std::string("/.crosspoint/ble-mapping") + filename;
}

bool hasMapCandidate(const uint8_t profileId) {
  const std::string path = mapPath(profileId);
  const std::string backup = path + ".bak";
  const std::string temporary = path + ".tmp";
  return Storage.exists(path.c_str()) || Storage.exists(backup.c_str()) || Storage.exists(temporary.c_str());
}

void appendMap(JsonDocument& doc, const CrossPointSettings::BleKeyMapEntry* map) {
  JsonArray array = doc["bleKeyMap"].to<JsonArray>();
  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i) {
    const auto& entry = map[i];
    if (!bleinput::mapping::entryIsValid(entry)) continue;
    JsonObject item = array.add<JsonObject>();
    item["k"] = entry.keyKind;
    item["v"] = entry.keyValue;
    if (entry.actionType == bleinput::mapping::kShortcutAction) {
      item["t"] = entry.actionType;
      item["a"] = entry.shortcut;
    } else {
      item["b"] = entry.button;
    }
    if (entry.signatureCount > 1) {
      JsonArray signature = item["s"].to<JsonArray>();
      for (uint8_t j = 0; j < entry.signatureCount; ++j) signature.add(entry.signature[j]);
    }
    if (entry.inputLabel[0]) item["n"] = entry.inputLabel;
  }
}

bool parseMap(JsonVariantConst source, CrossPointSettings::BleKeyMapEntry* output) {
  if (!source.is<JsonArrayConst>() || !output) return false;
  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i)
    output[i] = CrossPointSettings::BleKeyMapEntry{};
  uint8_t slot = 0;
  for (JsonObjectConst item : source.as<JsonArrayConst>()) {
    if (slot >= CrossPointSettings::BLE_MAP_CAPACITY) return false;
    bleinput::mapping::PersistedEntry stored{};
    stored.keyKind = item["k"] | static_cast<uint8_t>(0xFF);
    stored.keyValue = item["v"] | static_cast<uint8_t>(0);
    stored.button = item["b"] | static_cast<uint8_t>(0xFF);
    stored.actionType = item["t"] | static_cast<uint8_t>(0xFF);
    stored.shortcut = item["a"] | static_cast<uint8_t>(0xFF);
    const char* inputLabel = item["n"] | "";
    JsonArrayConst signature = item["s"];
    if (!signature.isNull()) {
      for (JsonVariantConst value : signature) {
        if (stored.signatureCount >= bleinput::mapping::kSignatureMaxEvents) return false;
        stored.signature[stored.signatureCount++] = value | static_cast<uint16_t>(0xFFFF);
      }
    }
    if (!bleinput::mapping::fromPersisted(stored, output[slot]) ||
        !bleinput::mapping::setInputLabel(output[slot], inputLabel)) return false;
    ++slot;
  }
  return true;
}

bool sameIdPresent(const ProfileRecord* records, const uint8_t count, const uint8_t id) {
  return bleinput::profiles::find(records, count, id) >= 0;
}

bool sameMapEntry(const CrossPointSettings::BleKeyMapEntry& left,
                  const CrossPointSettings::BleKeyMapEntry& right) {
  if (left.keyKind != right.keyKind || left.keyValue != right.keyValue ||
      left.button != right.button || left.actionType != right.actionType ||
      left.shortcut != right.shortcut || left.signatureCount != right.signatureCount) return false;
  for (uint8_t i = 0; i < left.signatureCount; ++i) {
    if (left.signature[i] != right.signature[i]) return false;
  }
  return std::strcmp(left.inputLabel, right.inputLabel) == 0;
}

bool sameMap(const CrossPointSettings::BleKeyMapEntry* left,
             const CrossPointSettings::BleKeyMapEntry* right) {
  if (!left || !right) return false;
  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i) {
    if (!sameMapEntry(left[i], right[i])) return false;
  }
  return true;
}
}  // namespace

uint8_t BleMappingProfileStore::activeId() { return SETTINGS.bleMappingProfileId; }

bool BleMappingProfileStore::isFactory(const uint8_t profileId) {
  return bleinput::profiles::isFactoryProfile(profileId);
}

const char* BleMappingProfileStore::factoryName(const uint8_t profileId) {
  switch (profileId) {
    case bleinput::profiles::kFactoryYiserId: return "Yiser (built-in)";
    case bleinput::profiles::kFactoryFree2Id: return "Free2 (built-in)";
    case bleinput::profiles::kFactoryFree3Id: return "Free3 (built-in)";
    default: return "";
  }
}

bool BleMappingProfileStore::readFactoryMap(const uint8_t profileId,
                                             CrossPointSettings::BleKeyMapEntry* map) {
  if (!map || !isFactory(profileId)) return false;
  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i)
    map[i] = CrossPointSettings::BleKeyMapEntry{};
  using Btn = MappedInputManager::Button;
  auto set = [&](uint8_t slot, freeink::SpecialKey key, Btn button) {
    auto& e = map[slot];
    e.keyKind = 0;
    e.keyValue = static_cast<uint8_t>(key);
    e.button = static_cast<uint8_t>(button);
    e.signatureCount = 1;
    e.signature[0] = static_cast<uint16_t>(static_cast<uint8_t>(key));
  };
  if (profileId == bleinput::profiles::kFactoryYiserId) {
    set(0, freeink::SpecialKey::Up, Btn::Up); set(1, freeink::SpecialKey::Down, Btn::Down);
    set(2, freeink::SpecialKey::Left, Btn::Left); set(3, freeink::SpecialKey::Right, Btn::Right);
    set(4, freeink::SpecialKey::Enter, Btn::Confirm); set(5, freeink::SpecialKey::Escape, Btn::Back);
  } else {
    set(0, freeink::SpecialKey::PageDown, Btn::PageForward);
    set(1, freeink::SpecialKey::PageUp, Btn::PageBack);
    if (profileId == bleinput::profiles::kFactoryFree3Id)
      set(2, freeink::SpecialKey::Enter, Btn::Confirm);
  }
  return true;
}

bool BleMappingProfileStore::readCatalog(ProfileRecord* records, uint8_t& count) {
  if (!records) return false;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(CATALOG_PATH, doc) ||
      (doc["schema"] | static_cast<uint8_t>(0)) != CATALOG_SCHEMA_VERSION) return false;
  JsonArrayConst array = doc["profiles"];
  if (!doc["profiles"].is<JsonArrayConst>() || array.size() == 0 || array.size() > kProfileCapacity) return false;
  count = 0;
  for (JsonObjectConst item : array) {
    const uint8_t id = item["id"] | static_cast<uint8_t>(0xFF);
    const char* name = item["name"] | "";
    char safe[bleinput::profiles::kProfileNameBytes] = {};
    if (id == 0xFF || !bleinput::profiles::sanitizeName(name, safe) ||
        bleinput::profiles::find(records, count, id) >= 0 ||
        bleinput::profiles::nameExists(records, count, safe)) return false;
    records[count].id = id;
    std::memcpy(records[count].name, safe, sizeof(safe));
    ++count;
  }
  const int8_t defaultIndex = bleinput::profiles::find(records, count, bleinput::profiles::kDefaultProfileId);
  return defaultIndex >= 0 && std::strcmp(records[defaultIndex].name, "Default") == 0 &&
         (sameIdPresent(records, count, SETTINGS.bleMappingProfileId) || isFactory(SETTINGS.bleMappingProfileId));
}

bool BleMappingProfileStore::writeCatalog(const ProfileRecord* records, const uint8_t count) {
  if (!records || count == 0 || count > kProfileCapacity) return false;
  JsonDocument doc;
  doc["schema"] = CATALOG_SCHEMA_VERSION;
  JsonArray array = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < count; ++i) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = records[i].id;
    item["name"] = records[i].name;
  }
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(DIRECTORY);
  return PersistableStoreBase::writeDocToFile(CATALOG_PATH, doc);
}

bool BleMappingProfileStore::ensureCatalog(ProfileRecord* records, uint8_t& count) {
  if (!records) return false;
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(DIRECTORY);
  if (Storage.exists(CATALOG_PATH) || Storage.exists((std::string(CATALOG_PATH) + ".bak").c_str()) ||
      Storage.exists((std::string(CATALOG_PATH) + ".tmp").c_str())) {
    if (!readCatalog(records, count)) {
      LOG_ERR("BLEPROF", "catalog invalid; left files and active settings untouched");
      return false;
    }
    if (!isFactory(SETTINGS.bleMappingProfileId) && !hasMapCandidate(SETTINGS.bleMappingProfileId) &&
        !writeMap(SETTINGS.bleMappingProfileId, SETTINGS.bleKeyMap, SETTINGS.bleControllerPreset)) return false;
    return true;
  }

  // First-use migration: preserve the already-loaded legacy mapping as Default.
  // Do not allocate or retain any second mapping table beyond this local operation.
  SETTINGS.bleMappingProfileId = bleinput::profiles::kDefaultProfileId;
  records[0] = ProfileRecord{};
  records[0].id = bleinput::profiles::kDefaultProfileId;
  std::strncpy(records[0].name, "Default", sizeof(records[0].name) - 1);
  count = 1;
  if (!writeMap(bleinput::profiles::kDefaultProfileId, SETTINGS.bleKeyMap, SETTINGS.bleControllerPreset) || !writeCatalog(records, count)) {
    LOG_ERR("BLEPROF", "first-use migration failed; existing settings map retained");
    return false;
  }
  return SETTINGS.saveToFile();
}

bool BleMappingProfileStore::list(ProfileRecord* records, uint8_t& count) {
  count = 0;
  return ensureCatalog(records, count);
}

bool BleMappingProfileStore::readMap(const uint8_t profileId, CrossPointSettings::BleKeyMapEntry* map,
                                     uint8_t* controllerPreset) {
  if (!map) return false;
  const std::string path = mapPath(profileId);
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) return false;
  const uint8_t schema = doc["schema"] | static_cast<uint8_t>(0);
  if (schema != 1 && schema != MAP_SCHEMA_VERSION) return false;
  if (controllerPreset) {
    if (schema >= 2) {
      const uint8_t preset = doc["controllerPreset"] | static_cast<uint8_t>(0xFF);
      if (preset > CrossPointSettings::BLE_CONTROLLER_PRESET_YISER_J6_RING) return false;
      *controllerPreset = preset;
    } else {
      *controllerPreset = 0xFF;  // Old profile files inherit the saved global preset.
    }
  }
  return parseMap(doc["bleKeyMap"], map);
}

bool BleMappingProfileStore::writeMap(const uint8_t profileId, const CrossPointSettings::BleKeyMapEntry* map,
                                      const uint8_t controllerPreset) {
  if (!map) return false;
  JsonDocument doc;
  doc["schema"] = MAP_SCHEMA_VERSION;
  doc["controllerPreset"] = controllerPreset;
  appendMap(doc, map);
  const std::string path = mapPath(profileId);
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(DIRECTORY);
  return PersistableStoreBase::writeDocToFile(path.c_str(), doc);
}

bool BleMappingProfileStore::saveActive(const bool allowMappingRemoval) {
  if (isFactory(SETTINGS.bleMappingProfileId)) return SETTINGS.saveToFile();
  ProfileRecord records[kProfileCapacity]{};
  uint8_t count = 0;
  if (!ensureCatalog(records, count)) return false;

  // A stale or partially initialized Settings table must never replace a valid
  // populated profile with an empty/partial snapshot. Only an explicit Remove
  // mapping operation may shrink the persisted identity set.
  if (hasMapCandidate(SETTINGS.bleMappingProfileId)) {
    CrossPointSettings::BleKeyMapEntry persisted[CrossPointSettings::BLE_MAP_CAPACITY]{};
    uint8_t ignoredPreset = SETTINGS.bleControllerPreset;
    if (!readMap(SETTINGS.bleMappingProfileId, persisted, &ignoredPreset)) {
      LOG_ERR("BLEPROF", "active profile read failed; refusing to overwrite saved mappings");
      return false;
    }
    if (!allowMappingRemoval &&
        !bleinput::mapping::preservesExistingMappings(persisted, SETTINGS.bleKeyMap)) {
      LOG_ERR("BLEPROF", "active map lost saved inputs; refusing stale profile overwrite");
      return false;
    }
  }
  return writeMap(SETTINGS.bleMappingProfileId, SETTINGS.bleKeyMap, SETTINGS.bleControllerPreset);
}

bool BleMappingProfileStore::switchTo(const uint8_t profileId) {
  ProfileRecord records[kProfileCapacity]{};
  uint8_t count = 0;
  if (!ensureCatalog(records, count) ||
      (!isFactory(profileId) && bleinput::profiles::find(records, count, profileId) < 0)) return false;

  // Always load the selected profile, including when it is already active.
  // Settings may contain a stale map after restart/profile UI transitions.
  CrossPointSettings::BleKeyMapEntry candidate[CrossPointSettings::BLE_MAP_CAPACITY]{};
  uint8_t candidatePreset = SETTINGS.bleControllerPreset;
  if (isFactory(profileId)) {
    if (!readFactoryMap(profileId, candidate)) return false;
    candidatePreset = profileId == bleinput::profiles::kFactoryYiserId
        ? CrossPointSettings::BLE_CONTROLLER_PRESET_YISER_J6_RING : CrossPointSettings::BLE_CONTROLLER_PRESET_GENERIC;
  } else if (!readMap(profileId, candidate, &candidatePreset)) {
    return false;
  }

  const uint8_t oldId = SETTINGS.bleMappingProfileId;
  const uint8_t oldPreset = SETTINGS.bleControllerPreset;
  if (profileId == oldId && sameMap(candidate, SETTINGS.bleKeyMap) &&
      (candidatePreset == 0xFF || candidatePreset == oldPreset)) {
    BleHid.setInputPreset(static_cast<freeink::InputPreset>(oldPreset));
    return true;  // Already loaded; avoid another settings write on screen entry.
  }
  // Mapping edits are atomically saved as they are assigned. Do not snapshot
  // SETTINGS here: after restart it may be stale, and writing it over the
  // profile file can silently replace valid assignments with an empty map.
  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i) {
    const auto previousEntry = SETTINGS.bleKeyMap[i];
    SETTINGS.bleKeyMap[i] = candidate[i];
    candidate[i] = previousEntry;
  }
  SETTINGS.bleMappingProfileId = profileId;
  if (candidatePreset != 0xFF) SETTINGS.bleControllerPreset = candidatePreset;
  BleHid.setInputPreset(static_cast<freeink::InputPreset>(SETTINGS.bleControllerPreset));
  if (SETTINGS.saveToFile()) return true;

  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i) {
    const auto failedEntry = SETTINGS.bleKeyMap[i];
    SETTINGS.bleKeyMap[i] = candidate[i];
    candidate[i] = failedEntry;
  }
  SETTINGS.bleMappingProfileId = oldId;
  SETTINGS.bleControllerPreset = oldPreset;
  BleHid.setInputPreset(static_cast<freeink::InputPreset>(oldPreset));
  LOG_ERR("BLEPROF", "switch settings commit failed; active map restored");
  return false;
}

bool BleMappingProfileStore::loadActive() {
  return switchTo(SETTINGS.bleMappingProfileId);
}

bool BleMappingProfileStore::createEmpty(const char* name) {
  ProfileRecord records[kProfileCapacity]{};
  uint8_t count = 0;
  if (!ensureCatalog(records, count)) return false;
  char safe[bleinput::profiles::kProfileNameBytes]{};
  if (!bleinput::profiles::sanitizeName(name, safe)) return false;
  const uint8_t id = bleinput::profiles::nextId(records, count, 1);
  if (id == 0xFF) return false;
  CrossPointSettings::BleKeyMapEntry empty[CrossPointSettings::BLE_MAP_CAPACITY]{};
  if (!writeMap(id, empty, SETTINGS.bleControllerPreset)) return false;
  if (bleinput::profiles::add(records, count, id, safe) != bleinput::profiles::CatalogResult::Added ||
      !writeCatalog(records, count)) return false;
  return true;
}

bool BleMappingProfileStore::duplicate(const uint8_t sourceProfileId, const char* name) {
  ProfileRecord records[kProfileCapacity]{};
  uint8_t count = 0;
  if (!ensureCatalog(records, count) ||
      (!isFactory(sourceProfileId) && bleinput::profiles::find(records, count, sourceProfileId) < 0)) return false;
  char safe[bleinput::profiles::kProfileNameBytes]{};
  if (!bleinput::profiles::sanitizeName(name, safe)) return false;
  const uint8_t id = bleinput::profiles::nextId(records, count, 1);
  if (id == 0xFF) return false;
  CrossPointSettings::BleKeyMapEntry copy[CrossPointSettings::BLE_MAP_CAPACITY]{};
  uint8_t preset = SETTINGS.bleControllerPreset;
  if (isFactory(sourceProfileId)) {
    if (!readFactoryMap(sourceProfileId, copy)) return false;
    preset = sourceProfileId == bleinput::profiles::kFactoryYiserId
        ? CrossPointSettings::BLE_CONTROLLER_PRESET_YISER_J6_RING : CrossPointSettings::BLE_CONTROLLER_PRESET_GENERIC;
  } else if (!readMap(sourceProfileId, copy, &preset)) {
    // The profile file is authoritative even for the active profile; the
    // Settings copy may be stale after a restart or a failed earlier load.
    return false;
  }
  if (!writeMap(id, copy, preset)) return false;
  if (bleinput::profiles::add(records, count, id, safe) != bleinput::profiles::CatalogResult::Added ||
      !writeCatalog(records, count)) return false;
  return true;
}

bool BleMappingProfileStore::rename(const uint8_t profileId, const char* name) {
  ProfileRecord records[kProfileCapacity]{};
  uint8_t count = 0;
  if (!ensureCatalog(records, count) ||
      bleinput::profiles::rename(records, count, profileId, name) != bleinput::profiles::CatalogResult::Renamed)
    return false;
  return writeCatalog(records, count);
}

bool BleMappingProfileStore::removeMapFiles(const uint8_t profileId) {
  const std::string path = mapPath(profileId);
  const std::string backup = path + ".bak";
  const std::string temporary = path + ".tmp";
  bool ok = true;
  if (Storage.exists(path.c_str())) ok = Storage.remove(path.c_str()) && ok;
  if (Storage.exists(backup.c_str())) ok = Storage.remove(backup.c_str()) && ok;
  if (Storage.exists(temporary.c_str())) ok = Storage.remove(temporary.c_str()) && ok;
  return ok;
}

bool BleMappingProfileStore::remove(const uint8_t profileId) {
  ProfileRecord records[kProfileCapacity]{};
  uint8_t count = 0;
  if (!ensureCatalog(records, count)) return false;
  const auto result = bleinput::profiles::remove(records, count, profileId, SETTINGS.bleMappingProfileId);
  if (result != bleinput::profiles::CatalogResult::Removed || !writeCatalog(records, count)) return false;
  // Catalog commit is authoritative; a failed cleanup only leaves an unreferenced
  // profile file, never a live mapping or rollback candidate used by the app.
  (void)removeMapFiles(profileId);
  return true;
}
