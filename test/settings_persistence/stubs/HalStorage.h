#pragma once

#include <Arduino.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

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
    const std::string name = path ? path : "";
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

  bool rename(const char* from, const char* to) {
    const auto it = files.find(from ? from : "");
    if (it == files.end()) return false;
    files[to ? to : ""] = std::move(it->second);
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
  }

  void armCrash(const CrashPoint point) {
    crashPoint = point;
    renameCount = 0;
  }

  std::unordered_map<std::string, std::string> files;

 private:
  CrashPoint crashPoint = CrashPoint::None;
  size_t renameCount = 0;
};

inline TestStorage Storage;
