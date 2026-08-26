#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Persistent, per-page native CBZ render cache. The cache is deliberately
// independent from the reader's transient current/next files: completed pages
// survive reader exit, while a page being written is never published until its
// .pxc payload and manifest entry are complete.
class CbzPageCache final {
 public:
  struct PageInfo {
    uint16_t sourceWidth = 0;
    uint16_t sourceHeight = 0;
    uint16_t targetWidth = 0;
    uint16_t targetHeight = 0;
    uint8_t viewMode = 0;
    uint8_t zoomLevel = 0;
  };

  CbzPageCache(std::string cachePath, std::string sourcePath, size_t pageCount);

  bool open();
  bool isReady(size_t page, uint8_t viewMode, uint8_t zoomLevel, PageInfo& out) const;
  bool publish(size_t page, const PageInfo& info, const std::string& temporaryPath);
  void invalidate(size_t page);
  void clear();

  std::string pagePath(size_t page) const;
  std::string temporaryPagePath(size_t page) const;
  size_t completedCount() const;
  size_t pageCount() const { return pages.size(); }

 private:
  std::string cachePath;
  std::string sourcePath;
  uint64_t sourceSize = 0;
  std::vector<PageInfo> pages;
  std::vector<uint8_t> valid;

  bool saveManifest() const;
  void removePageFiles(size_t count) const;
};
