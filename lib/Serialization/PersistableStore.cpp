#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "PersistableStorePolicy.h"

namespace {

struct PersistableSaveProfile {
  uint32_t encodeUs = 0;
  uint32_t probeUs = 0;
  uint32_t openUs = 0;
  uint32_t fileWriteUs = 0;
  uint32_t storageFlushUs = 0;
  uint32_t closeUs = 0;
  uint32_t validationUs = 0;
  uint32_t backupPrepareUs = 0;
  uint32_t primaryRenameUs = 0;
  uint32_t tempRenameUs = 0;
  uint32_t cleanupUs = 0;
  uint32_t totalUs = 0;
};

constexpr const char* SETTINGS_PATH = "/.crosspoint/settings.json";
constexpr const char* SETTINGS_RECOVERY_MARKER = "/.crosspoint/settings.recovery-in-progress";
constexpr const char* RECENT_BOOKS_PATH = "/.crosspoint/recent.json";

bool isSettingsPath(const char* path) {
  return path && strcmp(path, SETTINGS_PATH) == 0;
}

bool isRecentBooksPath(const char* path) {
  return path && strcmp(path, RECENT_BOOKS_PATH) == 0;
}

class JsonDocumentFileWriter {
 public:
  explicit JsonDocumentFileWriter(HalFile& file, PersistableSaveProfile* profile = nullptr)
      : file(file), profile(profile) {}

  size_t write(const uint8_t byte) { return write(&byte, 1); }

  size_t write(const uint8_t* data, size_t length) {
    if (!ok) return 0;
    const size_t requested = length;
    while (length > 0) {
      if (buffered == buffer.size() && !flushBuffer()) return requested - length;
      const size_t chunk = std::min(length, buffer.size() - buffered);
      memcpy(buffer.data() + buffered, data, chunk);
      buffered += chunk;
      data += chunk;
      length -= chunk;
    }
    return requested;
  }

  bool flush() { return flushBuffer(); }
  size_t fileBytesWritten() const { return bytesWritten; }

 private:
  bool flushBuffer() {
    if (!ok || buffered == 0) return ok;
    const uint32_t startedUs = profile ? micros() : 0;
    const size_t written = file.write(buffer.data(), buffered);
    if (profile) profile->fileWriteUs += micros() - startedUs;
    bytesWritten += written;
    if (written != buffered) ok = false;
    buffered = 0;
    return ok;
  }

  HalFile& file;
  PersistableSaveProfile* profile = nullptr;
  std::array<uint8_t, 256> buffer{};
  size_t buffered = 0;
  size_t bytesWritten = 0;
  bool ok = true;
};

class JsonDocumentFileCompareWriter {
 public:
  explicit JsonDocumentFileCompareWriter(HalFile& file) : file(file) {}

  size_t write(const uint8_t byte) { return write(&byte, 1); }

  size_t write(const uint8_t* data, size_t length) {
    if (!ok) return 0;
    const size_t requested = length;
    while (length > 0) {
      if (available == 0 && !readMore()) return requested - length;
      const size_t chunk = std::min(length, available);
      if (memcmp(buffer.data() + offset, data, chunk) != 0) {
        ok = false;
        return requested - length;
      }
      offset += chunk;
      available -= chunk;
      data += chunk;
      length -= chunk;
      compared += chunk;
    }
    return requested;
  }

  bool finish(const size_t expectedBytes) {
    if (!ok || compared != expectedBytes || available != 0) return false;
    uint8_t trailingByte = 0;
    return file.read(&trailingByte, 1) == 0;
  }

 private:
  bool readMore() {
    const int count = file.read(buffer.data(), buffer.size());
    if (count <= 0 || static_cast<size_t>(count) > buffer.size()) {
      ok = false;
      return false;
    }
    offset = 0;
    available = static_cast<size_t>(count);
    return true;
  }

