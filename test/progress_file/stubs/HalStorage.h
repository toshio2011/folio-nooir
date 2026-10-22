#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

class TestStorage;

class HalFile {
 public:
  size_t write(const uint8_t* data, const size_t length);
  int read(uint8_t* data, const size_t length);
  void flush() {}
  void close() { open_ = false; }

 private:
  friend class TestStorage;
  std::string path_;
  size_t position_ = 0;
  bool open_ = false;
};

class TestStorage {
 public:
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (failWrite) return false;
    files[path].clear();
    file.path_ = path;
    file.position_ = 0;
    file.open_ = true;
    return true;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (files.find(path) == files.end()) return false;
    file.path_ = path;
    file.position_ = 0;
    file.open_ = true;
    return true;
  }

  bool exists(const char* path) const { return files.find(path ? path : "") != files.end(); }
  bool remove(const char* path) { return files.erase(path ? path : "") != 0; }

  bool rename(const char* oldPath, const char* newPath) {
    const std::string from = oldPath ? oldPath : "";
    const std::string to = newPath ? newPath : "";
    if (failRenameFrom == from || files.find(from) == files.end() || files.find(to) != files.end()) return false;
    files[to] = files[from];
    files.erase(from);
    if (crashAfterRename > 0 && ++renameCount == crashAfterRename) throw std::runtime_error("simulated power loss");
    return true;
  }

  std::map<std::string, std::string> files;
  std::string failRenameFrom;
  int crashAfterRename = 0;
  int renameCount = 0;
  bool failWrite = false;
};

inline TestStorage Storage;

inline size_t HalFile::write(const uint8_t* data, const size_t length) {
  if (!open_) return 0;
  auto& output = Storage.files[path_];
  output.append(reinterpret_cast<const char*>(data), length);
  position_ += length;
  return length;
}

inline int HalFile::read(uint8_t* data, const size_t length) {
  if (!open_) return -1;
  const auto& input = Storage.files[path_];
  const size_t available = input.size() - position_;
  const size_t readLength = available < length ? available : length;
  for (size_t i = 0; i < readLength; ++i) data[i] = static_cast<uint8_t>(input[position_ + i]);
  position_ += readLength;
  return static_cast<int>(readLength);
}
