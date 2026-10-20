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
#include <cstdlib>
#include <cstring>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
void yieldDuringThumbnail(uint8_t& rowsSinceYield) {
  if (++rowsSinceYield < 8) return;
  rowsSinceYield = 0;
  vTaskDelay(1);
}

/**
 * Convert XTCH page zero into a small 1-bit shelf thumbnail WITHOUT loading
 * its two full 48KB bitplanes into one 96KB contiguous allocation.
 *
 * The on-disk XTCH bitplanes are reversed-column-major; output BMP is
 * row-major. Nearest-neighbour sampling uses two separate thumbnail-size
 * bitplanes (12KB each for a 240x400 X4 thumbnail), plus tiny coordinate
 * lookups and the parser's 1KB streaming buffer. Original grayscale/reader
 * pages and book metadata remain untouched. The existing area-average
 * thumbnail implementation remains the fallback for non-XTCH books.
 */
bool generateXtchThumbStreaming(const Xtc& book, const xtc::PageInfo& page,
                                const uint16_t dstWidth, const uint16_t dstHeight,
                                const float scale, const int requestedHeight) {
  if (page.width == 0 || page.height == 0 || dstWidth == 0 || dstHeight == 0 || scale <= 0.0f)
    return false;
  const size_t pixels = static_cast<size_t>(page.width) * page.height;
  const size_t sourcePlaneBytes = (pixels + 7u) / 8u;
  const size_t sourceBytes = sourcePlaneBytes * 2u;
  constexpr size_t MAX_SOURCE_BYTES = 128u * 1024u;
  constexpr size_t MAX_THUMB_PLANE_BYTES = 64u * 1024u;
  if (sourceBytes > MAX_SOURCE_BYTES) {
    LOG_DBG("XTC", "XTCH streaming thumbnail: invalid/oversized page (%ux%u)",
            page.width, page.height);
    return false;
  }

  const size_t stride = ((static_cast<size_t>(dstWidth) + 31u) / 32u) * 4u;
  const size_t destBytes = stride * dstHeight;
  if (destBytes == 0 || destBytes > MAX_THUMB_PLANE_BYTES) return false;

  using Bytes = std::unique_ptr<uint8_t, decltype(&std::free)>;
  using Lookup = std::unique_ptr<int16_t, decltype(&std::free)>;
  Bytes first(static_cast<uint8_t*>(std::malloc(destBytes)), &std::free);
  Bytes second(static_cast<uint8_t*>(std::malloc(destBytes)), &std::free);
  Lookup xToDst(static_cast<int16_t*>(std::malloc(static_cast<size_t>(page.width) * sizeof(int16_t))),
                &std::free);
  Lookup yToDst(static_cast<int16_t*>(std::malloc(static_cast<size_t>(page.height) * sizeof(int16_t))),
                &std::free);
  if (!first || !second || !xToDst || !yToDst) {
    LOG_DBG("XTC", "XTCH streaming thumb allocation failed (two %lu-byte planes)",
            static_cast<unsigned long>(destBytes));
    return false;
  }
  std::memset(first.get(), 0, destBytes);
  std::memset(second.get(), 0, destBytes);
  std::memset(xToDst.get(), 0xFF, static_cast<size_t>(page.width) * sizeof(int16_t));
  std::memset(yToDst.get(), 0xFF, static_cast<size_t>(page.height) * sizeof(int16_t));

  const uint32_t inverseScale = static_cast<uint32_t>(65536.0f / scale);
  for (uint16_t x = 0; x < dstWidth; ++x) {
    const size_t src = (static_cast<uint64_t>(x) * inverseScale) >> 16;
    xToDst.get()[std::min(src, static_cast<size_t>(page.width - 1))] = static_cast<int16_t>(x);
  }
  for (uint16_t y = 0; y < dstHeight; ++y) {
    const size_t src = (static_cast<uint64_t>(y) * inverseScale) >> 16;
    yToDst.get()[std::min(src, static_cast<size_t>(page.height - 1))] = static_cast<int16_t>(y);
  }

  const size_t bytesPerColumn = (static_cast<size_t>(page.height) + 7u) / 8u;
  const xtc::XtcError streamError = book.loadPageStreaming(
      0,
      [&](const uint8_t* data, const size_t length, const size_t offset) {
        for (size_t i = 0; i < length; ++i) {
          const size_t absolute = offset + i;
          if (absolute >= sourceBytes) break;
          const bool isSecondPlane = absolute >= sourcePlaneBytes;
          const size_t sourceIndex = isSecondPlane ? absolute - sourcePlaneBytes : absolute;
          const size_t column = sourceIndex / bytesPerColumn;
          if (column >= page.width) continue;
          const int16_t x = xToDst.get()[page.width - 1u - column];
          if (x < 0) continue;
          const size_t sourceRowBase = (sourceIndex % bytesPerColumn) * 8u;
          uint8_t* plane = isSecondPlane ? second.get() : first.get();
          const uint8_t sourceBits = data[i];
          for (size_t bit = 0; bit < 8u; ++bit) {
            const size_t sourceY = sourceRowBase + bit;
            if (sourceY >= page.height) break;
            const int16_t y = yToDst.get()[sourceY];
            if (y < 0 || (sourceBits & (0x80u >> bit)) == 0) continue;
            const size_t destByte = static_cast<size_t>(y) * stride + static_cast<size_t>(x) / 8u;
            plane[destByte] |= static_cast<uint8_t>(0x80u >> (x % 8));
          }
        }
      },
      1024u);
  if (streamError != xtc::XtcError::OK) {
    LOG_DBG("XTC", "XTCH streaming thumbnail read failed: %s", xtc::errorToString(streamError));
    return false;
  }

  // Quantize both sampled grayscale planes into the first buffer in place.
  // Each pixel's bits are read before only that same output bit is replaced.
  uint8_t rowsSinceYield = 0;
  for (uint16_t y = 0; y < dstHeight; ++y) {
    for (uint16_t x = 0; x < dstWidth; ++x) {
      const size_t index = static_cast<size_t>(y) * stride + (x / 8u);
      const uint8_t mask = static_cast<uint8_t>(0x80u >> (x % 8u));
      const uint8_t value = static_cast<uint8_t>(
          ((first.get()[index] & mask) ? 2 : 0) + ((second.get()[index] & mask) ? 1 : 0));
      const uint8_t gray = static_cast<uint8_t>((3 - value) * 85);
      uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
      hash = (hash ^ (hash >> 13)) * 1274126177u;
      const int noise = static_cast<int>(hash >> 24);
      const int threshold = 128 + ((noise - 128) / 2);
      if (gray >= threshold) first.get()[index] |= mask;
      else first.get()[index] &= static_cast<uint8_t>(~mask);
    }
    yieldDuringThumbnail(rowsSinceYield);
  }
  // The source is only opened for streaming, then closed by XtcParser's
  // existing on-demand implementation; no 96KB buffer is retained.
  const std::string destination = book.getThumbBmpPath(requestedHeight);
  HalFile output;
  if (!Storage.openFileForWrite("XTC", destination, output)) return false;
  BmpHeader header;
  createBmpHeader(&header, dstWidth, dstHeight, BmpRowOrder::TopDown);
  bool written = output.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  for (uint16_t y = 0; written && y < dstHeight; ++y) {
    written = output.write(first.get() + static_cast<size_t>(y) * stride, stride) == stride;
    yieldDuringThumbnail(rowsSinceYield);
  }
  output.close();
  if (!written) {
    Storage.remove(destination.c_str());
    return false;
  }
  LOG_DBG("XTC", "XTCH streaming thumb complete (%ux%u, two %lu-byte thumbnail planes): %s",
          dstWidth, dstHeight, static_cast<unsigned long>(destBytes), destination.c_str());
  return true;
}
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

