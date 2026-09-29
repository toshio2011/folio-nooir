#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

namespace {

constexpr const char* SETTINGS_PATH = "/.crosspoint/settings.json";

bool isSettingsPath(const char* path) {
  return path && strcmp(path, SETTINGS_PATH) == 0;
}

const char* settingsCandidateName(const size_t index) {
  switch (index) {
    case 0:
      return "main";
    case 1:
      return "bak";
    default:
      return "tmp";
  }
}

}  // namespace

bool PersistableStoreBase::isSemanticallyValidSettingsDocument(JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;

  // Profile documents use the same field names as settings but carry this
  // reserved marker.  Never let a profile be mistaken for the main settings
  // document during recovery.
  if (!doc["_profileSchema"].isNull()) return false;

  // These keys have existed across the RC1 settings history.  Do not require
  // any one version's complete field set: the purpose is only to distinguish
  // a real settings object from {}, a profile object, or unrelated JSON.
  constexpr const char* knownKeys[] = {
      "sleepScreen",       "uiTheme",          "fontFamily",       "fontSize",          "lineSpacing",
      "orientation",       "statusBarClock",  "statusBarBattery", "refreshFrequency", "language",
      "recentBookLayout",  "finishedBookLayout", "uiScalePercent", "quickActionSlot1", "sleepModeLayoutVersion",
  };
  for (const char* key : knownKeys) {
    if (!doc[key].isNull()) return true;
  }
  return false;
}

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");
  String json;
  serializeJson(doc, json);

  // Write the replacement separately first. Storage.writeFile() removes its
  // destination before writing, so using it on the live document could leave
  // settings.json missing or truncated after a reset or SD-card interruption.
  // This path is only used on explicit saves, never while rendering, so the
  // extra two renames do not affect reader or bookshelf speed.
  const String tmpPath = String(path) + ".tmp";
  const String backupPath = String(path) + ".bak";

  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
  if (!Storage.writeFile(tmpPath.c_str(), json)) {
    LOG_ERR("PERSIST", "Failed to write temporary document %s", tmpPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }

  // A successful write call is not enough to prove that the completed
  // temporary document is readable and meaningful.  Read it back only for
  // the main settings file: settings saves are explicit and infrequent, and
  // this closes the exact truncation/garbling window before promotion.
  if (isSettingsPath(path)) {
    const String writtenTemp = Storage.readFile(tmpPath.c_str());
    bool parsed = false;
    bool valid = false;
    if (!writtenTemp.isEmpty()) {
      JsonDocument verified;
      const auto error = deserializeJson(verified, writtenTemp);
      parsed = !error;
      valid = parsed && isSemanticallyValidSettingsDocument(verified.as<JsonVariantConst>());
    }
    LOG_INF("PERSIST", "settings_save_phase=temp_validation read=%d parse=%d validation=%d", !writtenTemp.isEmpty(),
            parsed, valid);
    if (!valid) {
      LOG_ERR("PERSIST", "settings_save_phase=temp_validation_failed");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  const bool hadOriginal = Storage.exists(path);
  if (hadOriginal) {
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
      LOG_ERR("PERSIST", "Failed to clear backup %s", backupPath.c_str());
      Storage.remove(tmpPath.c_str());
      return false;
    }
    if (!Storage.rename(path, backupPath.c_str())) {
      LOG_ERR("PERSIST", "Failed to preserve %s as %s", path, backupPath.c_str());
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), path)) {
    LOG_ERR("PERSIST", "Failed to commit %s", path);
    Storage.remove(tmpPath.c_str());
    // Restore immediately when possible. If power is lost between the two
    // renames, readDocFromFile() can still load the .bak copy at next boot.
    if (hadOriginal && !Storage.exists(path)) {
      if (!Storage.rename(backupPath.c_str(), path)) {
        LOG_ERR("PERSIST", "Failed to restore backup %s", path);
      }
    }
    return false;
  }

  // The new document is complete; the backup is no longer needed for this
  // write. It is recreated on the next save.
  if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  const bool settingsPath = isSettingsPath(path);
  const String backupPath = String(path) + ".bak";
  const String tmpPath = String(path) + ".tmp";
  const String candidates[] = {String(path), backupPath, tmpPath};

  for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
    const String& candidate = candidates[i];
    if (!Storage.exists(candidate.c_str())) {
      if (settingsPath) {
        LOG_INF("PERSIST", "settings_candidate=%s exists=0 read=0 parse=0 validation=0",
                settingsCandidateName(i));
      }
      continue;
    }

    String json = Storage.readFile(candidate.c_str());
    bool parsed = false;
    bool valid = false;
    if (json.isEmpty()) {
      LOG_ERR("PERSIST", "Failed to read %s (empty)", candidate.c_str());
      if (settingsPath) {
        LOG_INF("PERSIST", "settings_candidate=%s exists=1 read=0 parse=0 validation=0", settingsCandidateName(i));
      }
      continue;
    }

    doc.clear();
    auto error = deserializeJson(doc, json);
    if (!error) {
      parsed = true;
      valid = !settingsPath || isSemanticallyValidSettingsDocument(doc.as<JsonVariantConst>());
      if (settingsPath) {
        LOG_INF("PERSIST", "settings_candidate=%s exists=1 read=1 parse=1 validation=%d", settingsCandidateName(i),
                valid);
      }
      if (!valid) {
        LOG_ERR("PERSIST", "Rejected semantically invalid settings candidate %s", candidate.c_str());
        doc.clear();
        continue;
      }
      if (i == 1) {
        LOG_ERR("PERSIST", "Recovered %s from backup %s", path, candidate.c_str());
      } else if (i == 2) {
        LOG_ERR("PERSIST", "Recovered %s from pending write %s", path, candidate.c_str());
      }
      if (settingsPath) LOG_INF("PERSIST", "settings_source=%s", settingsCandidateName(i));
      return true;
    }
    parsed = false;
    if (settingsPath) {
      LOG_INF("PERSIST", "settings_candidate=%s exists=1 read=1 parse=0 validation=0", settingsCandidateName(i));
    }
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", candidate.c_str(), error.c_str());
  }

  // Missing documents are normal on first boot; malformed documents are not.
  if (settingsPath) LOG_ERR("PERSIST", "settings_source=defaults");
  return false;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool ok = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok) {
    // Deobfuscation failed; fall back to legacy plaintext password.
    pass = doc["password"] | "";
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
