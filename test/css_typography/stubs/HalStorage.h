#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class HalFile {
 public:
  int read(void*, size_t) { return 0; }
  int available() const { return 0; }
  size_t write(const void*, size_t size) { return size; }
  size_t write(uint8_t) { return 1; }
  void close() {}
  bool isOpen() const { return false; }
  operator bool() const { return false; }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool exists(const char*) { return false; }
  bool remove(const char*) { return true; }
  bool openFileForRead(const char*, const std::string&, HalFile&) { return false; }
  bool openFileForWrite(const char*, const std::string&, HalFile&) { return false; }
};

#define Storage HalStorage::getInstance()
