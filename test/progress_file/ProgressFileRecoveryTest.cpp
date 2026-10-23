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

void readExactPreservesAndRecoversFixedSizeCandidates() {
  Storage = {};
  uint8_t data[4]{};
  expect(ProgressFile::readExact("/cache", data, 0) == ProgressFile::ExactReadResult::Invalid,
         "zero-length progress reads are rejected before storage access");
  uint8_t oversizedDestination[8]{};
  expect(ProgressFile::readExact("/cache", oversizedDestination, sizeof(oversizedDestination)) ==
             ProgressFile::ExactReadResult::Invalid,
         "unsupported progress record lengths are rejected before allocation");
  expect(ProgressFile::readExact("/cache", data, sizeof(data)) == ProgressFile::ExactReadResult::Missing,
         "missing progress is distinct from a damaged record");

  Storage.files["/cache/progress.bin"] = "bad";
  Storage.files["/cache/progress.bin.bak"] = std::string("\x03\x00\x00\x00", 4);
  const auto before = Storage.files;
  expect(ProgressFile::readExact("/cache", data, sizeof(data)) == ProgressFile::ExactReadResult::Recovered,
         "valid backup recovers a short primary");
  expect(data[0] == 3 && data[1] == 0 && data[2] == 0 && data[3] == 0,
         "backup page bytes are returned intact");
  expect(Storage.files == before, "read-only recovery leaves primary and backup untouched");

  Storage = {};
  Storage.files["/cache/progress.bin.tmp"] = std::string("\x07\x00\x00\x00", 4);
  expect(ProgressFile::readExact("/cache", data, sizeof(data)) == ProgressFile::ExactReadResult::Recovered,
         "valid temporary candidate remains recoverable");
  expect(data[0] == 7 && Storage.exists("/cache/progress.bin.tmp"),
         "temporary candidate is read without renaming or deleting it");

  Storage = {};
  Storage.files["/cache/progress.bin"] = "oversized";
  expect(ProgressFile::readExact("/cache", data, sizeof(data)) == ProgressFile::ExactReadResult::Invalid,
         "oversized progress is invalid instead of silently becoming page zero");

  Storage = {};
  Storage.readStatuses["/cache/progress.bin"] = HalStorageReadStatus::Unavailable;
  Storage.readStatuses["/cache/progress.bin.bak"] = HalStorageReadStatus::Unavailable;
  Storage.readStatuses["/cache/progress.bin.tmp"] = HalStorageReadStatus::Unavailable;
  expect(ProgressFile::readExact("/cache", data, sizeof(data)) == ProgressFile::ExactReadResult::Unavailable,
         "transient SD failure remains distinct from a missing progress file");

  Storage = {};
  Storage.readStatuses["/cache/progress.bin"] = HalStorageReadStatus::Unavailable;
  Storage.files["/cache/progress.bin.bak"] = std::string("\x09\x00\x00\x00", 4);
  expect(ProgressFile::readExact("/cache", data, sizeof(data)) == ProgressFile::ExactReadResult::Recovered,
         "backup remains readable while a transient primary read failure is reported as recovery");
  expect(data[0] == 9, "recovered backup page bytes are available without authorizing primary replacement");
}

}  // namespace

int main() {
  successfulWritePublishesAndCleansBackup();
  failedPublishRestoresOldCanonical();
  readRecoversPreviousProgressAfterInterruptedReplacement();
  readUsesNewCanonicalAndFallsBackToBackup();
  readExactPreservesAndRecoversFixedSizeCandidates();
  if (failures == 0) std::cout << "ProgressFile recovery checks passed\n";
  return failures == 0 ? 0 : 1;
}
