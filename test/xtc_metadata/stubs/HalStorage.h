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

inline void reset() {
  files.clear();
  openCalls = 0;
  openHandles = 0;
}
}  // namespace XtcMetadataTestStorage

class HalFile {
 public:
  size_t read(void* destination, const size_t requested) {
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

  void close() {
    if (!open_) return;
    open_ = false;
    --XtcMetadataTestStorage::openHandles;
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

  void addFile(const std::string& path, std::vector<uint8_t> bytes) {
    XtcMetadataTestStorage::files[path] = std::move(bytes);
  }
};

#define Storage HalStorage::getInstance()
