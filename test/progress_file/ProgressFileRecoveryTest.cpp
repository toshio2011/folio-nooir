#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "activities/reader/ProgressFile.h"

namespace {

int failures = 0;

void expect(const bool condition, const char* name) {
  if (condition) return;
  std::cerr << "FAILED: " << name << '\n';
  ++failures;
}

std::string readProgress(const std::string& path, const char* module = "TEST") {
  HalFile file;
  if (!ProgressFile::openForRead(module, path, file)) return {};
  std::array<uint8_t, 16> data{};
  const int count = file.read(data.data(), data.size());
  file.close();
  return count < 0 ? std::string()
                   : std::string(reinterpret_cast<const char*>(data.data()), static_cast<size_t>(count));
}

void successfulWritePublishesAndCleansBackup() {
  Storage = {};
  Storage.files["/cache/progress.bin"] = "old";
  const uint8_t next[] = {'n', 'e', 'w'};

  expect(ProgressFile::writeAtomic("/cache", next, sizeof(next)), "successful write returns true");
  expect(Storage.files["/cache/progress.bin"] == "new", "new progress becomes canonical");
  expect(!Storage.exists("/cache/progress.bin.bak"), "successful write removes backup");
}

void failedPublishRestoresOldCanonical() {
  Storage = {};
  Storage.files["/cache/progress.bin"] = "old";
  Storage.failRenameFrom = "/cache/progress.bin.tmp";
  const uint8_t next[] = {'n', 'e', 'w'};

  expect(!ProgressFile::writeAtomic("/cache", next, sizeof(next)), "failed publish returns false");
  expect(Storage.files["/cache/progress.bin"] == "old", "failed publish restores previous progress");
  expect(!Storage.exists("/cache/progress.bin.bak"), "successful restore consumes backup");
}

void readRecoversPreviousProgressAfterInterruptedReplacement() {
  Storage = {};
  Storage.files["/cache/progress.bin"] = "old";
  Storage.crashAfterRename = 1;
  const uint8_t next[] = {'n', 'e', 'w'};
  try {
    (void)ProgressFile::writeAtomic("/cache", next, sizeof(next));
  } catch (const std::runtime_error&) {
  }

  expect(!Storage.exists("/cache/progress.bin"), "simulated interruption leaves canonical absent");
  expect(readProgress("/cache") == "old", "read restores old progress after interruption");
  expect(Storage.files["/cache/progress.bin"] == "old", "recovered progress is canonical");
}

void readUsesNewCanonicalAndFallsBackToBackup() {
  Storage = {};
  Storage.files["/cache/progress.bin"] = "new";
  Storage.files["/cache/progress.bin.bak"] = "old";
  expect(readProgress("/cache") == "new", "canonical wins over stale backup");

  Storage = {};
  Storage.files["/cache/progress.bin.bak"] = "old";
  Storage.failRenameFrom = "/cache/progress.bin.bak";
  expect(readProgress("/cache") == "old", "backup remains readable when restore rename fails");
}

}  // namespace

int main() {
  successfulWritePublishesAndCleansBackup();
  failedPublishRestoresOldCanonical();
  readRecoversPreviousProgressAfterInterruptedReplacement();
  readUsesNewCanonicalAndFallsBackToBackup();
  if (failures == 0) std::cout << "ProgressFile recovery checks passed\n";
  return failures == 0 ? 0 : 1;
}
