#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace CssCacheTestHooks {
inline bool failClose = false;
inline bool shortWrite = false;
}  // namespace CssCacheTestHooks

class HalFile {
 public:
  static HalFile readBuffer(const std::string& contents) {
    HalFile file;
    file.readData.assign(contents.begin(), contents.end());
    file.readOnly = true;
    file.open = true;
    return file;
  }

  int read(void* destination, const size_t requested) {
    if (!open || !readOnly) return 0;
    const size_t count = std::min(requested, readData.size() - position);
    if (count != 0) std::memcpy(destination, readData.data() + position, count);
    position += count;
    return static_cast<int>(count);
  }

  size_t available() const { return open && readOnly ? readData.size() - position : 0; }
  size_t size() const { return readOnly ? readData.size() : writeData->size(); }

  size_t write(const void* source, const size_t count) {
    if (!open || readOnly || writeData == nullptr) return 0;
    if (CssCacheTestHooks::shortWrite) {
      CssCacheTestHooks::shortWrite = false;
      const size_t partial = count == 0 ? 0 : count - 1;
      const auto* bytes = static_cast<const uint8_t*>(source);
      writeData->insert(writeData->end(), bytes, bytes + partial);
      return partial;
    }
    const auto* bytes = static_cast<const uint8_t*>(source);
    writeData->insert(writeData->end(), bytes, bytes + count);
    return count;
  }

  size_t write(const uint8_t value) { return write(&value, sizeof(value)); }

  void flush() {}
  bool close() {
    open = false;
    const bool ok = !CssCacheTestHooks::failClose;
    CssCacheTestHooks::failClose = false;
    return ok;
  }
  bool isOpen() const { return open; }
  operator bool() const { return open; }

 private:
  friend class HalStorage;

  void bind(std::vector<uint8_t>* data) {
    writeData = data;
    readOnly = false;
    open = true;
    position = 0;
  }

  std::vector<uint8_t> readData;
  std::vector<uint8_t>* writeData = nullptr;
  size_t position = 0;
  bool readOnly = false;
  bool open = false;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool exists(const char* path) const { return files.find(path) != files.end(); }

  bool remove(const char* path) {
    if (failRemoveAt != 0 && ++removeCalls == failRemoveAt) return false;
    return files.erase(path) != 0;
  }

  bool rename(const char* oldPath, const char* newPath) {
    const size_t call = ++renameCalls;
    if (failRenameAt != 0 && call == failRenameAt) return false;
    const auto it = files.find(oldPath);
    if (it == files.end() || files.find(newPath) != files.end()) return false;
    files.emplace(newPath, std::move(it->second));
    files.erase(it);
    return true;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (failOpenRead) {
      failOpenRead = false;
      return false;
    }
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file.readData = it->second;
    file.writeData = nullptr;
    file.position = 0;
    file.readOnly = true;
    file.open = true;
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (failOpenWrite) {
      failOpenWrite = false;
      return false;
    }
    auto& contents = files[path];
    contents.clear();
    file.bind(&contents);
    return true;
  }

  void clear() {
    files.clear();
    failOpenRead = false;
    failOpenWrite = false;
    failRenameAt = 0;
    renameCalls = 0;
    failRemoveAt = 0;
    removeCalls = 0;
    CssCacheTestHooks::failClose = false;
    CssCacheTestHooks::shortWrite = false;
  }

  bool failOpenRead = false;
  bool failOpenWrite = false;
  size_t failRenameAt = 0;
  size_t renameCalls = 0;
  size_t failRemoveAt = 0;
  size_t removeCalls = 0;

 private:
  std::unordered_map<std::string, std::vector<uint8_t>> files;
};

#define Storage HalStorage::getInstance()
