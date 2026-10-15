#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

class TestStorage;

class HalFile {
 public:
  size_t write(const uint8_t* data, size_t size);
  void close() { open = false; }

 private:
  friend class TestStorage;
  std::string path;
  bool open = false;
};

class TestStorage {
 public:
  bool exists(const char* path) const { return files.find(path ? path : "") != files.end(); }
  bool remove(const char* path) { return files.erase(path ? path : "") != 0; }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    file.path = path ? path : "";
    file.open = true;
    files[file.path].clear();
    return true;
  }
  bool rename(const char* from, const char* to) {
    const auto it = files.find(from ? from : "");
    if (it == files.end()) return false;
    files[to ? to : ""] = std::move(it->second);
    files.erase(it);
    return true;
  }
  void clear() { files.clear(); }

  std::unordered_map<std::string, std::string> files;
};

inline TestStorage Storage;

inline size_t HalFile::write(const uint8_t* data, const size_t size) {
  if (!open) return 0;
  Storage.files[path].append(reinterpret_cast<const char*>(data), size);
  return size;
}