  HalFile& file;
  std::array<uint8_t, 256> buffer{};
  size_t offset = 0;
  size_t available = 0;
  size_t compared = 0;
  bool ok = true;
};

bool writeJsonDocumentToFile(const char* path, const JsonDocument& doc, const size_t expectedBytes,
                             size_t& serializedBytes, size_t& outputBytes, bool& opened,
                             PersistableSaveProfile* profile = nullptr) {
  opened = false;
  HalFile file;
  const uint32_t openStartedUs = profile ? micros() : 0;
  if (!Storage.openFileForWrite("PERSIST", path, file)) return false;
  if (profile) profile->openUs += micros() - openStartedUs;
  opened = true;
  JsonDocumentFileWriter writer(file, profile);
  const uint32_t encodeStartedUs = profile ? micros() : 0;
  serializedBytes = serializeJson(doc, writer);
  if (profile) profile->encodeUs += micros() - encodeStartedUs;
  const bool flushed = writer.flush();
  outputBytes = writer.fileBytesWritten();
  const uint32_t storageFlushStartedUs = profile ? micros() : 0;
  file.flush();
  if (profile) profile->storageFlushUs += micros() - storageFlushStartedUs;
  const uint32_t closeStartedUs = profile ? micros() : 0;
  const bool closed = file.close();
  if (profile) profile->closeUs += micros() - closeStartedUs;
  return flushed && closed && persistable::serializedDocumentIsComplete(
                                  doc.overflowed(), expectedBytes, serializedBytes, outputBytes);
}

bool temporaryFileMatches(const char* path, const JsonDocument& expected, const size_t expectedBytes) {
  HalFile file;
  if (!Storage.openFileForRead("PERSIST", path, file)) return false;
  if (file.fileSize64() != expectedBytes) {
    file.close();
    return false;
  }
  JsonDocumentFileCompareWriter writer(file);
  const size_t serializedBytes = serializeJson(expected, writer);
  const bool matches = serializedBytes == expectedBytes && writer.finish(expectedBytes);
  const bool closed = file.close();
  return matches && closed;
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

void logSettingsCandidateSummary(const std::array<persistable::SettingsCandidateResult, 3>& results,
                                 const char* selected) {
  LOG_INF("PERSIST", "settings_candidates main=%s bak=%s tmp=%s selected=%s",
          persistable::settingsCandidateResultName(results[0]),
          persistable::settingsCandidateResultName(results[1]),
          persistable::settingsCandidateResultName(results[2]), selected);
}

const char* settingsQuarantineSuffix(const size_t index) {
  switch (index) {
    case 0:
      return "main";
    case 1:
      return "bak";
    default:
      return "tmp";
  }
}

bool moveToUniqueSibling(const char* source, const char* suffix, std::string& destination) {
  for (size_t index = 0; index < 1000; ++index) {
    const std::string candidate = std::string(source) + suffix + std::to_string(index);
    if (Storage.exists(candidate.c_str())) continue;
    if (!Storage.rename(source, candidate.c_str())) return false;
    destination = candidate;
    return true;
  }
  return false;
}

enum class SettingsFileState : uint8_t { Missing, Uncertain, Malformed, Incompatible, Valid };
enum class SettingsValidationCacheState : uint8_t { Unknown, Missing, Valid };

SettingsValidationCacheState settingsValidationCacheState = SettingsValidationCacheState::Unknown;

SettingsFileState inspectSettingsFile(const char* path) {
  String json;
  const auto status = Storage.readFileChecked(path, json, 65536);
  if (status == HalStorageReadStatus::NotFound || status == HalStorageReadStatus::ParentMissing) {
    return SettingsFileState::Missing;
  }
  if (status != HalStorageReadStatus::Success) return SettingsFileState::Uncertain;

  JsonDocument verified;
  if (deserializeJson(verified, json)) return SettingsFileState::Malformed;
  return PersistableStoreBase::isSemanticallyValidSettingsDocument(verified.as<JsonVariantConst>())
             ? SettingsFileState::Valid
             : SettingsFileState::Incompatible;
}

const char* settingsFileStateName(const SettingsFileState state) {
  switch (state) {
    case SettingsFileState::Missing:
      return "missing";
    case SettingsFileState::Uncertain:
      return "uncertain";
    case SettingsFileState::Malformed:
      return "malformed";
    case SettingsFileState::Incompatible:
      return "incompatible";
    case SettingsFileState::Valid:
      return "valid";
  }
  return "unknown";
}

}  // namespace

uint64_t PersistableStoreBase::fingerprintDoc(const JsonDocument& doc, size_t* serializedBytes) {
  class JsonFingerprintWriter {
   public:
    size_t write(const uint8_t byte) {
      hash ^= byte;
      hash *= 1099511628211ULL;
      ++length;
      return 1;
    }
    size_t write(const uint8_t* data, const size_t size) {
      for (size_t index = 0; index < size; ++index) write(data[index]);
      return size;
    }
    uint64_t hash = 1469598103934665603ULL;
    size_t length = 0;
  } writer;
  serializeJson(doc, writer);
  if (serializedBytes != nullptr) *serializedBytes = writer.length;
  writer.hash ^= writer.length;
  writer.hash *= 1099511628211ULL;
  return writer.hash;
}

bool PersistableStoreBase::isSemanticallyValidSettingsDocument(JsonVariantConst doc) {
  if (!doc.is<JsonObjectConst>()) return false;

  // Profile documents use the same field names as settings but carry this
  // reserved marker.  Never let a profile be mistaken for the main settings
  // document during recovery.  Test key presence rather than value so even a
  // profile carrying a null marker cannot be accepted as main settings.
  const JsonObjectConst settings = doc.as<JsonObjectConst>();
  if (settings["_profileSchema"].is<JsonVariantConst>()) return false;

  // JSON settings have no global schema number.  Use three independent,
  // long-lived serializer identities instead of a language field (which was
  // added later) or an arbitrary recognized-key count:
  //   * reader: font family + font size;
  //   * sleep/power: sleep mode + the original or renamed sleep timeout;
  //   * display: orientation + the long-lived fading-fix setting.
  // These fields were emitted together by the first JSON serializer and remain
  // present in current settings.  Requiring all three groups rejects empty and
  // tiny fragments without requiring recently introduced settings.
  const bool hasReaderIdentity = settings["fontFamily"].is<uint8_t>() && settings["fontSize"].is<uint8_t>();
  const bool hasSleepIdentity = settings["sleepScreen"].is<uint8_t>() &&
                                (settings["sleepTimeout"].is<uint8_t>() ||
                                 settings["sleepTimeoutMinutes"].is<uint8_t>());
  const bool hasDisplayIdentity = settings["orientation"].is<uint8_t>() && settings["fadingFix"].is<uint8_t>();
  return hasReaderIdentity && hasSleepIdentity && hasDisplayIdentity;
}

static bool writeDocToFileInternal(const char* path, const JsonDocument& doc,
                                   const bool useCachedSettingsValidation,
                                   const bool preserveRecoveredRecentBooks) {
  PersistableSaveProfile profile;
  const uint32_t saveStartedUs = micros();
  const bool settingsPath = isSettingsPath(path);
  const bool recentPath = isRecentBooksPath(path);
  String json;
  const size_t expectedBytes = measureJson(doc);
  size_t writtenBytes = 0;
  size_t outputBytes = 0;
  bool documentComplete = false;
  if (!settingsPath) {
    // Reader state and Recent history are streamed from the JSON document to
    // the staged file. This avoids a second full serialized string on the
    // constrained heap while keeping byte-for-byte readback validation.
    documentComplete = !doc.overflowed() && expectedBytes > 0;
  } else {
    const uint32_t encodeStartedUs = micros();
    writtenBytes = serializeJson(doc, json);
    profile.encodeUs = micros() - encodeStartedUs;
    outputBytes = json.length();
    documentComplete = persistable::serializedDocumentIsComplete(
        doc.overflowed(), expectedBytes, writtenBytes, outputBytes);
  }
  if (!documentComplete) {
    if (recentPath) {
      LOG_ERR("RBS", "save_result=serialization_incomplete expected=%u wrote=%u output=%u overflow=%d",
              static_cast<unsigned>(expectedBytes), static_cast<unsigned>(writtenBytes),
              static_cast<unsigned>(outputBytes), doc.overflowed() ? 1 : 0);
    } else {
      LOG_ERR("PERSIST", "save_result=serialization_incomplete path=%s expected=%u wrote=%u output=%u overflow=%d",
              path, static_cast<unsigned>(expectedBytes), static_cast<unsigned>(writtenBytes),
              static_cast<unsigned>(outputBytes), doc.overflowed() ? 1 : 0);
    }
    return false;
  }
  Storage.mkdir("/.crosspoint");

  // Write the replacement separately first. Storage.writeFile() removes its
  // destination before writing, so using it on the live document could leave
  // settings.json missing or truncated after a reset or SD-card interruption.
  // This path is only used on explicit saves, never while rendering, so the
  // extra two renames do not affect reader or bookshelf speed.
  const String tmpPath = String(path) + ".tmp";
  const String backupPath = String(path) + ".bak";
  const bool recentRecoveryWrite = recentPath && preserveRecoveredRecentBooks;
  const bool fastSettingsWrite = settingsPath && useCachedSettingsValidation &&
                                 settingsValidationCacheState != SettingsValidationCacheState::Unknown;

  SettingsFileState mainState = SettingsFileState::Missing;
  SettingsFileState backupState = SettingsFileState::Missing;
  SettingsFileState tempState = SettingsFileState::Missing;
  if (settingsPath && !fastSettingsWrite) {
    // Inspect all candidates before a save can rotate or replace them. A read
    // error is not proof of corruption, so preserve every file and retry only
    // after storage is healthy.
    mainState = inspectSettingsFile(path);
    backupState = inspectSettingsFile(backupPath.c_str());
    tempState = inspectSettingsFile(tmpPath.c_str());
    if (mainState == SettingsFileState::Uncertain || backupState == SettingsFileState::Uncertain ||
        tempState == SettingsFileState::Uncertain) {
      LOG_ERR("PERSIST", "settings_save=blocked reason=candidate_read_uncertain main=%s bak=%s tmp=%s",
              settingsFileStateName(mainState), settingsFileStateName(backupState),
              settingsFileStateName(tempState));
      return false;
    }

    // A completed temporary candidate may be the only usable copy after an
    // interrupted save. Promote it before reusing .tmp, but preserve any
    // malformed or unrecognized primary under a unique name first.
    if (tempState == SettingsFileState::Valid && mainState != SettingsFileState::Valid) {
      if (mainState == SettingsFileState::Malformed || mainState == SettingsFileState::Incompatible) {
        const char* suffix = mainState == SettingsFileState::Malformed ? ".corrupt-before-save-"
                                                                       : ".unrecognized-before-save-";
        std::string preservedPrimary;
        if (!moveToUniqueSibling(path, suffix, preservedPrimary)) {
          LOG_ERR("PERSIST", "Failed to preserve unusable primary before pending-settings promotion");
          return false;
        }
      }
      if (!Storage.rename(tmpPath.c_str(), path)) {
        LOG_ERR("PERSIST", "Failed to promote pending settings before save");
        return false;
      }
      mainState = SettingsFileState::Valid;
      tempState = SettingsFileState::Missing;
    }

    // An explicit profile apply may replace an unusable settings file. Keep
    // malformed and unknown-but-valid candidates under unique names first.
    if (mainState == SettingsFileState::Malformed || mainState == SettingsFileState::Incompatible) {
      const char* suffix = mainState == SettingsFileState::Malformed ? ".corrupt-before-save-"
                                                                     : ".unrecognized-before-save-";
      std::string preservedPrimary;
      if (!moveToUniqueSibling(path, suffix, preservedPrimary)) {
        LOG_ERR("PERSIST", "Failed to preserve unusable settings primary before save");
        return false;
      }
      mainState = SettingsFileState::Missing;
    }

    if (backupState == SettingsFileState::Malformed || backupState == SettingsFileState::Incompatible) {
      const char* suffix = backupState == SettingsFileState::Malformed ? ".corrupt-before-save-"
                                                                       : ".unrecognized-before-save-";
      std::string preservedBackup;
      if (!moveToUniqueSibling(backupPath.c_str(), suffix, preservedBackup)) {
        LOG_ERR("PERSIST", "Failed to preserve unusable settings backup before save");
        return false;
      }
      backupState = SettingsFileState::Missing;
    }

    if (tempState == SettingsFileState::Malformed || tempState == SettingsFileState::Incompatible) {
      const char* suffix = tempState == SettingsFileState::Malformed ? ".corrupt-before-save-"
                                                                     : ".unrecognized-before-save-";
      std::string preservedTemp;
      if (!moveToUniqueSibling(tmpPath.c_str(), suffix, preservedTemp)) {
        LOG_ERR("PERSIST", "Failed to preserve unusable temporary settings before save");
        return false;
      }
      tempState = SettingsFileState::Missing;
    }
  } else if (fastSettingsWrite) {
    // This state came from an actual successful settings load, or a
    // successful write in this process. In particular, a Valid primary was
    // semantically checked when loaded. The backup and temporary candidates
    // are not reread on ordinary saves; a recovery load or external writer
    // invalidates this cache and returns the next save to the checked path.
    mainState = settingsValidationCacheState == SettingsValidationCacheState::Valid
                    ? SettingsFileState::Valid
                    : SettingsFileState::Missing;
  }

  std::string preservedRecentTemp;
  bool temporaryExists = false;
  if (!fastSettingsWrite) {
    const uint32_t tempProbeStartedUs = micros();
    temporaryExists = Storage.exists(tmpPath.c_str());
    profile.probeUs += micros() - tempProbeStartedUs;
  }
  if (!fastSettingsWrite && temporaryExists) {
    if (recentPath) {
      // A leftover Recent temp may be the only complete candidate after an
      // interrupted save. Keep it under a unique sibling name before using
      // the shared staging path again.
      if (!moveToUniqueSibling(tmpPath.c_str(), ".recovery-candidate-", preservedRecentTemp)) {
        LOG_ERR("PERSIST", "Failed to preserve prior Recent temporary candidate %s", tmpPath.c_str());
        return false;
      }
    } else if (!Storage.remove(tmpPath.c_str())) {
      LOG_ERR("PERSIST", "Failed to clear temporary document %s", tmpPath.c_str());
      return false;
    }
  }
  if (recentPath) {
    bool opened = false;
    if (!writeJsonDocumentToFile(tmpPath.c_str(), doc, expectedBytes, writtenBytes, outputBytes, opened, &profile)) {
      if (!opened) {
        LOG_ERR("RBS", "save_result=temp_open_failed path=%s", tmpPath.c_str());
      } else {
        LOG_ERR("RBS", "save_result=serialization_incomplete expected=%u wrote=%u output=%u overflow=%d",
                static_cast<unsigned>(expectedBytes), static_cast<unsigned>(writtenBytes),
                static_cast<unsigned>(outputBytes), doc.overflowed() ? 1 : 0);
      }
      return false;
    }
  } else if (settingsPath) {
    const uint32_t writeStartedUs = micros();
    const bool written = Storage.writeFile(tmpPath.c_str(), json);
    profile.fileWriteUs = micros() - writeStartedUs;
    if (!written) {
      LOG_ERR("PERSIST", "Failed to write temporary document %s", tmpPath.c_str());
      return false;
    }
  } else {
    bool opened = false;
    if (!writeJsonDocumentToFile(tmpPath.c_str(), doc, expectedBytes, writtenBytes, outputBytes, opened, &profile)) {
      LOG_ERR("PERSIST", "Failed to write temporary document %s", tmpPath.c_str());
      return false;
    }
    if (!persistable::serializedDocumentIsComplete(doc.overflowed(), expectedBytes, writtenBytes, outputBytes)) {
      LOG_ERR("PERSIST", "save_result=serialization_incomplete path=%s expected=%u wrote=%u output=%u overflow=%d",
              path, static_cast<unsigned>(expectedBytes), static_cast<unsigned>(writtenBytes),
              static_cast<unsigned>(outputBytes), doc.overflowed() ? 1 : 0);
      return false;
    }
  }

  // A successful write call is not enough to prove that the completed
  // temporary document reached storage intact. Settings retain their
  // semantic readback validation below. Other JSON stores use a bounded
  // byte-for-byte stream comparison, avoiding a second full JSON allocation
  // while closing the truncation window before the last good file is rotated.
  const uint32_t validationStartedUs = micros();
  if (isSettingsPath(path)) {
    String writtenTemp;
    const auto tempReadStatus = Storage.readFileChecked(tmpPath.c_str(), writtenTemp, 65536);
    bool parsed = false;
    bool valid = false;
    if (tempReadStatus == HalStorageReadStatus::Success) {
      JsonDocument verified;
      const auto error = deserializeJson(verified, writtenTemp);
      parsed = !error;
      valid = parsed && PersistableStoreBase::isSemanticallyValidSettingsDocument(verified.as<JsonVariantConst>());
    }
    LOG_INF("PERSIST", "settings_save_phase=temp_validation read=%d parse=%d validation=%d",
            tempReadStatus == HalStorageReadStatus::Success, parsed, valid);
    if (!valid) {
      LOG_ERR("PERSIST", "settings_save_phase=temp_validation_failed");
      // Preserve uncertain and malformed outputs for later recovery or
      // diagnostics. Only a complete read proves this newly written file is
      // malformed; even then move it aside rather than deleting it.
      if (tempReadStatus == HalStorageReadStatus::Success) {
        std::string failedTemp;
        if (!moveToUniqueSibling(tmpPath.c_str(), ".failed-temp-", failedTemp)) {
          LOG_ERR("PERSIST", "Failed to preserve invalid temporary settings file");
        }
      }
      return false;
    }
  } else if (!temporaryFileMatches(tmpPath.c_str(), doc, expectedBytes)) {
    LOG_ERR("PERSIST", "save_result=temporary_validation_failed path=%s bytes=%u",
            path, static_cast<unsigned>(expectedBytes));
    return false;
  }
  profile.validationUs = micros() - validationStartedUs;
  if (recentPath) {
    LOG_INF("RBS", "save_phase=temp_validated bytes=%u", static_cast<unsigned>(expectedBytes));
  }

  bool hadOriginal = settingsPath && mainState == SettingsFileState::Valid;
  if (!settingsPath) {
    const uint32_t primaryProbeStartedUs = micros();
    hadOriginal = Storage.exists(path);
    profile.probeUs += micros() - primaryProbeStartedUs;
  }
  std::string displacedBackup;
  std::string preservedRecentPrimary;
  if (hadOriginal) {
    if (fastSettingsWrite) {
      // The validated primary is the strongest known-good copy. Removing an
      // older backup before moving it into place is safe: if this removal or
      // the following rename fails, the still-live primary remains untouched.
      // Do not probe for the backup first; remove() returning false for an
      // absent backup is expected and the rename below is the authoritative
      // operation result.
      Storage.remove(backupPath.c_str());
    }
    if (recentRecoveryWrite) {
      // The primary was rejected during Recent recovery. Preserve it for
      // diagnosis and leave the validated .bak at its canonical recovery
      // path; rotating the damaged primary over .bak would destroy the only
      // known-good generation.
      if (!moveToUniqueSibling(path, ".recovered-primary-", preservedRecentPrimary)) {
        LOG_ERR("PERSIST", "Failed to preserve damaged Recent primary %s", path);
        return false;
      }
    } else {
      // Keep the old backup until the primary has been moved safely. If that
      // rename fails, both original files remain recoverable.
      const uint32_t backupPrepareStartedUs = micros();
      if ((settingsPath ? backupState != SettingsFileState::Missing : Storage.exists(backupPath.c_str())) &&
          !moveToUniqueSibling(backupPath.c_str(), ".previous-", displacedBackup)) {
        LOG_ERR("PERSIST", "Failed to preserve existing backup %s", backupPath.c_str());
        return false;
      }
      profile.backupPrepareUs += micros() - backupPrepareStartedUs;
      const uint32_t primaryRenameStartedUs = micros();
      if (!Storage.rename(path, backupPath.c_str())) {
        LOG_ERR("PERSIST", "Failed to preserve %s as %s", path, backupPath.c_str());
        if (!displacedBackup.empty() && !Storage.rename(displacedBackup.c_str(), backupPath.c_str())) {
          LOG_ERR("PERSIST", "Failed to restore prior backup %s", backupPath.c_str());
        }
        return false;
      }
      profile.primaryRenameUs += micros() - primaryRenameStartedUs;
    }
  }

  const uint32_t tempRenameStartedUs = micros();
  if (!Storage.rename(tmpPath.c_str(), path)) {
    LOG_ERR("PERSIST", "Failed to commit %s", path);
    // Keep the validated .tmp as a recovery candidate if promotion fails.
    if (recentRecoveryWrite && !preservedRecentPrimary.empty() && !Storage.exists(path)) {
      if (!Storage.rename(preservedRecentPrimary.c_str(), path)) {
        LOG_ERR("PERSIST", "Failed to restore damaged Recent primary %s", path);
      }
    } else if (!recentRecoveryWrite && hadOriginal && !Storage.exists(path)) {
      if (!Storage.rename(backupPath.c_str(), path)) {
        LOG_ERR("PERSIST", "Failed to restore backup %s", path);
      }
    }
    if (!displacedBackup.empty() && !Storage.exists(backupPath.c_str()) &&
        !Storage.rename(displacedBackup.c_str(), backupPath.c_str())) {
      LOG_ERR("PERSIST", "Failed to restore prior backup %s", backupPath.c_str());
    }
    return false;
  }
  profile.tempRenameUs += micros() - tempRenameStartedUs;

  if (settingsPath && !fastSettingsWrite) {
    const SettingsFileState committedState = inspectSettingsFile(path);
    if (committedState == SettingsFileState::Uncertain) {
      // A read error cannot prove the new primary is damaged. Leave it and
      // every backup in place so the next boot can inspect all candidates.
      LOG_ERR("PERSIST", "settings_save_phase=primary_readback_uncertain");
      return false;
    }
    if (committedState != SettingsFileState::Valid) {
      LOG_ERR("PERSIST", "settings_save_phase=primary_validation_failed");
      std::string failedPrimary;
      const char* suffix = committedState == SettingsFileState::Malformed ? ".failed-write-"
                                                                           : ".failed-unrecognized-write-";
      if (!moveToUniqueSibling(path, suffix, failedPrimary)) {
        LOG_ERR("PERSIST", "Failed to preserve invalid committed settings file");
      }
      if (hadOriginal && Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), path)) {
        LOG_ERR("PERSIST", "Failed to restore validated settings backup %s", backupPath.c_str());
      }
      if (!displacedBackup.empty() && !Storage.exists(backupPath.c_str()) &&
          !Storage.rename(displacedBackup.c_str(), backupPath.c_str())) {
        LOG_ERR("PERSIST", "Failed to restore prior backup %s", backupPath.c_str());
      }
      return false;
    }
    LOG_INF("PERSIST", "settings_save_phase=primary_validation_passed");
  } else if (fastSettingsWrite) {
    // The temporary file was fully read and semantically validated before the
    // atomic rename. A successful rename moves those same validated bytes to
    // the primary path, so a second full read is redundant.
    LOG_INF("PERSIST", "settings_save_phase=primary_promoted_from_validated_temp");
  }

  // The new document is complete; the backup is no longer needed for this
  // write. It is recreated on the next save.
  // If the original primary was missing, .bak may be the only recoverable
  // user settings. Keep it until a later save starts from a validated main.
  if (fastSettingsWrite) {
    // Keep the prior validated primary as the backup until the next changed
    // save. This both avoids a post-commit existence probe and leaves a useful
    // recovery copy if the new file is later damaged.
  } else {
    // Recent history retains the prior committed primary as one recovery
    // generation. If the primary was missing, keep an existing .bak as the
    // only copy while the new main is promoted.
    const uint32_t cleanupStartedUs = micros();
    if (!recentPath && ((!settingsPath) || hadOriginal)) {
      Storage.remove(backupPath.c_str());
    }
    if (!recentPath && !displacedBackup.empty()) Storage.remove(displacedBackup.c_str());
    profile.cleanupUs += micros() - cleanupStartedUs;
    if (recentRecoveryWrite) {
      LOG_INF("PERSIST", "recent_recovery_save=committed backup_preserved=1 primary_quarantined=%u temp_preserved=%u",
              preservedRecentPrimary.empty() ? 0U : 1U, preservedRecentTemp.empty() ? 0U : 1U);
    }
  }
  if (recentPath && !recentRecoveryWrite) {
    LOG_INF("RBS", "save_result=committed bytes=%u", static_cast<unsigned>(expectedBytes));
  }
  profile.totalUs = micros() - saveStartedUs;
  const char* fileName = path ? strrchr(path, '/') : nullptr;
  fileName = fileName ? fileName + 1 : (path ? path : "?");
  if (!settingsPath) {
    // encode_us includes the serializer's buffered writes; write_call_us is
    // nested time spent inside HalFile::write. Do not add those two fields.
    LOG_INF(recentPath ? "RBS" : "PERSIST",
            "save_profile file=%.24s bytes=%u encode_us=%lu probe_us=%lu open_us=%lu write_call_us=%lu "
            "sync_flush_us=%lu close_us=%lu validation_us=%lu backup_us=%lu primary_rename_us=%lu "
            "temp_rename_us=%lu cleanup_us=%lu total_us=%lu",
            fileName, static_cast<unsigned>(expectedBytes), static_cast<unsigned long>(profile.encodeUs),
            static_cast<unsigned long>(profile.probeUs), static_cast<unsigned long>(profile.openUs),
            static_cast<unsigned long>(profile.fileWriteUs), static_cast<unsigned long>(profile.storageFlushUs),
            static_cast<unsigned long>(profile.closeUs), static_cast<unsigned long>(profile.validationUs),
            static_cast<unsigned long>(profile.backupPrepareUs), static_cast<unsigned long>(profile.primaryRenameUs),
            static_cast<unsigned long>(profile.tempRenameUs), static_cast<unsigned long>(profile.cleanupUs),
            static_cast<unsigned long>(profile.totalUs));
  }
  return true;
}

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  const bool saved = writeDocToFileInternal(path, doc, false, false);
  if (isSettingsPath(path) && saved) settingsValidationCacheState = SettingsValidationCacheState::Valid;
  return saved;
}

