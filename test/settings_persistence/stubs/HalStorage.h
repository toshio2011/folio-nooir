#pragma once

#include <Arduino.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

enum class HalStorageReadStatus : uint8_t { Success, NotFound, ParentMissing, Unavailable, IoError, TooLarge };

class SimulatedPowerLoss : public std::runtime_error {
 public:
  SimulatedPowerLoss() : std::runtime_error("simulated power loss") {}
};

class TestStorage {
 public:
  enum class CrashPoint { None, DuringTempWrite, AfterFirstRename, AfterSecondRename };

  bool mkdir(const char*) { return true; }
  bool exists(const char* path) const { return files.find(path ? path : "") != files.end(); }
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
    failWritePaths.clear();
    failRename = false;
    failRenameFrom.clear();
    writeCount = 0;
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
  std::unordered_map<std::string, bool> failWritePaths;
  bool failRename = false;
  std::unordered_map<std::string, bool> failRenameFrom;
  size_t writeCount = 0;

 private:
  CrashPoint crashPoint = CrashPoint::None;
  size_t renameCount = 0;
};

inline TestStorage Storage;
