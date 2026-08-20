#include "TjpgdToFramebufferConverter.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "BoundedAreaResampler.h"
#include "PixelCache.h"
#include "DitherUtils.h"

extern "C" {
#include <tjpgd.h>
}

namespace {

constexpr size_t TJPGD_WORK_BYTES = 8 * 1024;
constexpr size_t TJPGD_SKIP_BYTES = 512;
constexpr size_t MAX_AREA_BAND_BYTES = 16 * 1024;
constexpr size_t MIN_AREA_HEAP_HEADROOM = 24 * 1024;
constexpr size_t GRAY_BMP_PIXEL_OFFSET = 14 + 40 + 256 * 4;

#if JD_FORMAT == 0
constexpr const char* TJPGD_FORMAT_NAME = "RGB888";
constexpr int TJPGD_BYTES_PER_PIXEL = 3;
#elif JD_FORMAT == 1
constexpr const char* TJPGD_FORMAT_NAME = "RGB565";
constexpr int TJPGD_BYTES_PER_PIXEL = 2;
#elif JD_FORMAT == 2
constexpr const char* TJPGD_FORMAT_NAME = "gray8";
constexpr int TJPGD_BYTES_PER_PIXEL = 1;
#else
constexpr const char* TJPGD_FORMAT_NAME = "unknown";
constexpr int TJPGD_BYTES_PER_PIXEL = 0;
#endif

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
  const RenderConfig* config = nullptr;
  PixelCache cache;
  bool caching = false;
  std::unique_ptr<BoundedAreaResampler> areaResampler;
  std::unique_ptr<uint8_t[]> areaSourceBand;
  int areaBandStartY = -1;
  int areaBandRows = 0;
  int areaBandCapacity = 0;
  bool areaResampling = false;
  bool directBw = false;
  uint32_t callbackCount = 0;
  int minRectWidth = INT_MAX;
  int minRectHeight = INT_MAX;
  int maxRectWidth = 0;
  int maxRectHeight = 0;
  int shapeWidth[4]{};
  int shapeHeight[4]{};
  uint32_t shapeCount[4]{};
  uint64_t callbackArea = 0;
  uint32_t boundsErrors = 0;
  uint32_t orderErrors = 0;
  uint32_t gapCount = 0;
  uint32_t overlapCount = 0;
  int lastRectTop = -1;
  int lastRectBottom = -1;
  int lastRectRight = -1;
  HalFile cropFile;
  bool cropOpen = false;
  int cropX = 0;
  int cropY = 0;
  int cropWidth = 0;
  int cropHeight = 0;
  size_t cropRowBytes = 0;
  std::string cropPath;
};

void writeLe16(uint8_t* const p, const uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFFu);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void writeLe32(uint8_t* const p, const uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFFu);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