bool PersistableStoreBase::writeRecoveredRecentBooksDocToFile(const char* path, const JsonDocument& doc) {
  return writeDocToFileInternal(path, doc, false, true);
}

bool PersistableStoreBase::writeSettingsDocToFile(const char* path, const JsonDocument& doc) {
  const bool saved = writeDocToFileInternal(path, doc, true, false);
  if (isSettingsPath(path)) {
    settingsValidationCacheState = saved ? SettingsValidationCacheState::Valid : SettingsValidationCacheState::Unknown;
  }
  return saved;
}

void PersistableStoreBase::observeSettingsLoad(const persistable::SettingsLoadReport& report,
                                               const bool loadSucceeded) {
  if (loadSucceeded && report.selected == 0 &&
      report.candidates[0] == persistable::SettingsCandidateResult::Valid && !report.hasReadError() &&
      !report.hasIncompatibleCandidate()) {
    settingsValidationCacheState = SettingsValidationCacheState::Valid;
    return;
  }

  const bool allMissing = report.selected == persistable::SettingsLoadReport::NO_SELECTION && !report.anyExisting &&
                          !report.hasReadError() && !report.hasIncompatibleCandidate() &&
                          report.candidates[0] == persistable::SettingsCandidateResult::Missing &&
                          report.candidates[1] == persistable::SettingsCandidateResult::Missing &&
                          report.candidates[2] == persistable::SettingsCandidateResult::Missing;
  settingsValidationCacheState = !loadSucceeded && allMissing ? SettingsValidationCacheState::Missing
                                                               : SettingsValidationCacheState::Unknown;
}

