#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class TestStorage;

class HalFile {
 public:
  int write(const uint8_t* bytes, const size_t length) {
    if (!open || !writable || !data) return 0;
    if (position + length > data->size()) data->resize(position + length);
    if (length > 0) std::memcpy(data->data() + position, bytes, length);
    position += length;
    return static_cast<int>(length);
  }

  int read(uint8_t* bytes, const size_t length) {
    if (!open || writable || !data || position >= data->size()) return 0;
    const size_t count = std::min(length, data->size() - position);
    if (count > 0) std::memcpy(bytes, data->data() + position, count);
    position += count;
    return static_cast<int>(count);
  }

  size_t size() const { return data ? data->size() : 0; }
  void flush() {}
  void close() { open = false; }
  bool isOpen() const { return open; }

 private:
  friend class TestStorage;
  void bind(std::vector<uint8_t>* value, const bool writeMode) {
    data = value;
    writable = writeMode;
    position = 0;
    open = value != nullptr;
  }

  std::vector<uint8_t>* data = nullptr;
  size_t position = 0;
  bool writable = false;
  bool open = false;
};

class TestStorage {
 public:
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (unreadablePaths.count(path)) return false;
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file.bind(&it->second, false);
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    auto& contents = files[path];
    contents.clear();
    file.bind(&contents, true);
    return true;
  }

  bool exists(const char* path) const {
    const std::string value = path ? path : "";
    existenceQueries.push_back(value);
    return files.find(value) != files.end();
  }

  bool remove(const char* path) { return files.erase(path ? path : "") != 0; }

  bool rename(const char* from, const char* to) {
    const auto source = files.find(from ? from : "");
    const std::string destination = to ? to : "";
    if (source == files.end() || files.find(destination) != files.end()) return false;
    auto contents = std::move(source->second);
    files.erase(source);
    files.emplace(destination, std::move(contents));
    return true;
  }

  void reset() {
    files.clear();
    unreadablePaths.clear();
    existenceQueries.clear();
  }

  std::unordered_map<std::string, std::vector<uint8_t>> files;
  std::unordered_set<std::string> unreadablePaths;
  mutable std::vector<std::string> existenceQueries;
};

inline TestStorage Storage;
