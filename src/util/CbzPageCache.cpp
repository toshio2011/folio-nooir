#include "CbzPageCache.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace {

constexpr uint32_t MANIFEST_MAGIC = 0x43425A50;  // "CBZP"
constexpr uint16_t MANIFEST_VERSION = 1;
constexpr uint32_t MAX_MANIFEST_PAGES = 4096;
constexpr const char* MANIFEST_FILE = "/page_cache.bin";
constexpr const char* MANIFEST_TEMP_FILE = "/page_cache.bin.tmp";

bool writeBytes(HalFile& file, const void* data, const size_t length) {
  return file.write(reinterpret_cast<const uint8_t*>(data), length) == static_cast<int>(length);
}

bool readBytes(HalFile& file, void* data, const size_t length) {
  return file.read(reinterpret_cast<uint8_t*>(data), length) == static_cast<int>(length);
}

uint64_t getSourceSize(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("CBZCACHE", path, file)) return 0;
  const uint64_t size = file.size();
  file.close();
  return size;
}

}  // namespace

CbzPageCache::CbzPageCache(std::string cachePathValue, std::string sourcePathValue, const size_t pageCountValue)
    : cachePath(std::move(cachePathValue)), sourcePath(std::move(sourcePathValue)), pages(pageCountValue),
      valid(pageCountValue, 0) {}

std::string CbzPageCache::pagePath(const size_t page) const {
  char name[48] = {};
  std::snprintf(name, sizeof(name), "/page_%06lu.pxc", static_cast<unsigned long>(page));
  return cachePath + name;
}

std::string CbzPageCache::temporaryPagePath(const size_t page) const { return pagePath(page) + ".tmp"; }

void CbzPageCache::removePageFiles(const size_t count) const {
  const size_t bounded = std::min(count, static_cast<size_t>(MAX_MANIFEST_PAGES));
  for (size_t page = 0; page < bounded; ++page) {
    const std::string finalPath = pagePath(page);
    const std::string temporaryPath = temporaryPagePath(page);
    if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
    if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
  }
}

bool CbzPageCache::open() {
  sourceSize = getSourceSize(sourcePath);
  pages.assign(pages.size(), {});
  valid.assign(pages.size(), 0);

  const std::string manifestPath = cachePath + MANIFEST_FILE;
  HalFile file;
  bool manifestValid = false;
  uint32_t storedPageCount = 0;
  if (Storage.openFileForRead("CBZCACHE", manifestPath, file)) {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reserved = 0;
    uint64_t storedSourceSize = 0;
    if (readBytes(file, &magic, sizeof(magic)) && readBytes(file, &version, sizeof(version)) &&
        readBytes(file, &reserved, sizeof(reserved)) && readBytes(file, &storedSourceSize, sizeof(storedSourceSize)) &&
        readBytes(file, &storedPageCount, sizeof(storedPageCount))) {
      manifestValid = magic == MANIFEST_MAGIC && version == MANIFEST_VERSION &&
                      storedPageCount == pages.size() && storedSourceSize == sourceSize &&
                      storedPageCount <= MAX_MANIFEST_PAGES;
      if (manifestValid) {
        for (size_t page = 0; page < pages.size(); ++page) {
          PageInfo info;
          uint8_t pageValid = 0;
          uint8_t reservedEntry = 0;
          if (!readBytes(file, &info.sourceWidth, sizeof(info.sourceWidth)) ||
              !readBytes(file, &info.sourceHeight, sizeof(info.sourceHeight)) ||
              !readBytes(file, &info.targetWidth, sizeof(info.targetWidth)) ||
              !readBytes(file, &info.targetHeight, sizeof(info.targetHeight)) ||
              !readBytes(file, &info.viewMode, sizeof(info.viewMode)) ||
              !readBytes(file, &info.zoomLevel, sizeof(info.zoomLevel)) ||
              !readBytes(file, &pageValid, sizeof(pageValid)) ||
              !readBytes(file, &reservedEntry, sizeof(reservedEntry))) {
            manifestValid = false;
            break;
          }
          pages[page] = info;
          valid[page] = pageValid != 0 && Storage.exists(pagePath(page).c_str()) ? 1 : 0;
        }
      }
    }
    file.close();
  }

  if (!manifestValid) {
    const size_t oldCount = std::min(static_cast<size_t>(storedPageCount), static_cast<size_t>(MAX_MANIFEST_PAGES));
    removePageFiles(std::max(oldCount, pages.size()));
    if (!saveManifest()) {
      LOG_ERR("CBZCACHE", "Could not initialize persistent page cache: %s", cachePath.c_str());
      return false;
    }
    LOG_DBG("CBZCACHE", "persistent=reset pages=%lu sourceBytes=%llu", static_cast<unsigned long>(pages.size()),
            static_cast<unsigned long long>(sourceSize));
  } else {
    LOG_DBG("CBZCACHE", "persistent=loaded pages=%lu ready=%lu sourceBytes=%llu",
            static_cast<unsigned long>(pages.size()), static_cast<unsigned long>(completedCount()),
            static_cast<unsigned long long>(sourceSize));
  }
  return true;
}

