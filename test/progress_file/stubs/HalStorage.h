#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

enum class HalStorageReadStatus : uint8_t { Success, NotFound, ParentMissing, Unavailable, IoError, TooLarge };

class String {
 public:
  size_t length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  void assign(const std::string& value) { value_ = value; }

 private:
  std::string value_;
};

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

  HalStorageReadStatus readFileChecked(const char* path, String& content, const size_t maxBytes) const {
    const std::string requested = path ? path : "";
    const auto forcedStatus = readStatuses.find(requested);
    if (forcedStatus != readStatuses.end()) return forcedStatus->second;
    const auto file = files.find(requested);
    if (file == files.end()) return HalStorageReadStatus::NotFound;
    if (file->second.size() > maxBytes) return HalStorageReadStatus::TooLarge;
    content.assign(file->second);
    return HalStorageReadStatus::Success;
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
  std::map<std::string, HalStorageReadStatus> readStatuses;
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
