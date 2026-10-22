/**
 * Xtc.cpp
 *
 * Main XTC ebook class implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "Xtc.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "XtcCoverStream.h"
#include "XtcThumbnailStream.h"

namespace {
constexpr size_t kXtcStreamingChunkBytes = 512;
constexpr size_t kXtcCachePostWorkspaceReserve = 40u * 1024u;
constexpr size_t kXtcCacheLargestBlockMargin = 8u * 1024u;
constexpr uint16_t kMaxCoverWidth = 2048;
constexpr uint16_t kMaxCoverHeight = 3072;

void yieldDuringThumbnail(uint8_t& rowsSinceYield) {
  if (++rowsSinceYield < 8) return;
  rowsSinceYield = 0;
  vTaskDelay(1);
}

bool isCompleteThumbnail(const std::string& path, const int expectedWidth = 0, const int expectedHeight = 0,
                         const uint16_t expectedBpp = 1) {
  HalFile file;
  if (!Storage.openFileForRead("XTC", path, file)) return false;
  Bitmap bitmap(file);
  const bool parsed = bitmap.parseHeaders() == BmpReaderError::Ok;
  const uint64_t dataEnd = static_cast<uint64_t>(file.position()) +
                           static_cast<uint64_t>(bitmap.getRowBytes()) * bitmap.getHeight();
  const bool complete = parsed && bitmap.getWidth() > 0 && bitmap.getHeight() > 0 &&
                        (expectedWidth == 0 || bitmap.getWidth() == expectedWidth) &&
                        (expectedHeight == 0 || bitmap.getHeight() == expectedHeight) &&
                        (expectedBpp == 0 || bitmap.getBpp() == expectedBpp) &&
                        file.fileSize64() >= dataEnd;
  file.close();
  return complete;
}

bool commitBitmapCache(const std::string& temporaryPath, const std::string& outputPath,
                       const uint16_t width, const uint16_t height, const uint16_t bpp) {
  if (!isCompleteThumbnail(temporaryPath, width, height, bpp)) return false;
  if (Storage.exists(outputPath.c_str())) {
    if (isCompleteThumbnail(outputPath, width, height, bpp)) {
      Storage.remove(temporaryPath.c_str());
      return true;
    }
    if (!Storage.remove(outputPath.c_str())) {
      Storage.remove(temporaryPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(temporaryPath.c_str(), outputPath.c_str())) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  return isCompleteThumbnail(outputPath, width, height, bpp);
}

struct XtcOnePlaneThumbnailBuffers {
  uint16_t* xMap = nullptr;
  uint16_t* yMap = nullptr;
  uint16_t* xCounts = nullptr;
  uint16_t* yCounts = nullptr;
  uint32_t* whiteSums = nullptr;
  uint8_t* outputRow = nullptr;

  ~XtcOnePlaneThumbnailBuffers() {
    free(xMap);
    free(yMap);
    free(xCounts);
    free(yCounts);
    free(whiteSums);
    free(outputRow);
  }

  bool allocate(const xtc_thumb::OnePlaneWorkspaceLayout& layout) {
    xMap = static_cast<uint16_t*>(malloc(layout.xMapBytes));
    yMap = static_cast<uint16_t*>(malloc(layout.yMapBytes));
    xCounts = static_cast<uint16_t*>(malloc(layout.xCountBytes));
    yCounts = static_cast<uint16_t*>(malloc(layout.yCountBytes));
    whiteSums = static_cast<uint32_t*>(malloc(layout.sumsBytes));
    outputRow = static_cast<uint8_t*>(malloc(layout.outputRowBytes));
    return xMap && yMap && xCounts && yCounts && whiteSums && outputRow;
  }
};

struct XtcThumbnailBuffers {
  uint8_t* xAxis = nullptr;
  uint8_t* yAxis = nullptr;
  uint8_t* firstPlaneSums = nullptr;
  uint8_t* output = nullptr;
  uint32_t* secondPlaneColumnSums = nullptr;

  ~XtcThumbnailBuffers() {
    free(xAxis);
    free(yAxis);
    free(firstPlaneSums);
    free(output);
    free(secondPlaneColumnSums);
  }

  bool allocate(const xtc_thumb::WorkspaceLayout& layout) {
    xAxis = static_cast<uint8_t*>(malloc(layout.xAxisBytes));
    yAxis = static_cast<uint8_t*>(malloc(layout.yAxisBytes));
    firstPlaneSums = static_cast<uint8_t*>(malloc(layout.firstPlaneBytes));
    output = static_cast<uint8_t*>(malloc(layout.outputBytes));
    secondPlaneColumnSums = static_cast<uint32_t*>(malloc(layout.secondPlaneColumnBytes));
    return xAxis && yAxis && firstPlaneSums && output && secondPlaneColumnSums;
  }
};
}  // namespace

bool Xtc::load() {
  LOG_DBG("XTC", "Loading XTC: %s", filepath.c_str());

  // Initialize parser
  parser.reset(new xtc::XtcParser());

  // Open XTC file
  xtc::XtcError err = parser->open(filepath.c_str());
  if (err != xtc::XtcError::OK) {
    LOG_ERR("XTC", "Failed to load: %s", xtc::errorToString(err));
    parser.reset();
    return false;
  }

  loaded = true;
  LOG_DBG("XTC", "Loaded XTC: %s (%lu pages)", filepath.c_str(), parser->getPageCount());
  return true;
}

bool Xtc::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("XTC", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("XTC", "Failed to clear cache");
    return false;
  }

  LOG_DBG("XTC", "Cache cleared successfully");
  return true;
}

bool Xtc::clearCoverCache() const {
  if (!Storage.exists(cachePath.c_str())) return true;

  constexpr int kMaxCoverCacheEntries = 128;
  const std::vector<String> entries = Storage.listFiles(cachePath.c_str(), kMaxCoverCacheEntries);
  if (entries.size() >= static_cast<size_t>(kMaxCoverCacheEntries)) {
    LOG_ERR("XTC", "Refusing ambiguous cover-cache cleanup: too many cache entries");
    return false;
  }

  bool success = true;
  const std::string cachePrefix = cachePath + "/";
  const char* fixedOutputs[] = {"/cover.bmp", "/cover.bmp.tmp", "/cover-2bit.bmp", "/cover-2bit.bmp.tmp"};
  for (const char* suffix : fixedOutputs) {
    const std::string path = cachePath + suffix;
    if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
      success = false;
      LOG_ERR("XTC", "Could not remove generated cover cache file: %s", path.c_str());
    }
  }
  for (const String& entryString : entries) {
    const std::string entry(entryString.c_str());
    if (entry.empty()) continue;

    std::string relativeName;
    std::string filePath;
    if (entry.compare(0, cachePrefix.size(), cachePrefix) == 0) {
      relativeName = entry.substr(cachePrefix.size());
      filePath = entry;
    } else if (entry.find('/') == std::string::npos) {
      relativeName = entry;
      filePath = cachePrefix + entry;
    } else {
      const size_t slash = entry.find_last_of('/');
      if (slash == std::string::npos || entry.compare(0, slash, cachePath) != 0) continue;
      relativeName = entry.substr(slash + 1);
      filePath = entry;
    }

    // Restrict deletion to generated root-level cover/thumbnail outputs. The
    // cache directory also holds progress, bookmarks and future metadata.
    const bool cover = relativeName == "cover.bmp" || relativeName == "cover.bmp.tmp" ||
                       relativeName == "cover-2bit.bmp" || relativeName == "cover-2bit.bmp.tmp";
    const bool thumbnail = relativeName.rfind("thumb_", 0) == 0 &&
                           ((relativeName.size() >= 4 && relativeName.compare(relativeName.size() - 4, 4, ".bmp") == 0) ||
                            (relativeName.size() >= 8 && relativeName.compare(relativeName.size() - 8, 8, ".bmp.tmp") == 0));
    if ((cover || thumbnail) && Storage.exists(filePath.c_str()) && !Storage.remove(filePath.c_str())) {
      success = false;
      LOG_ERR("XTC", "Could not remove generated cover cache file: %s", filePath.c_str());
    }
  }
  return success;
}

void Xtc::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  // Create directories recursively
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      Storage.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  Storage.mkdir(cachePath.c_str());
}

std::string Xtc::getTitle() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get title from XTC metadata first
  std::string title = parser->getTitle();
  if (!title.empty()) {
    return title;
  }

  // Fallback: extract filename from path as title
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  }

  return filepath.substr(lastSlash, lastDot - lastSlash);
}

std::string Xtc::getAuthor() const {
  if (!loaded || !parser) {
    return "";
  }

  // Try to get author from XTC metadata
  return parser->getAuthor();
}

bool Xtc::hasChapters() const {
  if (!loaded || !parser) {
    return false;
  }
  return parser->hasChapters();
}

const std::vector<xtc::ChapterInfo>& Xtc::getChapters() {
  static const std::vector<xtc::ChapterInfo> kEmpty;
  if (!loaded || !parser) {
    return kEmpty;
  }
  return parser->getChapters();
}

std::string Xtc::getCoverBmpPath() const {
  return cachePath + (loaded && parser && parser->getBitDepth() == 2 ? "/cover-2bit.bmp" : "/cover.bmp");
}

bool Xtc::generateCoverBmp() const {
  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  const uint8_t bitDepth = parser->getBitDepth();
  const std::string outputPath = getCoverBmpPath();
  if (isCompleteThumbnail(outputPath, pageInfo.width, pageInfo.height, bitDepth)) return true;
  if ((bitDepth != 1 && bitDepth != 2) || pageInfo.width == 0 || pageInfo.height == 0 ||
      pageInfo.width > kMaxCoverWidth || pageInfo.height > kMaxCoverHeight) {
    LOG_DBG("XTC", "Skipping cover outside supported bounds (%ux%u, %u-bit)", pageInfo.width,
            pageInfo.height, bitDepth);
    return false;
  }

  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  size_t sourcePageBytes = 0;
  size_t planeBytes = 0;
  size_t columnBytes = 0;
  size_t bandPlaneBytes = 0;
  size_t bmpRowBytes = 0;
  size_t stripBytes = 0;
  size_t coverWorkspaceBytes = 0;
  if (bitDepth == 1) {
    const uint64_t sourceBytes = ((static_cast<uint64_t>(pageInfo.width) + 7u) / 8u) * pageInfo.height;
    if (sourceBytes > SIZE_MAX) return false;
    sourcePageBytes = static_cast<size_t>(sourceBytes);
    if (freeBytes < kXtcCachePostWorkspaceReserve + kXtcStreamingChunkBytes ||
        largestBlock < kXtcCacheLargestBlockMargin + kXtcStreamingChunkBytes) {
      LOG_DBG("XTC", "Deferring XTC cover: free=%lu largest=%lu", static_cast<unsigned long>(freeBytes),
              static_cast<unsigned long>(largestBlock));
      return false;
    }
  } else {
    if (!xtc_cover::makeTwoBitCoverWorkspace(pageInfo.width, pageInfo.height, planeBytes, columnBytes,
                                             bandPlaneBytes, bmpRowBytes, stripBytes, coverWorkspaceBytes)) {
      LOG_DBG("XTC", "Skipping XTCH cover with unsupported plane layout (%ux%u)", pageInfo.width,
              pageInfo.height);
      return false;
    }
    const size_t largestAllocation = std::max(bandPlaneBytes, stripBytes);
    if (freeBytes < coverWorkspaceBytes + kXtcCachePostWorkspaceReserve + kXtcStreamingChunkBytes ||
        largestBlock < largestAllocation + kXtcCacheLargestBlockMargin + kXtcStreamingChunkBytes) {
      LOG_DBG("XTC", "Deferring XTCH cover: workspace=%lu free=%lu largest=%lu",
              static_cast<unsigned long>(coverWorkspaceBytes), static_cast<unsigned long>(freeBytes),
              static_cast<unsigned long>(largestBlock));
      return false;
    }
  }

  setupCacheDir();
  const std::string temporaryPath = outputPath + ".tmp";
  if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
  HalFile coverBmp;
  if (!Storage.openFileForWrite("XTC", temporaryPath, coverBmp)) {
    LOG_DBG("XTC", "Failed to create temporary cover BMP file");
    return false;
  }

  bool written = true;
  uint8_t chunksSinceYield = 0;
  if (bitDepth == 2) {
    xtc_cover::TwoBitBmpHeader header;
    written = xtc_cover::makeTwoBitCoverWorkspace(pageInfo.width, pageInfo.height, planeBytes, columnBytes,
                                                   bandPlaneBytes, bmpRowBytes, stripBytes, coverWorkspaceBytes) &&
              xtc_cover::makeTwoBitBmpHeader(pageInfo.width, pageInfo.height, bmpRowBytes, header) &&
              coverBmp.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
    uint8_t* firstPlaneColumns = static_cast<uint8_t*>(malloc(bandPlaneBytes));
    uint8_t* outputRows = static_cast<uint8_t*>(malloc(stripBytes));
    if (!firstPlaneColumns || !outputRows) written = false;

    for (uint16_t group = 0; written && group < pageInfo.height / 8u; group += xtc_cover::kMaxBandGroups) {
      xtc_cover::TwoBitCoverStripAssembler assembler(pageInfo.width, pageInfo.height, planeBytes, columnBytes,
                                                      bmpRowBytes, stripBytes, firstPlaneColumns, outputRows);
      written = assembler.begin(group);
      chunksSinceYield = 0;
      const auto streamResult = written ? loadPageStreaming(
          0,
          [&assembler, &written, &chunksSinceYield](const uint8_t* data, const size_t size, const size_t offset) {
            if (written && !assembler.addChunk(data, size, offset)) written = false;
            if (++chunksSinceYield >= 64) {
              chunksSinceYield = 0;
              vTaskDelay(1);
            }
          },
          kXtcStreamingChunkBytes) : xtc::XtcError::READ_ERROR;
      if (streamResult != xtc::XtcError::OK || !written || !assembler.finish()) {
        written = false;
        break;
      }

      for (uint16_t row = 0; written && row < assembler.rowCount(); ++row) {
        const uint8_t* rowData = assembler.rows() + static_cast<size_t>(row) * bmpRowBytes;
        written = coverBmp.write(rowData, bmpRowBytes) == bmpRowBytes;
      }
      vTaskDelay(1);
    }
    free(firstPlaneColumns);
    free(outputRows);
  } else {
    BmpHeader header;
    createBmpHeader(&header, pageInfo.width, pageInfo.height, BmpRowOrder::TopDown);
    const size_t headerWritten = coverBmp.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    const size_t sourceRowBytes = (static_cast<size_t>(pageInfo.width) + 7u) / 8u;
    const size_t bmpRowBytes = ((static_cast<size_t>(pageInfo.width) + 31u) / 32u) * 4u;
    const size_t paddingBytes = bmpRowBytes - sourceRowBytes;
    const uint8_t padding[4] = {0, 0, 0, 0};
    size_t nextOffset = 0;
    const size_t expectedSourcePageBytes = sourcePageBytes;
    if (headerWritten != sizeof(header)) written = false;
    chunksSinceYield = 0;
    const auto streamResult = written ? loadPageStreaming(
        0,
        [&coverBmp, &written, &nextOffset, sourceRowBytes, paddingBytes, padding,
         expectedSourcePageBytes,
         &chunksSinceYield](const uint8_t* data, const size_t size, const size_t offset) {
          if (!written) return;
          if (!data || offset != nextOffset || nextOffset > expectedSourcePageBytes ||
              size > expectedSourcePageBytes - nextOffset) {
            written = false;
            return;
          }
          size_t consumed = 0;
          while (consumed < size && written) {
            const size_t withinRow = nextOffset % sourceRowBytes;
            const size_t count = std::min(size - consumed, sourceRowBytes - withinRow);
            written = coverBmp.write(data + consumed, count) == count;
            consumed += count;
            nextOffset += count;
            if (written && withinRow + count == sourceRowBytes && paddingBytes) {
              written = coverBmp.write(padding, paddingBytes) == paddingBytes;
            }
            if (written && nextOffset % sourceRowBytes == 0) yieldDuringThumbnail(chunksSinceYield);
          }
        },
        kXtcStreamingChunkBytes) : xtc::XtcError::READ_ERROR;
    if (bmpRowBytes < sourceRowBytes) written = false;
    if (streamResult != xtc::XtcError::OK || !written || nextOffset != sourcePageBytes) {
      written = false;
    }
  }

  coverBmp.flush();
  written = coverBmp.close() && written;
  if (!written) {
    Storage.remove(temporaryPath.c_str());
    LOG_ERR("XTC", "Failed while streaming cover BMP; existing cache retained");
    return false;
  }
  if (!commitBitmapCache(temporaryPath, outputPath, pageInfo.width, pageInfo.height, bitDepth)) {
    Storage.remove(temporaryPath.c_str());
    LOG_ERR("XTC", "Could not commit complete cover BMP; previous cache retained");
    return false;
  }

  LOG_DBG("XTC", "Generated cover BMP: %s", outputPath.c_str());
  return true;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Xtc::generateXtgThumbBmpStreaming(const uint16_t sourceWidth, const uint16_t sourceHeight,
                                       const uint16_t thumbWidth, const uint16_t thumbHeight,
                                       const uint32_t inverseScaleFp, const int requestedHeight) const {
  xtc_thumb::OnePlaneWorkspaceLayout layout;
  if (!xtc_thumb::makeOnePlaneWorkspaceLayout(sourceWidth, sourceHeight, thumbWidth, thumbHeight,
                                              inverseScaleFp, layout)) {
    LOG_DBG("XTC", "Skipping XTC thumbnail outside bounded workspace (%ux%u -> %ux%u)", sourceWidth,
            sourceHeight, thumbWidth, thumbHeight);
    return false;
  }

  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (!xtc_thumb::canAllocateOnePlaneWorkspace(layout, freeBytes, largestBlock)) {
    LOG_DBG("XTC", "Deferring XTC thumbnail: workspace=%lu free=%lu largest=%lu",
            static_cast<unsigned long>(layout.totalBytes), static_cast<unsigned long>(freeBytes),
            static_cast<unsigned long>(largestBlock));
    return false;
  }

  XtcOnePlaneThumbnailBuffers buffers;
  if (!buffers.allocate(layout)) {
    LOG_DBG("XTC", "Could not allocate bounded XTC thumbnail workspace (%lu bytes)",
            static_cast<unsigned long>(layout.totalBytes));
    return false;
  }
  xtc_thumb::OnePlaneReducer reducer(sourceWidth, sourceHeight, thumbWidth, thumbHeight, inverseScaleFp, layout,
                                     buffers.xMap, buffers.yMap, buffers.xCounts, buffers.yCounts,
                                     buffers.whiteSums, buffers.outputRow);
  if (!reducer.initialize()) return false;

  const std::string outputPath = getThumbBmpPath(requestedHeight);
  const std::string temporaryPath = outputPath + ".tmp";
  if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
  HalFile thumbnail;
  if (!Storage.openFileForWrite("XTC", temporaryPath, thumbnail)) {
    LOG_ERR("XTC", "Could not create temporary XTC thumbnail");
    return false;
  }

  BmpHeader header;
  createBmpHeader(&header, thumbWidth, thumbHeight, BmpRowOrder::TopDown);
  bool written = thumbnail.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  uint8_t chunksSinceYield = 0;
  const auto streamResult = written ? loadPageStreaming(
      0,
      [&reducer, &thumbnail, &written, &chunksSinceYield](const uint8_t* data, const size_t size,
                                                          const size_t offset) {
        if (!written) return;
        const auto emitRow = [&thumbnail](const uint8_t* row, const size_t rowBytes, const uint16_t) {
          return thumbnail.write(row, rowBytes) == rowBytes;
        };
        if (!reducer.addChunk(data, size, offset, emitRow)) {
          written = false;
          return;
        }
        yieldDuringThumbnail(chunksSinceYield);
      },
      kXtcStreamingChunkBytes) : xtc::XtcError::READ_ERROR;
  if (streamResult != xtc::XtcError::OK || !written) {
    written = false;
  } else {
    const auto emitRow = [&thumbnail](const uint8_t* row, const size_t rowBytes, const uint16_t) {
      return thumbnail.write(row, rowBytes) == rowBytes;
    };
    written = reducer.finish(emitRow);
  }

  thumbnail.flush();
  written = thumbnail.close() && written;
  if (!written || !commitBitmapCache(temporaryPath, outputPath, thumbWidth, thumbHeight, 1)) {
    Storage.remove(temporaryPath.c_str());
    LOG_ERR("XTC", "Could not commit complete XTC thumbnail; previous cache retained");
    return false;
  }
  return true;
}

bool Xtc::generateXthThumbBmpStreaming(const uint16_t sourceWidth, const uint16_t sourceHeight,
                                       const uint16_t thumbWidth, const uint16_t thumbHeight,
                                       const uint32_t inverseScaleFp) const {
  xtc_thumb::WorkspaceLayout layout;
  if (!xtc_thumb::makeWorkspaceLayout(sourceWidth, sourceHeight, thumbWidth, thumbHeight, inverseScaleFp, layout)) {
    LOG_DBG("XTC", "Skipping XTCH thumbnail outside bounded workspace (%ux%u -> %ux%u)", sourceWidth,
            sourceHeight, thumbWidth, thumbHeight);
    return false;
  }

  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (!xtc_thumb::canAllocateWorkspace(layout, freeBytes, largestBlock)) {
    LOG_DBG("XTC", "Deferring XTCH thumbnail: workspace=%lu free=%lu largest=%lu",
            static_cast<unsigned long>(layout.totalBytes), static_cast<unsigned long>(freeBytes),
            static_cast<unsigned long>(largestBlock));
    return false;
  }

  XtcThumbnailBuffers buffers;
  if (!buffers.allocate(layout)) {
    LOG_DBG("XTC", "Could not allocate bounded XTCH thumbnail workspace (%lu bytes)",
            static_cast<unsigned long>(layout.totalBytes));
    return false;
  }
  xtc_thumb::TwoPlaneReducer reducer(sourceWidth, sourceHeight, thumbWidth, thumbHeight, inverseScaleFp, layout,
                                     buffers.xAxis, buffers.yAxis, buffers.firstPlaneSums, buffers.output,
                                     buffers.secondPlaneColumnSums);
  if (!reducer.initialize()) return false;

  const size_t planeBytes = layout.planeBytes;
  uint8_t chunksSinceYield = 0;
  const auto firstPlaneResult = loadPageStreaming(
      0,
      [&reducer, planeBytes, &chunksSinceYield](const uint8_t* data, const size_t size, const size_t offset) {
        if (offset >= planeBytes || !reducer.healthy()) return;
        const size_t count = std::min(size, planeBytes - offset);
        if (!reducer.addFirstPlane(data, count, offset)) return;
        yieldDuringThumbnail(chunksSinceYield);
      },
      xtc_thumb::kStreamingChunkBytes);
  if (firstPlaneResult != xtc::XtcError::OK || !reducer.healthy()) {
    LOG_ERR("XTC", "Could not stream first XTCH thumbnail plane");
    return false;
  }

  chunksSinceYield = 0;
  const auto secondPlaneResult = loadPageStreaming(
      0,
      [&reducer, planeBytes, &chunksSinceYield](const uint8_t* data, const size_t size, const size_t offset) {
        const size_t end = offset + size;
        if (end <= planeBytes || offset >= planeBytes * 2u || !reducer.healthy()) return;
        const size_t begin = std::max(offset, planeBytes);
        const size_t clippedEnd = std::min(end, planeBytes * 2u);
        if (!reducer.addSecondPlane(data + (begin - offset), clippedEnd - begin, begin - planeBytes)) return;
        yieldDuringThumbnail(chunksSinceYield);
      },
      xtc_thumb::kStreamingChunkBytes);
  if (secondPlaneResult != xtc::XtcError::OK || !reducer.finishSecondPlane()) {
    LOG_ERR("XTC", "Could not stream second XTCH thumbnail plane");
    return false;
  }

  const std::string outputPath = getThumbBmpPath(thumbHeight);
  const std::string temporaryPath = outputPath + ".tmp";
  if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
  HalFile thumbnail;
  if (!Storage.openFileForWrite("XTC", temporaryPath, thumbnail)) {
    LOG_ERR("XTC", "Could not create temporary XTCH thumbnail");
    return false;
  }

  BmpHeader header;
  createBmpHeader(&header, thumbWidth, thumbHeight, BmpRowOrder::TopDown);
  bool written = thumbnail.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  for (uint16_t row = 0; written && row < thumbHeight; ++row) {
    const uint8_t* rowData = reducer.output() + static_cast<size_t>(row) * reducer.rowBytes();
    written = thumbnail.write(rowData, reducer.rowBytes()) == reducer.rowBytes();
    yieldDuringThumbnail(chunksSinceYield);
  }
  thumbnail.flush();
  written = thumbnail.close() && written;
  if (!written || !commitBitmapCache(temporaryPath, outputPath, thumbWidth, thumbHeight, 1)) {
    Storage.remove(temporaryPath.c_str());
    LOG_ERR("XTC", "Incomplete XTCH thumbnail; previous cache retained");
    return false;
  }
  return true;
}
bool Xtc::generateThumbBmp(int height) const {
  if (height <= 0) return false;
  // A complete cached image remains reusable; a short/incomplete file is not a cache hit.
  const std::string cachedThumbPath = getThumbBmpPath(height);
  if (isCompleteThumbnail(cachedThumbPath)) return true;

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate thumb BMP, file not loaded");
    return false;
  }

  if (parser->getPageCount() == 0) {
    LOG_ERR("XTC", "No pages in XTC file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  // Get first page info for cover
  xtc::PageInfo pageInfo;
  if (!parser->getPageInfo(0, pageInfo)) {
    LOG_DBG("XTC", "Failed to get first page info");
    return false;
  }

  // Get bit depth
  const uint8_t bitDepth = parser->getBitDepth();

  // Calculate target dimensions for thumbnail (fit within 240x400 Continue Reading card)
  const int THUMB_TARGET_WIDTH = std::max(1, height * 3 / 5);
  const int THUMB_TARGET_HEIGHT = height;

  if (pageInfo.width == 0 || pageInfo.height == 0) {
    LOG_DBG("XTC", "Skipping invalid thumbnail source dimensions (%ux%u)", pageInfo.width, pageInfo.height);
    return false;
  }

  // Calculate scale factor
  float scaleX = static_cast<float>(THUMB_TARGET_WIDTH) / pageInfo.width;
  float scaleY = static_cast<float>(THUMB_TARGET_HEIGHT) / pageInfo.height;
  float scale = (scaleX > scaleY) ? scaleX : scaleY;  // for cropping

  // Only scale down, never up
  if (scale >= 1.0f) {
    if (isCompleteThumbnail(cachedThumbPath, pageInfo.width, pageInfo.height, bitDepth)) return true;
    if (generateCoverBmp()) {
      HalFile src, dst;
      const std::string outputPath = getThumbBmpPath(height);
      const std::string temporaryPath = outputPath + ".tmp";
      if (Storage.exists(temporaryPath.c_str())) Storage.remove(temporaryPath.c_str());
      if (!Storage.openFileForRead("XTC", getCoverBmpPath(), src) ||
          !Storage.openFileForWrite("XTC", temporaryPath, dst)) {
        Storage.remove(temporaryPath.c_str());
        return false;
      }
      uint8_t buffer[kXtcStreamingChunkBytes];
      bool copied = true;
      while (copied && src.available()) {
        const size_t bytesRead = src.read(buffer, sizeof(buffer));
        if (bytesRead == 0 || dst.write(buffer, bytesRead) != bytesRead) copied = false;
      }
      src.close();
      dst.flush();
      copied = dst.close() && copied;
      if (!copied || !commitBitmapCache(temporaryPath, outputPath, pageInfo.width, pageInfo.height, bitDepth)) {
        Storage.remove(temporaryPath.c_str());
        return false;
      }
      LOG_DBG("XTC", "Copied cover to thumbnail without scaling");
      return true;
    }
    return false;
  }

  uint16_t thumbWidth = static_cast<uint16_t>(pageInfo.width * scale);
  uint16_t thumbHeight = static_cast<uint16_t>(pageInfo.height * scale);
  if (thumbWidth == 0 || thumbHeight == 0) return false;

  LOG_DBG("XTC", "Generating thumb BMP: %dx%d -> %dx%d (scale: %.3f)", pageInfo.width, pageInfo.height, thumbWidth,
          thumbHeight, scale);

  // Both source formats stream directly from SD. XTG uses one-bit input,
  // while XTCH keeps the existing bounded two-plane grayscale reducer.
  if (bitDepth == 2) {
    const uint32_t inverseScaleFp = static_cast<uint32_t>(65536.0f / scale);
    return generateXthThumbBmpStreaming(pageInfo.width, pageInfo.height, thumbWidth, thumbHeight, inverseScaleFp);
  }
  if (bitDepth != 1) return false;

  const uint32_t inverseScaleFp = static_cast<uint32_t>(65536.0f / scale);
  return generateXtgThumbBmpStreaming(pageInfo.width, pageInfo.height, thumbWidth, thumbHeight, inverseScaleFp, height);
}

uint32_t Xtc::getPageCount() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getPageCount();
}

uint16_t Xtc::getPageWidth() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getWidth();
}

uint16_t Xtc::getPageHeight() const {
  if (!loaded || !parser) {
    return 0;
  }
  return parser->getHeight();
}

uint8_t Xtc::getBitDepth() const {
  if (!loaded || !parser) {
    return 1;  // Default to 1-bit
  }
  return parser->getBitDepth();
}

size_t Xtc::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) const {
  if (!loaded || !parser) {
    return 0;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPage(pageIndex, buffer, bufferSize);
}

xtc::XtcError Xtc::loadPageStreaming(uint32_t pageIndex,
                                     std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                     size_t chunkSize) const {
  if (!loaded || !parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return const_cast<xtc::XtcParser*>(parser.get())->loadPageStreaming(pageIndex, callback, chunkSize);
}

uint8_t Xtc::calculateProgress(uint32_t currentPage) const {
  if (!loaded || !parser || parser->getPageCount() == 0) {
    return 0;
  }
  return static_cast<uint8_t>((currentPage + 1) * 100 / parser->getPageCount());
}

xtc::XtcError Xtc::getLastError() const {
  if (!parser) {
    return xtc::XtcError::FILE_NOT_FOUND;
  }
  return parser->getLastError();
}
