#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

inline std::string backupPath(const std::string& cachePath) {
  return cachePath + "/progress.bin.bak";
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
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len) {
  const std::string finalPath = cachePath + "/progress.bin";
  const std::string tmpPath = cachePath + "/progress.bin.tmp";
  const std::string backup = backupPath(cachePath);

  {
    HalFile f;
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }
    const size_t written = f.write(data, len);
    if (written != len) {
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      return false;
    }
    f.flush();
    // f (the temp file) is closed at scope exit (DESTRUCTOR_CLOSES_FILE=1) before
    // the rename below -- SdFat must not rename a path that still has an open FsFile.
  }

  bool hadFinal = Storage.rename(finalPath.c_str(), backup.c_str());
  if (!hadFinal && Storage.exists(finalPath.c_str())) {
    // A backup beside a valid canonical is a remnant from a completed prior
    // save. Remove it only when the canonical still exists, then retry moving
    // the canonical aside. If removal fails, the valid progress stays in place.
    if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) {
      LOG_ERR("PRG", "Could not clear stale progress backup: %s", backup.c_str());
      return false;
    }
    hadFinal = Storage.rename(finalPath.c_str(), backup.c_str());
    if (!hadFinal) {
      LOG_ERR("PRG", "Could not protect current progress before replacement: %s", finalPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
    if (hadFinal && !Storage.rename(backup.c_str(), finalPath.c_str())) {
      LOG_ERR("PRG", "Could not restore previous progress; backup retained: %s", backup.c_str());
    }
    return false;
  }
  if (hadFinal && !Storage.remove(backup.c_str())) {
    LOG_INF("PRG", "New progress saved; stale backup could not be removed: %s", backup.c_str());
  }
  return true;
}

}  // namespace ProgressFile