std::string Xtc::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Xtc::generateCoverBmp() const {
  // Already generated
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  if (!loaded || !parser) {
    LOG_ERR("XTC", "Cannot generate cover BMP, file not loaded");
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

  // Allocate buffer for page data
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageInfo.width + 7) / 8) * pageInfo.height;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_ERR("XTC", "Failed to allocate page buffer (%lu bytes)", bitmapSize);
    return false;
  }

  // Load first page (cover)
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page");
    free(pageBuffer);
    return false;
  }

  // Create BMP file
  HalFile coverBmp;
  if (!Storage.openFileForWrite("XTC", getCoverBmpPath(), coverBmp)) {
    LOG_DBG("XTC", "Failed to create cover BMP file");
    free(pageBuffer);
    return false;
  }

  // Write 1-bit BMP header (top-down row order)
  BmpHeader bmpHeader;
  createBmpHeader(&bmpHeader, pageInfo.width, pageInfo.height, BmpRowOrder::TopDown);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeader), sizeof(bmpHeader));

  const uint32_t rowSize = ((pageInfo.width + 31) / 32) * 4;

  // Write bitmap data
  // BMP requires 4-byte row alignment
  const size_t dstRowSize = (pageInfo.width + 7) / 8;  // 1-bit destination row size

  if (bitDepth == 2) {
    // XTH 2-bit mode: Two bit planes, column-major order
    // - Columns scanned right to left (x = width-1 down to 0)
    // - 8 vertical pixels per byte (MSB = topmost pixel in group)
    // - First plane: Bit1, Second plane: Bit2
    // - Pixel value = (bit1 << 1) | bit2
    const size_t planeSize = (static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8;
    const uint8_t* plane1 = pageBuffer;                 // Bit1 plane
    const uint8_t* plane2 = pageBuffer + planeSize;     // Bit2 plane
    const size_t colBytes = (pageInfo.height + 7) / 8;  // Bytes per column

    // Allocate a row buffer for 1-bit output
    uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(dstRowSize));
    if (!rowBuffer) {
      free(pageBuffer);
      return false;
    }

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      memset(rowBuffer, 0xFF, dstRowSize);  // Start with all white

      for (uint16_t x = 0; x < pageInfo.width; x++) {
        // Column-major, right to left: column index = (width - 1 - x)
        const size_t colIndex = pageInfo.width - 1 - x;
        const size_t byteInCol = y / 8;
        const size_t bitInByte = 7 - (y % 8);  // MSB = topmost pixel

        const size_t byteOffset = colIndex * colBytes + byteInCol;
        const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
        const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
        const uint8_t pixelValue = (bit1 << 1) | bit2;

        // Threshold: 0=white (1); 1,2,3=black (0)
        if (pixelValue >= 1) {
          // Set bit to 0 (black) in BMP format
          const size_t dstByte = x / 8;
          const size_t dstBit = 7 - (x % 8);
          rowBuffer[dstByte] &= ~(1 << dstBit);
        }
      }

      // Write converted row
      coverBmp.write(rowBuffer, dstRowSize);

      // Pad to 4-byte boundary
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - dstRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }

    free(rowBuffer);
  } else {
    // 1-bit source: write directly with proper padding
    const size_t srcRowSize = (pageInfo.width + 7) / 8;

    for (uint16_t y = 0; y < pageInfo.height; y++) {
      // Write source row
      coverBmp.write(pageBuffer + y * srcRowSize, srcRowSize);

      // Pad to 4-byte boundary
      uint8_t padding[4] = {0, 0, 0, 0};
      size_t paddingSize = rowSize - srcRowSize;
      if (paddingSize > 0) {
        coverBmp.write(padding, paddingSize);
      }
    }
  }

  free(pageBuffer);

  LOG_DBG("XTC", "Generated cover BMP: %s", getCoverBmpPath().c_str());
  return true;
}