bool beginGrayCrop(TjpgdDecodeContext& ctx) {
  if (!ctx.config || !ctx.config->cbzDumpTjpgGrayCrop) return false;
  if (ctx.config->cbzDumpCropWidth <= 0 || ctx.config->cbzDumpCropHeight <= 0 ||
      ctx.config->cbzDumpCropWidth > 240 || ctx.config->cbzDumpCropHeight > 240 ||
      ctx.config->cbzDumpCropX < 0 || ctx.config->cbzDumpCropY < 0 ||
      ctx.config->cbzDumpCropX + ctx.config->cbzDumpCropWidth > ctx.srcWidth ||
      ctx.config->cbzDumpCropY + ctx.config->cbzDumpCropHeight > ctx.srcHeight ||
      ctx.config->cbzDumpCropPath.empty()) {
    LOG_ERR("TJPGDUMP", "invalid crop request source=%dx%d rect=%d,%d %dx%d",
            ctx.srcWidth, ctx.srcHeight, ctx.config->cbzDumpCropX, ctx.config->cbzDumpCropY,
            ctx.config->cbzDumpCropWidth, ctx.config->cbzDumpCropHeight);
    return false;
  }

  ctx.cropX = ctx.config->cbzDumpCropX;
  ctx.cropY = ctx.config->cbzDumpCropY;
  ctx.cropWidth = ctx.config->cbzDumpCropWidth;
  ctx.cropHeight = ctx.config->cbzDumpCropHeight;
  ctx.cropRowBytes = (static_cast<size_t>(ctx.cropWidth) + 3u) & ~static_cast<size_t>(3u);
  ctx.cropPath = ctx.config->cbzDumpCropPath;
  if (!Storage.openFileForWrite("TJPG", ctx.cropPath, ctx.cropFile)) {
    LOG_ERR("TJPGDUMP", "open failed file=\"%s\"", ctx.cropPath.c_str());
    ctx.cropPath.clear();
    return false;
  }
  const auto cropSetupFailed = [&ctx] {
    ctx.cropFile.close();
    if (!ctx.cropPath.empty()) Storage.remove(ctx.cropPath.c_str());
    ctx.cropPath.clear();
    return false;
  };

  const uint32_t imageBytes = static_cast<uint32_t>(ctx.cropRowBytes * static_cast<size_t>(ctx.cropHeight));
  const uint32_t fileBytes = static_cast<uint32_t>(GRAY_BMP_PIXEL_OFFSET + imageBytes);
  uint8_t header[54]{};
  header[0] = 'B';
  header[1] = 'M';
  writeLe32(header + 2, fileBytes);
  writeLe32(header + 10, static_cast<uint32_t>(GRAY_BMP_PIXEL_OFFSET));
  writeLe32(header + 14, 40);
  writeLe32(header + 18, static_cast<uint32_t>(ctx.cropWidth));
  writeLe32(header + 22, static_cast<uint32_t>(ctx.cropHeight));
  writeLe16(header + 26, 1);
  writeLe16(header + 28, 8);
  writeLe32(header + 34, imageBytes);
  if (ctx.cropFile.write(header, sizeof(header)) != sizeof(header)) return cropSetupFailed();

  uint8_t palette[4]{};
  for (int i = 0; i < 256; ++i) {
    palette[0] = static_cast<uint8_t>(i);
    palette[1] = static_cast<uint8_t>(i);
    palette[2] = static_cast<uint8_t>(i);
    palette[3] = 0;
    if (ctx.cropFile.write(palette, sizeof(palette)) != sizeof(palette)) return cropSetupFailed();
  }

  // Initialize the bounded crop to white, including row padding. Callback
  // rectangles then overwrite only the source pixels they cover.
  uint8_t white[32];
  std::memset(white, 0xFF, sizeof(white));
  for (int row = 0; row < ctx.cropHeight; ++row) {
    const size_t rowOffset = GRAY_BMP_PIXEL_OFFSET + static_cast<size_t>(row) * ctx.cropRowBytes;
    if (!ctx.cropFile.seek(rowOffset)) return cropSetupFailed();
    size_t remaining = ctx.cropRowBytes;
    while (remaining > 0) {
      const size_t chunk = std::min(remaining, sizeof(white));
      if (ctx.cropFile.write(white, chunk) != chunk) return cropSetupFailed();
      remaining -= chunk;
    }
  }
  ctx.cropOpen = true;
  LOG_DBG("TJPGDUMP", "sourceRect=%d,%d,%d,%d file=\"%s\" format=gray8 stage=post_tjpg_pre_display",
          ctx.cropX, ctx.cropY, ctx.cropWidth, ctx.cropHeight, ctx.cropPath.c_str());
  return true;
}

void closeGrayCrop(TjpgdDecodeContext& ctx, const bool keepFile) {
  if (ctx.cropOpen) ctx.cropFile.close();
  ctx.cropOpen = false;
  if (!keepFile && !ctx.cropPath.empty()) {
    Storage.remove(ctx.cropPath.c_str());
    ctx.cropPath.clear();
  }
}

