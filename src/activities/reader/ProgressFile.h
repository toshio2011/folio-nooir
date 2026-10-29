#pragma once

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ProgressFile {

struct SaveTiming {
  explicit SaveTiming(const size_t length, const bool enabled)
      : bytes(length), active(enabled), startedUs(enabled ? micros() : 0) {}

  uint32_t beginPhase() const { return active ? micros() : 0; }
  void endPhase(uint32_t& elapsedUs, const uint32_t phaseStartedUs) {
    if (active) elapsedUs += micros() - phaseStartedUs;
  }

  ~SaveTiming() {
    if (!active) return;
    LOG_INF("PRG", "save_profile file=progress.bin bytes=%u result=%d open_us=%lu write_us=%lu flush_us=%lu "
                    "close_us=%lu backup_rename_us=%lu probe_us=%lu backup_cleanup_us=%lu "
                    "backup_retry_us=%lu publish_rename_us=%lu rollback_us=%lu cleanup_us=%lu total_us=%lu",
            static_cast<unsigned>(bytes), result ? 1 : 0, static_cast<unsigned long>(openUs),
            static_cast<unsigned long>(writeUs), static_cast<unsigned long>(flushUs),
            static_cast<unsigned long>(closeUs), static_cast<unsigned long>(backupRenameUs),
            static_cast<unsigned long>(probeUs), static_cast<unsigned long>(backupCleanupUs),
            static_cast<unsigned long>(backupRetryUs), static_cast<unsigned long>(publishRenameUs),
            static_cast<unsigned long>(rollbackUs), static_cast<unsigned long>(cleanupUs),
            static_cast<unsigned long>(micros() - startedUs));
  }

  const size_t bytes;
  const bool active;
  const uint32_t startedUs;
  bool result = false;
  uint32_t openUs = 0;
  uint32_t writeUs = 0;
  uint32_t flushUs = 0;
  uint32_t closeUs = 0;
  uint32_t backupRenameUs = 0;
  uint32_t probeUs = 0;
  uint32_t backupCleanupUs = 0;
  uint32_t backupRetryUs = 0;
  uint32_t publishRenameUs = 0;
  uint32_t rollbackUs = 0;
  uint32_t cleanupUs = 0;
};

enum class ExactReadResult : uint8_t { Success, Recovered, Missing, Invalid, Unavailable };

inline std::string backupPath(const std::string& cachePath) {
  return cachePath + "/progress.bin.bak";
}

// Read one fixed-size progress record without modifying recovery files. A
// malformed or temporarily unreadable primary may still have a usable backup
// or completed temporary candidate. Callers can distinguish a genuinely new
// book from damaged/unavailable progress and avoid replacing it with page 0.
inline ExactReadResult readExact(const std::string& cachePath, uint8_t* destination, const size_t length) {
  // Progress records are fixed-size: 32-bit records are used by XTC/TXT, and
  // EPUB's extended record is six bytes. Reject any other size before reading
  // so this recovery helper can never become an unbounded allocator.
  if (destination == nullptr || (length != 4 && length != 6)) return ExactReadResult::Invalid;

  const std::string candidates[] = {cachePath + "/progress.bin", backupPath(cachePath),
                                    cachePath + "/progress.bin.tmp"};
  bool sawInvalid = false;
  bool sawUnavailable = false;
  for (const auto& path : candidates) {
    String contents;
    const HalStorageReadStatus status = Storage.readFileChecked(path.c_str(), contents, length);
    if (status == HalStorageReadStatus::NotFound || status == HalStorageReadStatus::ParentMissing) continue;
    if (status == HalStorageReadStatus::Unavailable || status == HalStorageReadStatus::IoError) {
      sawUnavailable = true;
      continue;
    }
    if (status == HalStorageReadStatus::TooLarge) {
      sawInvalid = true;
      continue;
    }
    if (status != HalStorageReadStatus::Success || contents.length() != length) {
      sawInvalid = true;
      continue;
    }
    std::memcpy(destination, contents.c_str(), length);
    // A non-canonical candidate is useful for positioning the reader, but it
    // must not be treated as authoritative: a later save could otherwise
    // replace a primary file that was temporarily unreadable.
    return path == candidates[0] ? ExactReadResult::Success : ExactReadResult::Recovered;
  }

  if (sawUnavailable) return ExactReadResult::Unavailable;
  return sawInvalid ? ExactReadResult::Invalid : ExactReadResult::Missing;
}

// Open the canonical file first. If a reset interrupted the two-step FAT
// replacement after moving the old file aside, restore that saved copy before
// returning the progress file. The backup path remains a read fallback when a
// restore rename is temporarily unavailable.
inline bool openForRead(const char* moduleName, const std::string& cachePath, HalFile& file) {
  const std::string finalPath = cachePath + "/progress.bin";
  if (Storage.openFileForRead(moduleName, finalPath, file)) return true;

  const std::string backup = backupPath(cachePath);
  if (!Storage.exists(backup.c_str())) return false;
  if (!Storage.exists(finalPath.c_str()) && Storage.rename(backup.c_str(), finalPath.c_str()) &&
      Storage.openFileForRead(moduleName, finalPath, file)) {
    LOG_INF("PRG", "Restored interrupted progress save: %s", finalPath.c_str());
    return true;
  }
  return Storage.openFileForRead(moduleName, backup, file);
}

// Writes `len` bytes of reader progress to `<cachePath>/progress.bin` without
// ever leaving the canonical file half-written.
//
// The bytes go to a temporary `progress.bin.tmp` first; only once that is fully
// written and closed is it renamed over progress.bin. An interrupted write
// (power loss or a crash mid-SPI) therefore damages only the throwaway temp file.
// Previously a truncate-in-place write that was cut short left progress.bin with
// a broken FAT cluster chain that the firmware could neither rewrite nor clear,
// stranding the book on an old page (issue #2275).
//
// FAT cannot replace an existing file in one metadata operation. The current
// canonical is therefore renamed to .bak before the temp is published; readers
// restore or read that backup if power is lost in between.
//
// Note: this prevents a failed replacement from deleting the prior progress.
// It cannot repair an already-corrupted progress.bin or a FAT directory that
// the card can no longer rename; those cases may still require host recovery.
//
// Returns true only if the new progress.bin is fully in place.
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len,
                        const bool profileTiming = false) {
  const std::string finalPath = cachePath + "/progress.bin";
  const std::string tmpPath = cachePath + "/progress.bin.tmp";
  const std::string backup = backupPath(cachePath);
  SaveTiming profile(len, profileTiming);

  {
    HalFile f;
    uint32_t phaseStartedUs = profile.beginPhase();
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      profile.endPhase(profile.openUs, phaseStartedUs);
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }
    profile.endPhase(profile.openUs, phaseStartedUs);
    phaseStartedUs = profile.beginPhase();
    const size_t written = f.write(data, len);
    profile.endPhase(profile.writeUs, phaseStartedUs);
    if (written != len) {
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      phaseStartedUs = profile.beginPhase();
      (void)f.close();
      profile.endPhase(profile.closeUs, phaseStartedUs);
      return false;
    }
    phaseStartedUs = profile.beginPhase();
    f.flush();
    profile.endPhase(profile.flushUs, phaseStartedUs);
    phaseStartedUs = profile.beginPhase();
    (void)f.close();
    profile.endPhase(profile.closeUs, phaseStartedUs);
    // Close the temp file before the rename below; SdFat cannot safely rename
    // a path that still has an open FsFile.
  }

  uint32_t phaseStartedUs = profile.beginPhase();
  bool hadFinal = Storage.rename(finalPath.c_str(), backup.c_str());
  profile.endPhase(profile.backupRenameUs, phaseStartedUs);
  bool finalExists = false;
  if (!hadFinal) {
    phaseStartedUs = profile.beginPhase();
    finalExists = Storage.exists(finalPath.c_str());
    profile.endPhase(profile.probeUs, phaseStartedUs);
  }
  if (!hadFinal && finalExists) {
    phaseStartedUs = profile.beginPhase();
    const bool backupExists = Storage.exists(backup.c_str());
    profile.endPhase(profile.probeUs, phaseStartedUs);
    // A backup beside a valid canonical is a remnant from a completed prior
    // save. Remove it only when the canonical still exists, then retry moving
    // the canonical aside. If removal fails, the valid progress stays in place.
    if (backupExists) {
      phaseStartedUs = profile.beginPhase();
      const bool removed = Storage.remove(backup.c_str());
      profile.endPhase(profile.backupCleanupUs, phaseStartedUs);
      if (!removed) {
        LOG_ERR("PRG", "Could not clear stale progress backup: %s", backup.c_str());
        return false;
      }
    }
    phaseStartedUs = profile.beginPhase();
    hadFinal = Storage.rename(finalPath.c_str(), backup.c_str());
    profile.endPhase(profile.backupRetryUs, phaseStartedUs);
    if (!hadFinal) {
      LOG_ERR("PRG", "Could not protect current progress before replacement: %s", finalPath.c_str());
      return false;
    }
  }

  phaseStartedUs = profile.beginPhase();
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    profile.endPhase(profile.publishRenameUs, phaseStartedUs);
    LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
    if (hadFinal) {
      phaseStartedUs = profile.beginPhase();
      const bool restored = Storage.rename(backup.c_str(), finalPath.c_str());
      profile.endPhase(profile.rollbackUs, phaseStartedUs);
      if (!restored) LOG_ERR("PRG", "Could not restore previous progress; backup retained: %s", backup.c_str());
    }
    return false;
  }
  profile.endPhase(profile.publishRenameUs, phaseStartedUs);
  if (hadFinal) {
    phaseStartedUs = profile.beginPhase();
    const bool removed = Storage.remove(backup.c_str());
    profile.endPhase(profile.cleanupUs, phaseStartedUs);
    if (!removed) LOG_INF("PRG", "New progress saved; stale backup could not be removed: %s", backup.c_str());
  }
  profile.result = true;
  return true;
}

}  // namespace ProgressFile
