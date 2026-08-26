#include "SettingsProfileStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cctype>

#include "CrossPointSettings.h"

namespace {
constexpr size_t MAX_PROFILE_NAME = 48;

bool hasJsonExtension(const std::string& name) {
  if (name.size() < 5) return false;
  const size_t dot = name.size() - 5;
  return name[dot] == '.' && std::tolower(static_cast<unsigned char>(name[dot + 1])) == 'j' &&
         std::tolower(static_cast<unsigned char>(name[dot + 2])) == 's' &&
         std::tolower(static_cast<unsigned char>(name[dot + 3])) == 'o' &&
         std::tolower(static_cast<unsigned char>(name[dot + 4])) == 'n';
}
}  // namespace

std::string SettingsProfileStore::sanitizeName(const std::string& name) {
  std::string result;
  result.reserve(std::min(name.size(), MAX_PROFILE_NAME));
  for (const unsigned char c : name) {
    if (result.size() >= MAX_PROFILE_NAME) break;
    if (std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c == '.') {
      result.push_back(static_cast<char>(c));
    } else {
      result.push_back('_');
    }
  }

  while (!result.empty() && result.back() == ' ') result.pop_back();
  size_t first = 0;
  while (first < result.size() && result[first] == ' ') ++first;
  if (first > 0) result.erase(0, first);
  if (result.empty() || result == "." || result == "..") return {};
  return result;
}

std::string SettingsProfileStore::pathForName(const std::string& name) {
  const std::string safe = sanitizeName(name);
  if (safe.empty()) return {};
  return std::string(PROFILE_DIR) + "/" + safe + ".json";
}

std::string SettingsProfileStore::nameFromEntry(const char* entryName) {
  if (!entryName) return {};
  std::string name(entryName);
  const size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name.erase(0, slash + 1);
  if (!hasJsonExtension(name)) return {};
  name.erase(name.size() - 5);
  return sanitizeName(name);
}

std::vector<std::string> SettingsProfileStore::list() {
  std::vector<std::string> profiles;
  Storage.mkdir(PROFILE_DIR);
  HalFile directory = Storage.open(PROFILE_DIR);
  if (!directory) return profiles;

  char entryName[128];
  for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory()) continue;
    if (entry.getName(entryName, sizeof(entryName)) == 0) continue;
    const std::string name = nameFromEntry(entryName);
    if (!name.empty()) profiles.push_back(name);
  }
  directory.close();
  std::sort(profiles.begin(), profiles.end(), [](const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](const unsigned char lhs, const unsigned char rhs) {
                                          return std::tolower(lhs) < std::tolower(rhs);
                                        });
  });
  profiles.erase(std::unique(profiles.begin(), profiles.end()), profiles.end());
  return profiles;
}

bool SettingsProfileStore::saveCurrent(const std::string& name) {
  const std::string safe = sanitizeName(name);
  const std::string path = pathForName(safe);
  if (safe.empty() || path.empty()) return false;

  Storage.mkdir(PROFILE_DIR);
  JsonDocument doc;
  SETTINGS.toJson(doc);
  doc["_profileSchema"] = SCHEMA_VERSION;
  doc["_profileName"] = safe.c_str();
  // This flag describes the current clock state, not a user preference. Do
  // not let applying an old profile make the clock look unsynchronised.
  doc.remove("clockHasBeenSynced");

  const bool ok = PersistableStoreBase::writeDocToFile(path.c_str(), doc);
  if (!ok) LOG_ERR("PROF", "Failed to save profile: %s", safe.c_str());
  return ok;
}

bool SettingsProfileStore::apply(const std::string& name) {
  const std::string path = pathForName(name);
  if (path.empty()) return false;

  JsonDocument profile;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), profile)) {
    LOG_ERR("PROF", "Profile not found: %s", name.c_str());
    return false;
  }
  if ((profile["_profileSchema"] | static_cast<uint8_t>(0)) != SCHEMA_VERSION) {
    LOG_ERR("PROF", "Unsupported profile schema: %s", name.c_str());
    return false;
  }

  JsonDocument previous;
  SETTINGS.toJson(previous);
  const uint8_t currentClockSync = SETTINGS.clockHasBeenSynced;
  if (!SETTINGS.fromJson(profile.as<JsonVariantConst>())) {
    SETTINGS.fromJson(previous.as<JsonVariantConst>());
    return false;
  }
  // Runtime sync state belongs to the current device, not the profile.
  SETTINGS.clockHasBeenSynced = currentClockSync;

  if (!SETTINGS.saveToFile()) {
    LOG_ERR("PROF", "Failed to commit profile: %s", name.c_str());
    SETTINGS.fromJson(previous.as<JsonVariantConst>());
    SETTINGS.clockHasBeenSynced = currentClockSync;
    SETTINGS.saveToFile();
    return false;
  }
  LOG_INF("PROF", "Applied settings profile: %s", name.c_str());
  return true;
}

bool SettingsProfileStore::remove(const std::string& name) {
  const std::string path = pathForName(name);
  if (path.empty()) return false;
  const String backupPath = String(path.c_str()) + ".bak";
  const String tempPath = String(path.c_str()) + ".tmp";
  if (!Storage.exists(path.c_str()) && !Storage.exists(backupPath.c_str()) && !Storage.exists(tempPath.c_str())) {
    return false;
  }
  bool ok = true;
  if (Storage.exists(path.c_str())) ok = Storage.remove(path.c_str()) && ok;
  if (Storage.exists(backupPath.c_str())) ok = Storage.remove(backupPath.c_str()) && ok;
  if (Storage.exists(tempPath.c_str())) ok = Storage.remove(tempPath.c_str()) && ok;
  return ok;
}