void writeGrayCropRow(TjpgdDecodeContext& ctx, const int sourceY, const uint8_t* sourceRow,
                      const int rectLeft, const int rectWidth) {
  if (!ctx.cropOpen || !sourceRow || sourceY < ctx.cropY || sourceY >= ctx.cropY + ctx.cropHeight) return;
  const int firstX = std::max(ctx.cropX, rectLeft);
  const int lastX = std::min(ctx.cropX + ctx.cropWidth - 1, rectLeft + rectWidth - 1);
  if (firstX > lastX) return;
  const int count = lastX - firstX + 1;
  const size_t row = static_cast<size_t>(ctx.cropHeight - 1 - (sourceY - ctx.cropY));
  const size_t offset = GRAY_BMP_PIXEL_OFFSET + row * ctx.cropRowBytes + static_cast<size_t>(firstX - ctx.cropX);
  if (!ctx.cropFile.seek(offset) ||
      ctx.cropFile.write(sourceRow + (firstX - rectLeft), static_cast<size_t>(count)) != static_cast<size_t>(count)) {
    closeGrayCrop(ctx, false);
  }
}

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

uint8_t quantizeTjpgdPixel(const TjpgdDecodeContext& ctx, const uint8_t gray, const int x, const int y) {
  if (!ctx.config || !ctx.config->useDithering) {
    // CBZ's bounded area reducer already averaged the source samples. Keep
    // that tone instead of reintroducing a pattern; other no-dither callers
    // retain their legacy floor quantization.
    return quantizeDirect4Level(gray, ctx.config && ctx.config->cbzQualityMode,
                                ctx.config && ctx.config->cbzBwDiagnostic);
  }
  return ctx.config->cbzQualityMode ? applyCbzDither4Level(gray, x, y) : applyBayerDither4Level(gray, x, y);
}

void writeTjpgdAreaRow(TjpgdDecodeContext& ctx, const int destinationY, const uint8_t* grayRow) {
  if (!grayRow || destinationY < 0 || destinationY >= ctx.dstHeight) return;
  const int outY = ctx.y + destinationY;
  const bool cacheImageLocal = ctx.config && ctx.config->cbzQualityMode;
  if (!cacheImageLocal && (outY < 0 || outY >= ctx.renderer->getScreenHeight())) return;
  DirectPixelWriter writer;
  writer.init(*ctx.renderer);
  writer.beginRow(outY);

  DirectCacheWriter cacheWriter{};
  bool caching = ctx.caching;
  int cacheOriginY = 0;
  if (caching) {
    if (!ctx.cache.advanceTo(destinationY)) {
      LOG_ERR("IMG", "TJpgDec image resampler pixel cache stream failed at row %d", destinationY);
      ctx.cache.abort();
      ctx.caching = false;
      caching = false;
    } else {
      cacheWriter.init(ctx.cache.buffer, ctx.cache.bytesPerRow, ctx.cache.bandRows,
                       cacheImageLocal ? 0 : ctx.cache.originX);
      cacheOriginY = cacheImageLocal ? ctx.cache.bandStart : ctx.y + ctx.cache.bandStart;
      cacheWriter.beginRow(cacheImageLocal ? destinationY : outY, cacheOriginY);
    }
  }

  for (int destinationX = 0; destinationX < ctx.dstWidth; ++destinationX) {
    const int outX = ctx.x + destinationX;
    if (!cacheImageLocal && (outX < 0 || outX >= ctx.renderer->getScreenWidth())) continue;
    const uint8_t gray = grayRow[destinationX];
    const uint8_t level = quantizeTjpgdPixel(ctx, gray, outX, outY);
    if (ctx.config) recordImageQualityPixel(*ctx.config, outX, outY, gray, level);
    writer.writePixel(outX, level);
    if (caching) cacheWriter.writePixel(cacheImageLocal ? destinationX : outX, level);
  }
}

