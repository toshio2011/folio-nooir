#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
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
  HalFile() = default;
  ~HalFile() { close(); }

  int read(uint8_t* data, size_t length);
  size_t write(const uint8_t* data, size_t length);
  void flush() {}
  bool close() {
    open_ = false;
    return closeSucceeds_;
  }
  uint64_t fileSize64() const;
  size_t position() const { return position_; }

 private:
  friend class TestStorage;
  std::string path_;
  size_t position_ = 0;
  bool open_ = false;
  bool closeSucceeds_ = true;
};

class TestStorage {
 public:
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (unreadablePaths.contains(path) || !files.contains(path)) return false;
    file.path_ = path;
    file.position_ = 0;
    file.open_ = true;
    file.closeSucceeds_ = !closeFailurePaths.contains(path);
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (failWrites || writeFailurePaths.contains(path)) return false;
    files[path].clear();
    file.path_ = path;
    file.position_ = 0;
    file.open_ = true;
    file.closeSucceeds_ = !closeFailurePaths.contains(path);
    return true;
  }

  HalStorageReadStatus readFileChecked(const char* rawPath, String& contents, const size_t maxBytes) const {
    const std::string path = rawPath ? rawPath : "";
    const auto forced = checkedReadStatuses.find(path);
    if (forced != checkedReadStatuses.end()) return forced->second;
    const auto file = files.find(path);
    if (unreadablePaths.contains(path)) return HalStorageReadStatus::Unavailable;
    if (file == files.end()) return HalStorageReadStatus::NotFound;
    if (file->second.size() > maxBytes) return HalStorageReadStatus::TooLarge;
    contents.assign(file->second);
    return HalStorageReadStatus::Success;
  }

  bool exists(const char* rawPath) const {
    const std::string path = rawPath ? rawPath : "";
    if (existsFalsePaths.contains(path)) return false;
    return files.contains(path) || directories.contains(path);
  }

  bool remove(const char* rawPath) { return files.erase(rawPath ? rawPath : "") != 0; }

  bool rename(const char* oldRawPath, const char* newRawPath) {
    const std::string oldPath = oldRawPath ? oldRawPath : "";
    const std::string newPath = newRawPath ? newRawPath : "";
    if (renameFailures.contains(oldPath) || !files.contains(oldPath) || files.contains(newPath)) return false;
    files[newPath] = files[oldPath];
    files.erase(oldPath);
    return true;
  }

  bool mkdir(const char* rawPath) {
    directories.insert(rawPath ? rawPath : "");
    return true;
  }

  bool removeDir(const char* rawPath) {
    ++removeDirCalls;
    const std::string path = rawPath ? rawPath : "";
    if (removeDirFails) return false;
    const std::string prefix = path.back() == '/' ? path : path + "/";
    for (auto it = files.begin(); it != files.end();) {
      if (it->first == path || it->first.rfind(prefix, 0) == 0) {
        const std::string removedPath = it->first;
        it = files.erase(it);
        if (removedPath == partialRemoveDirFailAfterPath) return false;
      } else {
        ++it;
      }
    }
    for (auto it = directories.begin(); it != directories.end();) {
      if (*it == path || it->rfind(prefix, 0) == 0) it = directories.erase(it);
      else ++it;
    }
    return true;
  }

  void reset() {
    files.clear();
    directories.clear();
    unreadablePaths.clear();
    readLimits.clear();
    writeLimits.clear();
    closeFailurePaths.clear();
    renameFailures.clear();
    checkedReadStatuses.clear();
    writeFailurePaths.clear();
    existsFalsePaths.clear();
    partialRemoveDirFailAfterPath.clear();
    failWrites = false;
    removeDirFails = false;
    removeDirCalls = 0;
  }

  std::map<std::string, std::string> files;
  std::set<std::string> directories;
  std::set<std::string> unreadablePaths;
  std::map<std::string, size_t> readLimits;
  std::map<std::string, size_t> writeLimits;
  std::set<std::string> closeFailurePaths;
  std::set<std::string> renameFailures;
  std::map<std::string, HalStorageReadStatus> checkedReadStatuses;
  std::set<std::string> writeFailurePaths;
  std::set<std::string> existsFalsePaths;
  std::string partialRemoveDirFailAfterPath;
  bool failWrites = false;
  bool removeDirFails = false;
  size_t removeDirCalls = 0;
};

inline TestStorage Storage;

inline int HalFile::read(uint8_t* data, const size_t length) {
  if (!open_) return -1;
  const auto it = Storage.files.find(path_);
  if (it == Storage.files.end() || position_ > it->second.size()) return -1;
  const size_t available = it->second.size() - position_;
  const auto limitIt = Storage.readLimits.find(path_);
  const size_t allowed = limitIt == Storage.readLimits.end() ? length : std::min(length, limitIt->second);
  const size_t count = std::min(available, allowed);
  for (size_t index = 0; index < count; ++index) data[index] = static_cast<uint8_t>(it->second[position_ + index]);
  position_ += count;
  return static_cast<int>(count);
}

inline size_t HalFile::write(const uint8_t* data, const size_t length) {
  if (!open_) return 0;
  auto it = Storage.files.find(path_);
  if (it == Storage.files.end()) return 0;
  const auto limitIt = Storage.writeLimits.find(path_);
  const size_t count = limitIt == Storage.writeLimits.end() ? length : std::min(length, limitIt->second);
  it->second.append(reinterpret_cast<const char*>(data), count);
  position_ += count;
  return count;
}

inline uint64_t HalFile::fileSize64() const {
  const auto it = Storage.files.find(path_);
  return it == Storage.files.end() ? 0 : it->second.size();
}