bool CbzPageCache::saveManifest() const {
  const std::string temporaryPath = cachePath + MANIFEST_TEMP_FILE;
  const std::string finalPath = cachePath + MANIFEST_FILE;
  if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("CBZCACHE", temporaryPath, file)) return false;
  const uint32_t magic = MANIFEST_MAGIC;
  const uint16_t version = MANIFEST_VERSION;
  const uint16_t reserved = 0;
  const uint32_t pageCount = static_cast<uint32_t>(std::min(pages.size(), static_cast<size_t>(MAX_MANIFEST_PAGES)));
  bool ok = writeBytes(file, &magic, sizeof(magic)) && writeBytes(file, &version, sizeof(version)) &&
            writeBytes(file, &reserved, sizeof(reserved)) && writeBytes(file, &sourceSize, sizeof(sourceSize)) &&
            writeBytes(file, &pageCount, sizeof(pageCount));
  for (size_t page = 0; ok && page < pageCount; ++page) {
    const PageInfo& info = pages[page];
    const uint8_t pageValid = valid[page];
    const uint8_t reservedEntry = 0;
    ok = writeBytes(file, &info.sourceWidth, sizeof(info.sourceWidth)) &&
         writeBytes(file, &info.sourceHeight, sizeof(info.sourceHeight)) &&
         writeBytes(file, &info.targetWidth, sizeof(info.targetWidth)) &&
         writeBytes(file, &info.targetHeight, sizeof(info.targetHeight)) &&
         writeBytes(file, &info.viewMode, sizeof(info.viewMode)) &&
         writeBytes(file, &info.zoomLevel, sizeof(info.zoomLevel)) && writeBytes(file, &pageValid, sizeof(pageValid)) &&
         writeBytes(file, &reservedEntry, sizeof(reservedEntry));
  }
  file.flush();
  file.close();
  if (!ok) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(temporaryPath.c_str(), finalPath.c_str())) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  return true;
}

bool CbzPageCache::isReady(const size_t page, const uint8_t viewModeValue, const uint8_t zoomLevelValue,
                           PageInfo& out) const {
  if (page >= pages.size() || valid[page] == 0 || pages[page].viewMode != viewModeValue ||
      pages[page].zoomLevel != zoomLevelValue || pages[page].sourceWidth == 0 || pages[page].sourceHeight == 0 ||
      pages[page].targetWidth == 0 || pages[page].targetHeight == 0 || !Storage.exists(pagePath(page).c_str())) {
    return false;
  }
  out = pages[page];
  return true;
}

bool CbzPageCache::publish(const size_t page, const PageInfo& info, const std::string& temporaryPath) {
  if (page >= pages.size() || temporaryPath.empty() || !Storage.exists(temporaryPath.c_str())) return false;
  const std::string finalPath = pagePath(page);
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(temporaryPath.c_str(), finalPath.c_str())) return false;

  const PageInfo previous = pages[page];
  const uint8_t previousValid = valid[page];
  pages[page] = info;
  valid[page] = 1;
  if (!saveManifest()) {
    pages[page] = previous;
    valid[page] = previousValid;
    Storage.remove(finalPath.c_str());
    return false;
  }
  LOG_DBG("CBZCACHE", "persistent=publish page=%lu path=%s", static_cast<unsigned long>(page + 1), finalPath.c_str());
  return true;
}

void CbzPageCache::invalidate(const size_t page) {
  if (page >= pages.size()) return;
  const std::string finalPath = pagePath(page);
  const std::string temporaryPath = temporaryPagePath(page);
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
  valid[page] = 0;
  saveManifest();
}

void CbzPageCache::clear() {
  removePageFiles(pages.size());
  if (Storage.exists((cachePath + MANIFEST_FILE).c_str())) Storage.remove((cachePath + MANIFEST_FILE).c_str());
  if (Storage.exists((cachePath + MANIFEST_TEMP_FILE).c_str())) Storage.remove((cachePath + MANIFEST_TEMP_FILE).c_str());
  std::fill(pages.begin(), pages.end(), PageInfo{});
  std::fill(valid.begin(), valid.end(), 0);
  saveManifest();
}

size_t CbzPageCache::completedCount() const {
  return static_cast<size_t>(std::count(valid.begin(), valid.end(), static_cast<uint8_t>(1)));
}