void writePendingTjpgdAreaRows(TjpgdDecodeContext& ctx) {
  while (ctx.areaResampler && ctx.areaResampler->hasPendingRow()) {
    writeTjpgdAreaRow(ctx, ctx.areaResampler->pendingRowIndex(), ctx.areaResampler->pendingRowData());
    ctx.areaResampler->consumePendingRow();
  }
}

bool flushTjpgdAreaBand(TjpgdDecodeContext& ctx) {
  if (!ctx.areaResampling || ctx.areaBandStartY < 0 || ctx.areaBandRows <= 0) return true;
  for (int row = 0; row < ctx.areaBandRows; ++row) {
    const int sourceY = ctx.areaBandStartY + row;
    const uint8_t* sourceRow = ctx.areaSourceBand.get() + static_cast<size_t>(row) * ctx.srcWidth;
    const uint32_t resampleStartedUs = ctx.config && ctx.config->diagnostics ? micros() : 0;
    const bool ok = ctx.areaResampler->addSourceRowSegment(sourceY, 0, sourceRow, ctx.srcWidth);
    if (ctx.config && ctx.config->diagnostics) {
      ctx.config->diagnostics->resampleUs += micros() - resampleStartedUs;
    }
    if (!ok) return false;
    writePendingTjpgdAreaRows(ctx);
  }
  ctx.areaBandStartY = -1;
  ctx.areaBandRows = 0;
  return true;
}

bool appendTjpgdAreaBlock(TjpgdDecodeContext& ctx, const uint8_t* source, const JRECT& rect) {
  const int rectWidth = static_cast<int>(rect.right - rect.left + 1);
  const int rectHeight = static_cast<int>(rect.bottom - rect.top + 1);
  if (!source || rectWidth <= 0 || rectHeight <= 0 || rect.left < 0 || rect.top < 0 ||
      rect.left + rectWidth > ctx.srcWidth || rect.top + rectHeight > ctx.srcHeight) {
    return false;
  }

  if (ctx.areaBandStartY < 0) {
    ctx.areaBandStartY = rect.top;
    ctx.areaBandRows = 0;
  } else if (rect.top != ctx.areaBandStartY) {
    if (!flushTjpgdAreaBand(ctx)) return false;
    ctx.areaBandStartY = rect.top;
  }

  const int rowOffset = rect.top - ctx.areaBandStartY;
  if (rowOffset < 0 || rowOffset + rectHeight > ctx.areaBandCapacity) return false;
  for (int row = 0; row < rectHeight; ++row) {
    std::memcpy(ctx.areaSourceBand.get() + static_cast<size_t>(rowOffset + row) * ctx.srcWidth + rect.left,
                source + static_cast<size_t>(row) * rectWidth, static_cast<size_t>(rectWidth));
  }
  ctx.areaBandRows = std::max(ctx.areaBandRows, rowOffset + rectHeight);

  // TJpgDec walks MCUs left-to-right within each vertical band. The rightmost
  // block completes every source row in this band, allowing bounded streaming
  // into the row resampler without retaining the decoded image.
  if (rect.right + 1 >= ctx.srcWidth) return flushTjpgdAreaBand(ctx);
  return true;
}

