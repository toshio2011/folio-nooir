#pragma once

#include <Arduino.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

enum class HalStorageReadStatus : uint8_t { Success, NotFound, ParentMissing, Unavailable, IoError, TooLarge };

class HalFile {
 public:
  void bind(const std::string* value) {
    data_ = value;
    writeData_ = nullptr;
    offset_ = 0;
  }
  void bindWrite(std::string* value) {
    data_ = nullptr;
    writeData_ = value;
    offset_ = 0;
  }
  uint64_t fileSize64() const { return writeData_ ? writeData_->size() : (data_ ? data_->size() : 0); }
  int read(void* destination, size_t requested) {
    const std::string* source = writeData_ ? writeData_ : data_;
    if (!source || !destination || offset_ >= source->size()) return 0;
    const size_t count = std::min(requested, source->size() - offset_);
    std::memcpy(destination, source->data() + offset_, count);
    offset_ += count;
    return static_cast<int>(count);
  }
  size_t write(const void* source, size_t requested) {
    if (!writeData_ || !source) return 0;
    writeData_->append(static_cast<const char*>(source), requested);
    return requested;
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
};

class SimulatedPowerLoss : public std::runtime_error {
 public:
  SimulatedPowerLoss() : std::runtime_error("simulated power loss") {}
};

class TestStorage {
 public:
  enum class CrashPoint { None, AfterFirstRename, AfterSecondRename };

  bool ready() const { return sdReady; }
  bool mkdir(const char*) { return true; }
  bool exists(const char* path) const { return files.find(path ? path : "") != files.end(); }
  bool remove(const char* path) { return files.erase(path ? path : "") != 0; }
  bool rename(const char* from, const char* to) {
    const std::string source = from ? from : "";
    const std::string target = to ? to : "";
    const auto it = files.find(source);
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
  bool writeFile(const char* path, const String& value) {
    ++writeCount;
    if (failWrite) return false;
    files[path ? path : ""] = value.c_str();
    return true;
  }
  String readFile(const char* path) const {
    const auto it = files.find(path ? path : "");
    return it == files.end() ? String() : String(it->second);
  }
  HalStorageReadStatus readFileChecked(const char* path, String& content, size_t maxBytes = 65536) const {
    const std::string key = path ? path : "";
    ++readCount;
    if (!sdReady) return HalStorageReadStatus::Unavailable;
    if (readErrors.find(key) != readErrors.end()) return HalStorageReadStatus::IoError;
    const auto it = files.find(key);
    if (it == files.end()) return HalStorageReadStatus::NotFound;
    if (it->second.size() > maxBytes) return HalStorageReadStatus::TooLarge;
    content = it->second;
    return HalStorageReadStatus::Success;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto it = files.find(path ? path : "");
    if (it == files.end()) return false;
    file.bind(&it->second);
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    if (!sdReady || failWrite) return false;
    std::string& destination = files[path ? path : ""];
    destination.clear();
    file.bindWrite(&destination);
    ++writeCount;
    return true;
  }
  void reset() {
    files.clear();
    readErrors.clear();
    sdReady = true;
    failWrite = false;
    writeCount = 0;
    readCount = 0;
    crashPoint = CrashPoint::None;
    renameCount = 0;
  }

  void armCrash(const CrashPoint point) {
    crashPoint = point;
    renameCount = 0;
  }

  std::unordered_map<std::string, std::string> files;
  std::unordered_map<std::string, bool> readErrors;
  bool sdReady = true;
  bool failWrite = false;
  size_t writeCount = 0;
  mutable size_t readCount = 0;

 private:
  CrashPoint crashPoint = CrashPoint::None;
  size_t renameCount = 0;
};

inline TestStorage Storage;