std::string Xtc::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Xtc::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Xtc::generateThumbBmp(int height) const {
  // Already generated
  if (Storage.exists(getThumbBmpPath(height).c_str())) {
    return true;
  }

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
  int THUMB_TARGET_WIDTH = height * 0.6;
  int THUMB_TARGET_HEIGHT = height;

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
    // Page is already small enough, just use cover.bmp
    // Copy cover.bmp to thumb.bmp
    if (generateCoverBmp()) {
      HalFile src, dst;
      if (Storage.openFileForRead("XTC", getCoverBmpPath(), src)) {
        if (Storage.openFileForWrite("XTC", getThumbBmpPath(height), dst)) {
          uint8_t buffer[512];
          while (src.available()) {
            size_t bytesRead = src.read(buffer, sizeof(buffer));
            dst.write(buffer, bytesRead);
          }
        }
      }
      LOG_DBG("XTC", "Copied cover to thumb (no scaling needed)");
      return Storage.exists(getThumbBmpPath(height).c_str());
    }
    return false;
  }

  uint16_t thumbWidth = static_cast<uint16_t>(pageInfo.width * scale);
  uint16_t thumbHeight = static_cast<uint16_t>(pageInfo.height * scale);

  LOG_DBG("XTC", "Generating thumb BMP: %dx%d -> %dx%d (scale: %.3f)", pageInfo.width, pageInfo.height, thumbWidth,
          thumbHeight, scale);

  // ESP32-C3 can struggle to find one contiguous 96KB XTCH page buffer when
  // the library/cover caches have fragmented RAM. The book reader already
  // renders grayscale from an SD stream; use the same low-memory approach
  // when generating XTCH shelf thumbnails. XTC thumbnails are unchanged.
  if (bitDepth == 2) {
    return generateXtchThumbStreaming(*this, pageInfo, thumbWidth, thumbHeight, scale, height);
  }

  // Allocate buffer for page data. Keep the multiplication bounded before it
  // happens: XTC dimensions are 16-bit and a malformed file could otherwise
  // wrap a 32-bit size calculation on the ESP32-C3.
  constexpr size_t MAX_THUMB_BITMAP_BYTES = 128u * 1024u;
  const size_t planes = bitDepth == 2 ? 2u : 1u;
  const size_t maxPixels = (MAX_THUMB_BITMAP_BYTES * 8u) / planes;
  if (static_cast<size_t>(pageInfo.width) > maxPixels / pageInfo.height) {
    LOG_DBG("XTC", "Skipping oversized thumbnail source (%ux%u)", pageInfo.width, pageInfo.height);
    return false;
  }

  size_t bitmapSize;
  if (bitDepth == 2) {
    bitmapSize = ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageInfo.width + 7) / 8) * pageInfo.height;
  }
  // Retrieve All must never allocate an unbounded page buffer for a malformed
  // or unusually large XTCH image. A normal X3/X4 page is well below this
  // limit; oversized pages can still be opened later through the reader's
  // normal streaming path or repaired manually.
  if (bitmapSize > MAX_THUMB_BITMAP_BYTES) {
    LOG_DBG("XTC", "Skipping oversized thumbnail source (%ux%u, %lu bytes)", pageInfo.width, pageInfo.height,
            static_cast<unsigned long>(bitmapSize));
    return false;
  }
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(bitmapSize));
  if (!pageBuffer) {
    LOG_ERR("XTC", "Failed to allocate page buffer (%lu bytes)", bitmapSize);
    return false;
  }

  // Load first page (cover)
  size_t bytesRead = const_cast<xtc::XtcParser*>(parser.get())->loadPage(0, pageBuffer, bitmapSize);
  if (bytesRead == 0) {
    LOG_ERR("XTC", "Failed to load cover page for thumb");
    free(pageBuffer);
    return false;
  }

  // Create thumbnail BMP file - use 1-bit format for fast home screen rendering (no gray passes)
  HalFile thumbBmp;
  if (!Storage.openFileForWrite("XTC", getThumbBmpPath(height), thumbBmp)) {
    LOG_DBG("XTC", "Failed to create thumb BMP file");
    free(pageBuffer);
    return false;
  }

  // Write 1-bit BMP header (top-down row order)
  BmpHeader bmpHeader;
  createBmpHeader(&bmpHeader, thumbWidth, thumbHeight, BmpRowOrder::TopDown);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeader), sizeof(bmpHeader));

  const uint32_t rowSize = (thumbWidth + 31) / 32 * 4;

  // Allocate row buffer for 1-bit output
  uint8_t* rowBuffer = static_cast<uint8_t*>(malloc(rowSize));
  if (!rowBuffer) {
    free(pageBuffer);
    return false;
  }

  // Fixed-point scale factor (16.16)
  uint32_t scaleInv_fp = static_cast<uint32_t>(65536.0f / scale);

  // Pre-calculate plane info for 2-bit mode
  const size_t planeSize = (bitDepth == 2) ? ((static_cast<size_t>(pageInfo.width) * pageInfo.height + 7) / 8) : 0;
  const uint8_t* plane1 = (bitDepth == 2) ? pageBuffer : nullptr;
  const uint8_t* plane2 = (bitDepth == 2) ? pageBuffer + planeSize : nullptr;
  const size_t colBytes = (bitDepth == 2) ? ((pageInfo.height + 7) / 8) : 0;
  const size_t srcRowBytes = (bitDepth == 1) ? ((pageInfo.width + 7) / 8) : 0;
  uint8_t rowsSinceYield = 0;

  for (uint16_t dstY = 0; dstY < thumbHeight; dstY++) {
    memset(rowBuffer, 0xFF, rowSize);  // Start with all white (bit 1)

    // Calculate source Y range with bounds checking
    uint32_t srcYStart = (static_cast<uint32_t>(dstY) * scaleInv_fp) >> 16;
    uint32_t srcYEnd = (static_cast<uint32_t>(dstY + 1) * scaleInv_fp) >> 16;
    if (srcYStart >= pageInfo.height) srcYStart = pageInfo.height - 1;
    if (srcYEnd > pageInfo.height) srcYEnd = pageInfo.height;
    if (srcYEnd <= srcYStart) srcYEnd = srcYStart + 1;
    if (srcYEnd > pageInfo.height) srcYEnd = pageInfo.height;

    for (uint16_t dstX = 0; dstX < thumbWidth; dstX++) {
      // Calculate source X range with bounds checking
      uint32_t srcXStart = (static_cast<uint32_t>(dstX) * scaleInv_fp) >> 16;
      uint32_t srcXEnd = (static_cast<uint32_t>(dstX + 1) * scaleInv_fp) >> 16;
      if (srcXStart >= pageInfo.width) srcXStart = pageInfo.width - 1;
      if (srcXEnd > pageInfo.width) srcXEnd = pageInfo.width;
      if (srcXEnd <= srcXStart) srcXEnd = srcXStart + 1;
      if (srcXEnd > pageInfo.width) srcXEnd = pageInfo.width;

      // Area averaging: sum grayscale values (0-255 range)
      uint32_t graySum = 0;
      uint32_t totalCount = 0;

      for (uint32_t srcY = srcYStart; srcY < srcYEnd && srcY < pageInfo.height; srcY++) {
        for (uint32_t srcX = srcXStart; srcX < srcXEnd && srcX < pageInfo.width; srcX++) {
          uint8_t grayValue = 255;  // Default: white

          if (bitDepth == 2) {
            // XTH 2-bit mode: pixel value 0-3
            // Bounds check for column index
            if (srcX < pageInfo.width) {
              const size_t colIndex = pageInfo.width - 1 - srcX;
              const size_t byteInCol = srcY / 8;
              const size_t bitInByte = 7 - (srcY % 8);
              const size_t byteOffset = colIndex * colBytes + byteInCol;
              // Bounds check for buffer access
              if (byteOffset < planeSize) {
                const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
                const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
                const uint8_t pixelValue = (bit1 << 1) | bit2;
                // Convert 2-bit (0-3) to grayscale: 0=black, 3=white
                // pixelValue: 0=white, 1=light gray, 2=dark gray, 3=black (XTC polarity)
                grayValue = (3 - pixelValue) * 85;  // 0->255, 1->170, 2->85, 3->0
              }
            }
          } else {
            // 1-bit mode
            const size_t byteIdx = srcY * srcRowBytes + srcX / 8;
            const size_t bitIdx = 7 - (srcX % 8);
            // Bounds check for buffer access
            if (byteIdx < bitmapSize) {
              const uint8_t pixelBit = (pageBuffer[byteIdx] >> bitIdx) & 1;
              // XTC 1-bit polarity: 0=black, 1=white (same as BMP palette)
              grayValue = pixelBit ? 255 : 0;
            }
          }

          graySum += grayValue;
          totalCount++;
        }
      }

      // Calculate average grayscale and quantize to 1-bit with noise dithering
      uint8_t avgGray = (totalCount > 0) ? static_cast<uint8_t>(graySum / totalCount) : 255;

      // Hash-based noise dithering for 1-bit output
      uint32_t hash = static_cast<uint32_t>(dstX) * 374761393u + static_cast<uint32_t>(dstY) * 668265263u;
      hash = (hash ^ (hash >> 13)) * 1274126177u;
      const int threshold = static_cast<int>(hash >> 24);           // 0-255
      const int adjustedThreshold = 128 + ((threshold - 128) / 2);  // Range: 64-192

      // Quantize to 1-bit: 0=black, 1=white
      uint8_t oneBit = (avgGray >= adjustedThreshold) ? 1 : 0;

      // Pack 1-bit value into row buffer (MSB first, 8 pixels per byte)
      const size_t byteIndex = dstX / 8;
      const size_t bitOffset = 7 - (dstX % 8);
      // Bounds check for row buffer access
      if (byteIndex < rowSize) {
        if (oneBit) {
          rowBuffer[byteIndex] |= (1 << bitOffset);  // Set bit for white
        } else {
          rowBuffer[byteIndex] &= ~(1 << bitOffset);  // Clear bit for black
        }
      }
    }

    // Write row (already padded to 4-byte boundary by rowSize)
    thumbBmp.write(rowBuffer, rowSize);
    yieldDuringThumbnail(rowsSinceYield);
  }

  free(rowBuffer);
  free(pageBuffer);

  LOG_DBG("XTC", "Generated thumb BMP (%dx%d): %s", thumbWidth, thumbHeight, getThumbBmpPath(height).c_str());
  return true;
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
