#pragma once

#include <Arduino.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>

enum class HalStorageReadStatus : uint8_t { Success, NotFound, ParentMissing, Unavailable, IoError, TooLarge };

class HalFile {
 public:
  void bind(const std::string* data) {
    data_ = data;
    writeData_ = nullptr;
    offset_ = 0;
  }
  void bindWrite(std::string* data, const size_t writeLimit) {
    data_ = nullptr;
    writeData_ = data;
    offset_ = 0;
    writeLimit_ = writeLimit;
  }
  uint64_t fileSize64() const {
    return writeData_ ? writeData_->size() : (data_ ? data_->size() : 0);
  }
  int read(void* destination, size_t requested) {
    const std::string* source = writeData_ ? writeData_ : data_;
    if (!source || destination == nullptr || offset_ >= source->size()) return 0;
    const size_t count = std::min(requested, source->size() - offset_);
    std::memcpy(destination, source->data() + offset_, count);
    offset_ += count;
    return static_cast<int>(count);
  }
  size_t write(const void* source, const size_t requested) {
    if (!writeData_ || source == nullptr || writeData_->size() >= writeLimit_) return 0;
    const size_t count = std::min(requested, writeLimit_ - writeData_->size());
    writeData_->append(static_cast<const char*>(source), count);
    return count;
  }
  size_t write(const uint8_t byte) { return write(&byte, 1); }
  void flush() {}
  bool close() {
    data_ = nullptr;
    writeData_ = nullptr;
    offset_ = 0;
    return true;
  }

 private:
  const std::string* data_ = nullptr;
  std::string* writeData_ = nullptr;
  size_t offset_ = 0;
  size_t writeLimit_ = static_cast<size_t>(-1);
};

class SimulatedPowerLoss : public std::runtime_error {
 public:
  SimulatedPowerLoss() : std::runtime_error("simulated power loss") {}
};

class TestStorage {
 public:
  enum class CrashPoint { None, DuringTempWrite, AfterFirstRename, AfterSecondRename };

  bool mkdir(const char*) { return true; }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto it = files.find(path ? path : "");
    if (!ready || failRead || failReadPaths.find(path ? path : "") != failReadPaths.end() || it == files.end()) {
      return false;
    }
    file.bind(&it->second);
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    const std::string name = path ? path : "";
    ++writeCount;
    if (!ready || failWrite || failWritePaths.find(name) != failWritePaths.end()) return false;
    std::string& destination = files[name];
    destination.clear();
    const size_t limit = truncateTempWrite && name.ends_with(".tmp") ? 8 : static_cast<size_t>(-1);
    file.bindWrite(&destination, limit);
    return true;
  }
  bool exists(const char* path) const {
    ++existsCount;
    return files.find(path ? path : "") != files.end();
  }
  bool remove(const char* path) { return files.erase(path ? path : "") != 0; }

  bool writeFile(const char* path, const String& value) {
    ++writeCount;
    const std::string name = path ? path : "";
    if (failWrite || failWritePaths.find(name) != failWritePaths.end()) return false;
    if (crashPoint == CrashPoint::DuringTempWrite && name.ends_with(".tmp")) {
      files[name] = value.c_str();
      files[name].resize(files[name].size() / 2);
      throw SimulatedPowerLoss();
    }
    files[name] = value.c_str();
    if (truncateTempWrite && name.ends_with(".tmp")) files[name].resize(files[name].size() / 2);
    return true;
  }

  String readFile(const char* path) const {
    const auto it = files.find(path ? path : "");
    return it == files.end() ? String() : String(it->second);
  }

  HalStorageReadStatus readFileChecked(const char* path, String& content, size_t maxBytes = 65536) const {
    const std::string name = path ? path : "";
    const size_t attempt = ++readAttempts[name];
    if (!ready) return HalStorageReadStatus::Unavailable;
    if (failRead || failReadPaths.find(name) != failReadPaths.end()) return HalStorageReadStatus::IoError;
    const auto failAt = failReadOnAttempt.find(name);
    if (failAt != failReadOnAttempt.end() && attempt == failAt->second) return HalStorageReadStatus::IoError;
    const auto it = files.find(name);
    if (it == files.end()) return HalStorageReadStatus::NotFound;
    if (it->second.size() > maxBytes) return HalStorageReadStatus::TooLarge;
    content = String(it->second);
    return HalStorageReadStatus::Success;
  }

  bool rename(const char* from, const char* to) {
    const std::string source = from ? from : "";
    if (failRename || failRenameFrom.find(source) != failRenameFrom.end()) return false;
    const auto it = files.find(from ? from : "");
    const std::string target = to ? to : "";
    if (it == files.end() || files.find(target) != files.end()) return false;
    files[target] = std::move(it->second);
    files.erase(it);
    ++renameCount;
    if ((crashPoint == CrashPoint::AfterFirstRename && renameCount == 1) ||
        (crashPoint == CrashPoint::AfterSecondRename && renameCount == 2)) {
      throw SimulatedPowerLoss();
    }
    return true;
  }

  void reset() {
    files.clear();
    crashPoint = CrashPoint::None;
    renameCount = 0;
    ready = true;
    failRead = false;
    readAttempts.clear();
    failReadOnAttempt.clear();
    failReadPaths.clear();
    failWrite = false;
    truncateTempWrite = false;
    failWritePaths.clear();
    failRename = false;
    failRenameFrom.clear();
    writeCount = 0;
    existsCount = 0;
  }

  void armCrash(const CrashPoint point) {
    crashPoint = point;
    renameCount = 0;
  }

  std::unordered_map<std::string, std::string> files;
  bool ready = true;
  bool failRead = false;
  mutable std::unordered_map<std::string, size_t> readAttempts;
  std::unordered_map<std::string, size_t> failReadOnAttempt;
  std::unordered_map<std::string, bool> failReadPaths;
  bool failWrite = false;
  bool truncateTempWrite = false;
  std::unordered_map<std::string, bool> failWritePaths;
  bool failRename = false;
  std::unordered_map<std::string, bool> failRenameFrom;
  size_t writeCount = 0;
  mutable size_t existsCount = 0;

 private:
  CrashPoint crashPoint = CrashPoint::None;
  size_t renameCount = 0;
};

inline TestStorage Storage;
