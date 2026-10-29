#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace XtcMetadataTestStorage {
inline std::unordered_map<std::string, std::vector<uint8_t>> files;
inline size_t openCalls = 0;
inline size_t openHandles = 0;
inline bool failNextRead = false;
inline uint16_t modifiedDate = 0x5A3Du;
inline uint16_t modifiedTime = 0x6B21u;

inline void reset() {
  files.clear();
  openCalls = 0;
  openHandles = 0;
  failNextRead = false;
  modifiedDate = 0x5A3Du;
  modifiedTime = 0x6B21u;
}
}  // namespace XtcMetadataTestStorage

class HalFile {
 public:
  size_t read(void* destination, const size_t requested) {
    if (XtcMetadataTestStorage::failNextRead) {
      XtcMetadataTestStorage::failNextRead = false;
      return 0;
    }
    if (!open_ || !bytes_ || position_ >= bytes_->size()) return 0;
    const size_t count = std::min(requested, bytes_->size() - position_);
    if (count != 0) std::memcpy(destination, bytes_->data() + position_, count);
    position_ += count;
    return count;
  }

  bool seek(const uint64_t position) { return seek64(position); }

  bool seek64(const uint64_t position) {
    if (!open_ || !bytes_ || position > bytes_->size()) return false;
    position_ = static_cast<size_t>(position);
    return true;
  }

  uint64_t fileSize64() const { return bytes_ ? bytes_->size() : 0; }

  bool getModifyDateTime(uint16_t* date, uint16_t* time) const {
    if (!open_ || !date || !time) return false;
    *date = XtcMetadataTestStorage::modifiedDate;
    *time = XtcMetadataTestStorage::modifiedTime;
    return true;
  }

  size_t write(const void* source, const size_t requested) {
    if (!open_ || !bytes_ || (!source && requested != 0)) return 0;
    if (position_ + requested > bytes_->size()) bytes_->resize(position_ + requested);
    if (requested != 0) std::memcpy(bytes_->data() + position_, source, requested);
    position_ += requested;
    return requested;
  }

  void flush() {}

  bool close() {
    if (!open_) return true;
    open_ = false;
    --XtcMetadataTestStorage::openHandles;
    return true;
  }

  bool isOpen() const { return open_; }

 private:
  friend class HalStorage;
  void bind(std::vector<uint8_t>* bytes) {
    close();
    bytes_ = bytes;
    position_ = 0;
    open_ = bytes_ != nullptr;
    if (open_) ++XtcMetadataTestStorage::openHandles;
  }

  std::vector<uint8_t>* bytes_ = nullptr;
  size_t position_ = 0;
  bool open_ = false;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    ++XtcMetadataTestStorage::openCalls;
    const auto it = XtcMetadataTestStorage::files.find(path);
    if (it == XtcMetadataTestStorage::files.end()) return false;
    file.bind(&it->second);
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    auto& bytes = XtcMetadataTestStorage::files[path];
    bytes.clear();
    file.bind(&bytes);
    return true;
  }

  bool exists(const char* path) const {
    return XtcMetadataTestStorage::files.find(path) != XtcMetadataTestStorage::files.end();
  }

  bool remove(const char* path) {
    return XtcMetadataTestStorage::files.erase(path) != 0;
  }

  bool rename(const char* oldPath, const char* newPath) {
    auto& files = XtcMetadataTestStorage::files;
    const auto old = files.find(oldPath);
    if (old == files.end() || files.find(newPath) != files.end()) return false;
    files.emplace(newPath, std::move(old->second));
    files.erase(old);
    return true;
  }

  void addFile(const std::string& path, std::vector<uint8_t> bytes) {
    XtcMetadataTestStorage::files[path] = std::move(bytes);
  }
};

#define Storage HalStorage::getInstance()