void PersistableStoreBase::invalidateSettingsValidationCache() {
  settingsValidationCacheState = SettingsValidationCacheState::Unknown;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc,
                                           persistable::SettingsLoadReport* settingsReport) {
  const bool settingsPath = isSettingsPath(path);
  const String backupPath = String(path) + ".bak";
  const String tmpPath = String(path) + ".tmp";
  const String candidates[] = {String(path), backupPath, tmpPath};
  std::array<persistable::SettingsCandidateResult, 3> candidateResults{};
  if (settingsReport) *settingsReport = {};

  for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
    const String& candidate = candidates[i];
    String json;
    HalStorageReadStatus readStatus = HalStorageReadStatus::Success;
    if (settingsPath) {
      readStatus = Storage.readFileChecked(candidate.c_str(), json, 65536);
    } else {
      if (!Storage.exists(candidate.c_str())) {
        readStatus = HalStorageReadStatus::NotFound;
      } else {
        json = Storage.readFile(candidate.c_str());
        readStatus = json.isEmpty() ? HalStorageReadStatus::IoError : HalStorageReadStatus::Success;
      }
    }

    if (readStatus == HalStorageReadStatus::NotFound || readStatus == HalStorageReadStatus::ParentMissing) {
      candidateResults[i] = persistable::SettingsCandidateResult::Missing;
      if (settingsReport && settingsPath) settingsReport->candidates[i] = candidateResults[i];
      if (settingsPath) {
        LOG_INF("PERSIST", "settings_candidate=%s exists=0 read=0 parse=0 validation=0", settingsCandidateName(i));
      }
      if (settingsPath && i == 0 && readStatus == HalStorageReadStatus::ParentMissing) {
        for (size_t sibling = 1; sibling < 3; ++sibling) {
          candidateResults[sibling] = persistable::SettingsCandidateResult::Missing;
          if (settingsReport) settingsReport->candidates[sibling] = candidateResults[sibling];
        }
        break;
      }
      continue;
    }

    if (settingsReport && settingsPath) settingsReport->anyExisting = true;
    if (readStatus == HalStorageReadStatus::Unavailable || readStatus == HalStorageReadStatus::IoError) {
      candidateResults[i] = persistable::SettingsCandidateResult::ReadError;
      if (settingsReport && settingsPath) settingsReport->candidates[i] = candidateResults[i];
      LOG_ERR("PERSIST", "Failed to read %s (storage unavailable or I/O error)", candidate.c_str());
      if (settingsPath) {
        LOG_INF("PERSIST", "settings_candidate=%s exists=unknown read=0 parse=0 validation=0",
                settingsCandidateName(i));
      }
      continue;
    }

    if (readStatus == HalStorageReadStatus::TooLarge) {
      candidateResults[i] = persistable::SettingsCandidateResult::TooLarge;
      if (settingsReport && settingsPath) {
        settingsReport->anyExisting = true;
        settingsReport->candidates[i] = candidateResults[i];
      }
      LOG_ERR("PERSIST", "Settings candidate exceeds safe read limit: %s", candidate.c_str());
      continue;
    }

    bool parsed = false;
    bool valid = false;
    if (json.isEmpty()) {
      // A successful, complete zero-byte read is confirmed malformed content,
      // not an I/O error. Deserialization below records it as malformed.
    }

    doc.clear();
    auto error = deserializeJson(doc, json);
    if (!error) {
      parsed = true;
      valid = !settingsPath || isSemanticallyValidSettingsDocument(doc.as<JsonVariantConst>());
      candidateResults[i] = valid ? persistable::SettingsCandidateResult::Valid
                                   : persistable::SettingsCandidateResult::Incompatible;
      if (settingsReport && settingsPath) settingsReport->candidates[i] = candidateResults[i];
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
      if (settingsPath) {
        if (settingsReport) settingsReport->selected = i;
        logSettingsCandidateSummary(candidateResults, settingsCandidateName(i));
      }
      return true;
    }
    parsed = false;
    candidateResults[i] = persistable::SettingsCandidateResult::Malformed;
    if (settingsReport && settingsPath) settingsReport->candidates[i] = candidateResults[i];
    if (settingsPath) {
      LOG_INF("PERSIST", "settings_candidate=%s exists=1 read=1 parse=0 validation=0", settingsCandidateName(i));
    }
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", candidate.c_str(), error.c_str());
  }

  // Missing documents are normal on first boot; malformed documents are not.
  if (settingsPath) {
    LOG_ERR("PERSIST", "settings_source=defaults");
    logSettingsCandidateSummary(candidateResults, "defaults");
  }
  return false;
}

