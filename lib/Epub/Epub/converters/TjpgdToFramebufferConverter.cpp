#include "TjpgdToFramebufferConverter.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"

extern "C" {
#include <tjpgd.h>
}

namespace {

constexpr size_t TJPGD_WORK_BYTES = 8 * 1024;
constexpr size_t TJPGD_SKIP_BYTES = 512;
constexpr size_t MAX_FALLBACK_CACHE_BYTES = 96 * 1024;
constexpr size_t FALLBACK_CACHE_HEADROOM = 24 * 1024;

struct TjpgdFileContext {
  HalFile file;
  uint8_t skipBuffer[TJPGD_SKIP_BYTES]{};
  bool ioError = false;
};

struct TjpgdDecodeContext {
  TjpgdFileContext io;
  GfxRenderer* renderer = nullptr;
  int x = 0;
  int y = 0;
  int srcWidth = 0;
  int srcHeight = 0;
  int dstWidth = 0;
  int dstHeight = 0;
  bool dither = true;
  std::unique_ptr<uint8_t[]> cache;
  int cacheBytesPerRow = 0;
};

size_t tjpgdInput(JDEC* jd, uint8_t* buffer, size_t length) {
  auto* ctx = static_cast<TjpgdFileContext*>(jd->device);
  if (ctx == nullptr) return 0;

  size_t total = 0;
  while (total < length) {
    uint8_t* target = buffer != nullptr ? buffer + total : ctx->skipBuffer;
    const size_t want = buffer != nullptr
                            ? length - total
                            : std::min(length - total, sizeof(ctx->skipBuffer));
    const int read = ctx->file.read(target, want);
    if (read <= 0) {
      if (read < 0) ctx->ioError = true;
      break;
    }
    total += static_cast<size_t>(read);
  }
  return total;
}

inline void setCachePixel(TjpgdDecodeContext& ctx, const int x, const int y, const uint8_t value) {
  if (!ctx.cache || x < 0 || y < 0 || x >= ctx.dstWidth || y >= ctx.dstHeight) return;
  const int byteIndex = x >> 2;
  const int bitShift = 6 - (x & 3) * 2;
  uint8_t& cell = ctx.cache[static_cast<size_t>(y) * ctx.cacheBytesPerRow + byteIndex];
  cell = static_cast<uint8_t>((cell & ~(0x03u << bitShift)) | ((value & 0x03u) << bitShift));
}

int tjpgdOutput(JDEC* jd, void* bitmap, JRECT* rect) {
  auto* ctx = static_cast<TjpgdDecodeContext*>(jd->device);
  if (ctx == nullptr || ctx->renderer == nullptr || bitmap == nullptr || rect == nullptr) return 0;

  const auto* source = static_cast<const uint8_t*>(bitmap);
  const int rectWidth = static_cast<int>(rect->right - rect->left + 1);
  const int rectHeight = static_cast<int>(rect->bottom - rect->top + 1);
  DirectPixelWriter writer;
  writer.init(*ctx->renderer);

  for (int row = 0; row < rectHeight; row++) {
    const int sourceY = static_cast<int>(rect->top) + row;
    int destY = (sourceY * ctx->dstHeight) / std::max(1, ctx->srcHeight);
    if (destY >= ctx->dstHeight) destY = ctx->dstHeight - 1;
    if (destY < 0) continue;
    writer.beginRow(ctx->y + destY);

    for (int col = 0; col < rectWidth; col++) {
      const int sourceX = static_cast<int>(rect->left) + col;
      int destX = (sourceX * ctx->dstWidth) / std::max(1, ctx->srcWidth);
      if (destX >= ctx->dstWidth) destX = ctx->dstWidth - 1;
      if (destX < 0) continue;

      const uint8_t gray = source[row * rectWidth + col];
      const uint8_t level = ctx->dither
                                ? applyBayerDither4Level(gray, ctx->x + destX, ctx->y + destY)
                                : static_cast<uint8_t>(gray / 85u > 3u ? 3u : gray / 85u);
      writer.writePixel(ctx->x + destX, level);
      setCachePixel(*ctx, destX, destY, level);
    }
  }
  return 1;
}

bool readByte(HalFile& file, uint8_t& value) {
  const int result = file.read();
  if (result < 0) return false;
  value = static_cast<uint8_t>(result);
  return true;
}

bool readBigEndian16(HalFile& file, uint16_t& value) {
  uint8_t hi, lo;
  if (!readByte(file, hi) || !readByte(file, lo)) return false;
  value = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
  return true;
}

// JPEGDEC's fast AC table builder rejects codes longer than ten bits. Parse
// only the JPEG header and route those valid images to TJpgDec before JPEGDEC
// touches the entropy stream. This is intentionally conservative: malformed
// headers simply fall back to the existing decoder path.
bool hasLongAcHuffmanCode(const std::string& imagePath) {
  HalFile file;
  if (!Storage.openFileForRead("JPG", imagePath, file)) return false;

  uint8_t first, second;
  if (!readByte(file, first) || !readByte(file, second) || first != 0xFF || second != 0xD8) return false;

  for (size_t guard = 0; guard < 256 * 1024 && file.position() + 1 < file.size();) {
    uint8_t prefix;
    if (!readByte(file, prefix)) return false;
    if (prefix != 0xFF) continue;

    uint8_t marker;
    do {
      if (!readByte(file, marker)) return false;
    } while (marker == 0xFF);

    if (marker == 0xDA || marker == 0xD9) return false;  // SOS/EOI
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;

    uint16_t segmentLength;
    if (!readBigEndian16(file, segmentLength) || segmentLength < 2) return false;
    const size_t payloadLength = segmentLength - 2;
    const size_t payloadStart = file.position();

    if (marker == 0xC4) {
      size_t consumed = 0;
      while (consumed < payloadLength) {
        uint8_t tableInfo;
        if (!readByte(file, tableInfo)) return false;
        consumed++;
        if (payloadLength - consumed < 16) return false;

        uint8_t counts[16];
        size_t symbolCount = 0;
        for (int i = 0; i < 16; i++) {
          if (!readByte(file, counts[i])) return false;
          consumed++;
          symbolCount += counts[i];
        }

        if ((tableInfo & 0x10u) != 0) {
          for (int i = 10; i < 16; i++) {
            if (counts[i] != 0) return true;
          }
        }
        if (symbolCount > payloadLength - consumed) return false;
        if (!file.seek(file.position() + symbolCount)) return false;
        consumed += symbolCount;
      }
      if (consumed != payloadLength) return false;
    }

    if (file.position() != payloadStart + payloadLength && !file.seek(payloadStart + payloadLength)) {
      return false;
    }
    guard = file.position();
  }
  return false;
}

bool writePixelCache(const std::string& cachePath, TjpgdDecodeContext& ctx) {
  if (!ctx.cache) return true;
  HalFile file;
  if (!Storage.openFileForWrite("IMG", cachePath, file)) {
    LOG_ERR("IMG", "TJpgDec cache open failed: %s", cachePath.c_str());
    return false;
  }
  const uint16_t width = static_cast<uint16_t>(ctx.dstWidth);
  const uint16_t height = static_cast<uint16_t>(ctx.dstHeight);
  const size_t payloadBytes = static_cast<size_t>(ctx.cacheBytesPerRow) * ctx.dstHeight;
  if (file.write(&width, 2) != 2 || file.write(&height, 2) != 2 ||
      file.write(ctx.cache.get(), payloadBytes) != payloadBytes) {
    file.close();
    Storage.remove(cachePath.c_str());
    LOG_ERR("IMG", "TJpgDec cache write failed: %s", cachePath.c_str());
    return false;
  }
  file.close();
  LOG_DBG("IMG", "TJpgDec cache written: %s (%dx%d)", cachePath.c_str(), ctx.dstWidth, ctx.dstHeight);
  return true;
}

}  // namespace