int tjpgdOutput(JDEC* jd, void* bitmap, JRECT* rect) {
  auto* ctx = static_cast<TjpgdDecodeContext*>(jd->device);
  if (ctx == nullptr || ctx->renderer == nullptr || bitmap == nullptr || rect == nullptr) return 0;
  ScopedImagePixelTimer pixelTimer(ctx->config ? ctx->config->diagnostics : nullptr);

  const auto* source = static_cast<const uint8_t*>(bitmap);
  const int rectWidth = static_cast<int>(rect->right - rect->left + 1);
  const int rectHeight = static_cast<int>(rect->bottom - rect->top + 1);
  if (rectWidth <= 0 || rectHeight <= 0) return 0;
  const bool rectInBounds = rect->left >= 0 && rect->top >= 0 &&
                            rect->right < ctx->srcWidth && rect->bottom < ctx->srcHeight;
  ++ctx->callbackCount;
  ctx->callbackArea += static_cast<uint64_t>(rectWidth) * static_cast<uint64_t>(rectHeight);
  if (!rectInBounds) {
    ++ctx->boundsErrors;
    return 0;
  }
  if (ctx->lastRectTop >= 0) {
    if (rect->top == ctx->lastRectTop) {
      if (rect->left <= ctx->lastRectRight) {
        ++ctx->overlapCount;
      } else if (rect->left != ctx->lastRectRight + 1) {
        ++ctx->gapCount;
      }
    } else if (rect->top > ctx->lastRectBottom) {
      if (rect->top != ctx->lastRectBottom + 1 || rect->left != 0) ++ctx->gapCount;
    } else {
      ++ctx->orderErrors;
    }
  }
  ctx->lastRectTop = rect->top;
  ctx->lastRectBottom = rect->bottom;
  ctx->lastRectRight = rect->right;
  ctx->minRectWidth = std::min(ctx->minRectWidth, rectWidth);
  ctx->minRectHeight = std::min(ctx->minRectHeight, rectHeight);
  ctx->maxRectWidth = std::max(ctx->maxRectWidth, rectWidth);
  ctx->maxRectHeight = std::max(ctx->maxRectHeight, rectHeight);
  for (size_t i = 0; i < 4; ++i) {
    if (ctx->shapeCount[i] != 0 && ctx->shapeWidth[i] == rectWidth && ctx->shapeHeight[i] == rectHeight) {
      ++ctx->shapeCount[i];
      break;
    }
    if (ctx->shapeCount[i] == 0) {
      ctx->shapeWidth[i] = rectWidth;
      ctx->shapeHeight[i] = rectHeight;
      ctx->shapeCount[i] = 1;
      break;
    }
  }
  if (ctx->areaResampling) return appendTjpgdAreaBlock(*ctx, source, *rect) ? 1 : 0;

  DirectPixelWriter writer;
  writer.init(*ctx->renderer);
  DirectCacheWriter cacheWriter{};
  const bool cacheImageLocal = ctx->config && ctx->config->cbzQualityMode;
  int cacheOriginY = 0;
  if (ctx->caching) {
    int firstDestY = (static_cast<int>(rect->top) * ctx->dstHeight) / std::max(1, ctx->srcHeight);
    if (firstDestY >= ctx->dstHeight) firstDestY = ctx->dstHeight - 1;
    if (firstDestY < 0) firstDestY = 0;
    if (!ctx->cache.advanceTo(firstDestY)) {
      LOG_ERR("IMG", "TJpgDec pixel cache stream failed at block row %d", firstDestY);
      ctx->cache.abort();
      ctx->caching = false;
      return 0;
    }
    cacheWriter.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows,
                     cacheImageLocal ? 0 : ctx->cache.originX);
    cacheOriginY = cacheImageLocal ? ctx->cache.bandStart : ctx->cache.originY + ctx->cache.bandStart;
  }

  for (int row = 0; row < rectHeight; row++) {
    const int sourceY = static_cast<int>(rect->top) + row;
    int destY = (sourceY * ctx->dstHeight) / std::max(1, ctx->srcHeight);
    if (destY >= ctx->dstHeight) destY = ctx->dstHeight - 1;
    if (destY < 0) continue;
    writer.beginRow(ctx->y + destY);
    if (ctx->caching) {
      cacheWriter.beginRow(cacheImageLocal ? destY : ctx->y + destY, cacheOriginY);
    }
    if (ctx->directBw) {
      writeGrayCropRow(*ctx, sourceY, source + static_cast<size_t>(row) * rectWidth, rect->left,
                       rectWidth);
    }

    for (int col = 0; col < rectWidth; col++) {
      const int sourceX = static_cast<int>(rect->left) + col;
      int destX = (sourceX * ctx->dstWidth) / std::max(1, ctx->srcWidth);
      if (destX >= ctx->dstWidth) destX = ctx->dstWidth - 1;
      if (destX < 0) continue;

      const uint8_t gray = source[row * rectWidth + col];
      if (ctx->directBw) {
        const int logicalX = ctx->x + destX;
        const int logicalY = ctx->y + destY;
        const RenderConfig& directConfig = *ctx->config;
        if (logicalX < directConfig.cbzDirectViewportX ||
            logicalX >= directConfig.cbzDirectViewportX + directConfig.cbzDirectViewportWidth ||
            logicalY < directConfig.cbzDirectViewportY ||
            logicalY >= directConfig.cbzDirectViewportY + directConfig.cbzDirectViewportHeight) {
          continue;
        }
      }
      const uint8_t level = quantizeTjpgdPixel(*ctx, gray, ctx->x + destX, ctx->y + destY);
      if (ctx->config) recordImageQualityPixel(*ctx->config, ctx->x + destX, ctx->y + destY, gray, level);
      writer.writePixel(ctx->x + destX, level);
      if (ctx->caching) cacheWriter.writePixel(cacheImageLocal ? destX : ctx->x + destX, level);
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
  ctx.config = &config;
  ctx.x = config.x;
  ctx.y = config.y;
  ctx.dstWidth = config.maxWidth;
  ctx.dstHeight = config.maxHeight;
  ctx.dither = config.useDithering;
  ctx.directBw = config.cbzDirectBwDiagnostic;
  if (config.diagnostics) {
    config.diagnostics->decoder = "TJpgDec";
    config.diagnostics->areaResampling = false;
  }

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
  const int sourceWidth = ctx.srcWidth;
  const int sourceHeight = ctx.srcHeight;
  if (!validateImageDimensionsForRender(ctx.srcWidth, ctx.srcHeight, "TJpgDec JPEG", config)) return false;

  uint8_t scale = 0;
  while (scale < 3 && (ctx.srcWidth >> (scale + 1)) >= ctx.dstWidth &&
         (ctx.srcHeight >> (scale + 1)) >= ctx.dstHeight) {
    scale++;
  }
  // TJpgDec reports callback rectangles in the scaled output coordinate
  // space, not the original JPEG coordinate space.
  ctx.srcWidth = std::max(1, ctx.srcWidth >> scale);
  ctx.srcHeight = std::max(1, ctx.srcHeight >> scale);
  if (config.allowBoundedLargeSource &&
      static_cast<int64_t>(sourceWidth) * static_cast<int64_t>(sourceHeight) > MAX_SOURCE_PIXELS) {
    LOG_DBG("EPUBIMG", "downsample=streamed format=TJpgDec source=%dx%d target=%dx%d scale=1/%u", sourceWidth,
            sourceHeight, ctx.dstWidth, ctx.dstHeight, static_cast<unsigned>(1u << scale));
  }

  if (config.cbzQualityMode) {
    static bool tjpgConfigLogged = false;
    if (!tjpgConfigLogged) {
      LOG_DBG("TJPGCFG",
              "format=%s bytesPerPixel=%d components=%u scale=%u workspace=%lu streamBuffer=%d fastDecode=%d colorPath=%s longHuffmanPatch=jpegdec-only",
              TJPGD_FORMAT_NAME, TJPGD_BYTES_PER_PIXEL, static_cast<unsigned>(decoder.ncomp),
              static_cast<unsigned>(scale), static_cast<unsigned long>(TJPGD_WORK_BYTES),
              JD_SZBUF, JD_FASTDECODE, JD_FORMAT == 2 ? "Y-gray8" : "configured-output");
      tjpgConfigLogged = true;
    }
  }

  if (ctx.directBw && config.cbzDumpTjpgGrayCrop) {
    // A failed diagnostic dump must never prevent the normal bounded decode.
    beginGrayCrop(ctx);
  }

  // TJpgDec emits complete MCU rectangles (all rows for one horizontal MCU)
  // rather than row segments. Assemble one bounded MCU-row band, then feed
  // complete rows to the existing area reducer. This avoids a full decoded
  // framebuffer while preserving correct horizontal and vertical coverage.
  if (!ctx.directBw && ctx.srcWidth > ctx.dstWidth && ctx.srcHeight > ctx.dstHeight) {
    const int bandCapacity = std::max(1, (16 + ((1 << scale) - 1)) >> scale);
    const size_t bandBytes = static_cast<size_t>(ctx.srcWidth) * static_cast<size_t>(bandCapacity);
    const size_t freeHeap = ESP.getFreeHeap();
    if (bandBytes <= MAX_AREA_BAND_BYTES && freeHeap > bandBytes + MIN_AREA_HEAP_HEADROOM) {
      ctx.areaResampler.reset(new (std::nothrow) BoundedAreaResampler());
      ctx.areaSourceBand.reset(new (std::nothrow) uint8_t[bandBytes]);
      if (ctx.areaResampler && ctx.areaSourceBand &&
          ctx.areaResampler->begin(ctx.srcWidth, ctx.srcHeight, ctx.dstWidth, ctx.dstHeight,
                                   config.cbzQualityMode)) {
        ctx.areaBandCapacity = bandCapacity;
        ctx.areaResampling = true;
        if (config.diagnostics) {
          config.diagnostics->areaResampling = true;
          config.diagnostics->resampleMode = config.cbzQualityMode ? "manga_nearest" : "bounded_area";
        }
        LOG_DBG("JPG", "TJpgDec using bounded %s downsampling (%dx%d -> %dx%d, band=%d rows)",
                config.cbzQualityMode ? "manga-nearest" : "area", ctx.srcWidth, ctx.srcHeight, ctx.dstWidth,
                ctx.dstHeight, bandCapacity);
      } else {
        ctx.areaResampler.reset();
        ctx.areaSourceBand.reset();
      }
    } else {
      LOG_DBG("JPG", "TJpgDec area downsampling skipped (band=%lu bytes, free=%lu)",
              static_cast<unsigned long>(bandBytes), static_cast<unsigned long>(freeHeap));
    }
  }

  // Use the same bounded streaming cache as the normal JPEG decoder. The old
  // fallback allocated the complete 2bpp image (often 80–96 KB) and silently
  // disabled caching when the heap was fragmented. A successful decode then
  // ran again for every grayscale strip. A small band keeps the heap stable
  // and leaves a persistent .pxc for all later passes/page turns.
  if (!ctx.directBw && !config.cachePath.empty()) {
    ctx.cache.diagnostics = config.diagnostics;
    // TJpgDec emits at most one JPEG MCU row per callback. A 4:2:0 MCU is
    // 16 source rows high; account for the decoder's 1/2, 1/4 and 1/8 scale
    // modes so the cache band is no larger than necessary.
    constexpr int MAX_MCU_ROWS = 16;
    const int scaleFactor = 1 << scale;
    const int maxBlockDstRows = std::max(1, (MAX_MCU_ROWS + scaleFactor - 1) / scaleFactor);
    ctx.caching = ctx.cache.begin(config.cachePath, ctx.dstWidth, ctx.dstHeight, config.x, config.y,
                                  maxBlockDstRows);
    if (!ctx.caching) {
      // A direct decode would look correct for the first BW pass but would be
      // repeated for every grayscale pass because there is no cache to reuse.
      // Fail fast instead of turning one image into a multi-second render loop.
      LOG_ERR("IMG", "TJpgDec cache stream unavailable; skipping uncached decode");
      return false;
    }
  }
  const unsigned long decodeStartedMs = millis();
  const JRESULT result = jd_decomp(&decoder, tjpgdOutput, scale);
  if (config.diagnostics) config.diagnostics->decodeMs = millis() - decodeStartedMs;
  if (ctx.io.ioError || result != JDR_OK) {
    LOG_ERR("JPG", "TJpgDec decode failed (%d): %s", static_cast<int>(result), imagePath.c_str());
    closeGrayCrop(ctx, false);
    if (ctx.caching) ctx.cache.abort();
    return false;
  }

  closeGrayCrop(ctx, true);

  if (ctx.directBw) {
    const int typicalIndex = [&ctx] {
      size_t best = 0;
      for (size_t i = 1; i < 4; ++i) {
        if (ctx.shapeCount[i] > ctx.shapeCount[best]) best = i;
      }
      return best;
    }();
    const unsigned long expectedPixels = static_cast<unsigned long>(static_cast<uint64_t>(ctx.srcWidth) *
                                                                      static_cast<uint64_t>(ctx.srcHeight));
    const unsigned long deliveredPixels = static_cast<unsigned long>(ctx.callbackArea);
    const unsigned long missingPixels = expectedPixels > deliveredPixels ? expectedPixels - deliveredPixels : 0;
    LOG_DBG("TJPGCB",
            "callbacks=%lu rectMin=%dx%d rectMax=%dx%d rectTypical=%dx%d pixelsExpected=%lu pixelsDelivered=%lu coverageMissing=%lu coverageOverlap=%lu boundsErrors=%lu orderErrors=%lu gaps=%lu rowStrideMode=actual_rect_width bytesPerPixel=%d",
            static_cast<unsigned long>(ctx.callbackCount),
            ctx.callbackCount == 0 ? 0 : ctx.minRectWidth, ctx.callbackCount == 0 ? 0 : ctx.minRectHeight,
            ctx.maxRectWidth, ctx.maxRectHeight,
            ctx.callbackCount == 0 ? 0 : ctx.shapeWidth[typicalIndex],
            ctx.callbackCount == 0 ? 0 : ctx.shapeHeight[typicalIndex], expectedPixels, deliveredPixels,
            missingPixels, static_cast<unsigned long>(ctx.overlapCount),
            static_cast<unsigned long>(ctx.boundsErrors), static_cast<unsigned long>(ctx.orderErrors),
            static_cast<unsigned long>(ctx.gapCount), TJPGD_BYTES_PER_PIXEL);
    LOG_DBG("CBZJPEG",
            "decoder=TJpgDec source=%dx%d callbacks=%lu rectMin=%dx%d rectMax=%dx%d rectTypical=%dx%d components=%u mcuBlocks=%ux%u",
            ctx.srcWidth, ctx.srcHeight, static_cast<unsigned long>(ctx.callbackCount),
            ctx.callbackCount == 0 ? 0 : ctx.minRectWidth, ctx.callbackCount == 0 ? 0 : ctx.minRectHeight,
            ctx.maxRectWidth, ctx.maxRectHeight,
            ctx.callbackCount == 0 ? 0 : ctx.shapeWidth[typicalIndex],
            ctx.callbackCount == 0 ? 0 : ctx.shapeHeight[typicalIndex],
            static_cast<unsigned>(decoder.ncomp), static_cast<unsigned>(decoder.msx), static_cast<unsigned>(decoder.msy));
  }

  if (ctx.areaResampling) {
    if (!flushTjpgdAreaBand(ctx) || !ctx.areaResampler->finish()) {
      LOG_ERR("JPG", "TJpgDec image resampler did not receive a complete image");
      if (ctx.caching) ctx.cache.abort();
      return false;
    }
    writePendingTjpgdAreaRows(ctx);
  }

  if (ctx.caching && !ctx.cache.finalize()) {
    LOG_ERR("IMG", "TJpgDec cache finalize failed: %s", config.cachePath.c_str());
    return false;
  }
  return true;
}