bool PersistableStoreBase::recoverMalformedSettings(const char* path, const JsonDocument& defaults,
                                                   const persistable::SettingsLoadReport& report) {
  if (!isSettingsPath(path) || !report.confirmedCorruptionOnly()) return false;

  String markerContents;
  const auto markerStatus = Storage.readFileChecked(SETTINGS_RECOVERY_MARKER, markerContents, 128);
  if (markerStatus != HalStorageReadStatus::NotFound && markerStatus != HalStorageReadStatus::ParentMissing) {
    LOG_ERR("PERSIST", "settings_recovery=blocked marker_status=%u", static_cast<unsigned>(markerStatus));
    return false;
  }

  constexpr const char* marker = "settings-recovery-v1";
  if (!Storage.writeFile(SETTINGS_RECOVERY_MARKER, String(marker))) {
    LOG_ERR("PERSIST", "settings_recovery=failed stage=marker_write");
    return false;
  }
  String verifiedMarker;
  if (Storage.readFileChecked(SETTINGS_RECOVERY_MARKER, verifiedMarker, 128) != HalStorageReadStatus::Success ||
      strcmp(verifiedMarker.c_str(), marker) != 0) {
    LOG_ERR("PERSIST", "settings_recovery=failed stage=marker_validation");
    return false;
  }

  const String backupPath = String(path) + ".bak";
  const String tempPath = String(path) + ".tmp";
  const String candidates[] = {String(path), backupPath, tempPath};
  for (size_t i = 0; i < 3; ++i) {
    if (report.candidates[i] == persistable::SettingsCandidateResult::Missing) continue;
    bool quarantined = false;
    for (size_t suffix = 0; suffix < 1000; ++suffix) {
      const std::string target = std::string(candidates[i].c_str()) + ".corrupt-" + settingsQuarantineSuffix(i) +
                                 "-" + std::to_string(suffix);
      if (Storage.exists(target.c_str())) continue;
      if (Storage.rename(candidates[i].c_str(), target.c_str())) {
        quarantined = true;
      }
      break;
    }
    if (!quarantined) {
      LOG_ERR("PERSIST", "settings_recovery=failed stage=quarantine candidate=%s", candidates[i].c_str());
      return false;
    }
  }

  if (!writeDocToFile(path, defaults)) {
    LOG_ERR("PERSIST", "settings_recovery=failed stage=default_write");
    return false;
  }
  JsonDocument check;
  if (!readDocFromFile(path, check) || !isSemanticallyValidSettingsDocument(check.as<JsonVariantConst>())) {
    LOG_ERR("PERSIST", "settings_recovery=failed stage=readback");
    return false;
  }
  if (!Storage.remove(SETTINGS_RECOVERY_MARKER)) {
    LOG_ERR("PERSIST", "settings_recovery=warning marker_cleanup_failed");
  }
  LOG_INF("PERSIST", "settings_recovery=success source=defaults originals=quarantined");
  return true;
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