bool TjpgdToFramebufferConverter::requiresFallback(const std::string& imagePath) {
  return hasLongAcHuffmanCode(imagePath);
}

bool TjpgdToFramebufferConverter::getDimensions(const std::string& imagePath, ImageDimensions& dims) const {
  TjpgdFileContext ctx;
  if (!Storage.openFileForRead("JPG", imagePath, ctx.file)) return false;

  auto work = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[TJPGD_WORK_BYTES]);
  if (!work) return false;
  JDEC decoder{};
  const JRESULT result = jd_prepare(&decoder, tjpgdInput, work.get(), TJPGD_WORK_BYTES, &ctx);
  if (result != JDR_OK) return false;

  dims.width = static_cast<int16_t>(decoder.width);
  dims.height = static_cast<int16_t>(decoder.height);
  return dims.width > 0 && dims.height > 0;
}

bool TjpgdToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                      const RenderConfig& config) {
  TjpgdDecodeContext ctx;
  ctx.renderer = &renderer;
  ctx.x = config.x;
  ctx.y = config.y;
  ctx.dstWidth = config.maxWidth;
  ctx.dstHeight = config.maxHeight;
  ctx.dither = config.useDithering;

  if (ctx.dstWidth <= 0 || ctx.dstHeight <= 0 || !Storage.openFileForRead("JPG", imagePath, ctx.io.file)) {
    return false;
  }

  auto work = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[TJPGD_WORK_BYTES]);
  if (!work) return false;
  JDEC decoder{};
  const JRESULT prepared = jd_prepare(&decoder, tjpgdInput, work.get(), TJPGD_WORK_BYTES, &ctx.io);
  if (prepared != JDR_OK) {
    LOG_ERR("JPG", "TJpgDec prepare failed (%d): %s", static_cast<int>(prepared), imagePath.c_str());
    return false;
  }

  ctx.srcWidth = decoder.width;
  ctx.srcHeight = decoder.height;
  if (!validateImageDimensions(ctx.srcWidth, ctx.srcHeight, "TJpgDec JPEG")) return false;

  if (!config.cachePath.empty()) {
    ctx.cacheBytesPerRow = (ctx.dstWidth + 3) / 4;
    const size_t cacheBytes = static_cast<size_t>(ctx.cacheBytesPerRow) * ctx.dstHeight;
    if (cacheBytes <= MAX_FALLBACK_CACHE_BYTES && ESP.getFreeHeap() > cacheBytes + FALLBACK_CACHE_HEADROOM) {
      ctx.cache.reset(new (std::nothrow) uint8_t[cacheBytes]);
      if (ctx.cache) memset(ctx.cache.get(), 0, cacheBytes);
    }
  }

  uint8_t scale = 0;
  while (scale < 3 && (ctx.srcWidth >> (scale + 1)) >= ctx.dstWidth &&
         (ctx.srcHeight >> (scale + 1)) >= ctx.dstHeight) {
    scale++;
  }
  // TJpgDec reports callback rectangles in the scaled output coordinate
  // space, not the original JPEG coordinate space.
  ctx.srcWidth = std::max(1, ctx.srcWidth >> scale);
  ctx.srcHeight = std::max(1, ctx.srcHeight >> scale);
  const JRESULT result = jd_decomp(&decoder, tjpgdOutput, scale);
  if (ctx.io.ioError || (result != JDR_OK && result != JDR_INTR)) {
    LOG_ERR("JPG", "TJpgDec decode failed (%d): %s", static_cast<int>(result), imagePath.c_str());
    if (!config.cachePath.empty()) Storage.remove(config.cachePath.c_str());
    return false;
  }

  return writePixelCache(config.cachePath, ctx);
}
