#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <PngToBmpConverter.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <memory>
#include <new>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/MoonIcon.h"
#include "images/NooirLogo360.h"

namespace {

// A page overlay is decoded directly from PNG so transparent pixels leave the
// reader framebuffer untouched. This is deliberately separate from the normal
// PNG-to-BMP sleep path, which flattens transparency against white.
struct OverlayPngContext {
  GfxRenderer* renderer{nullptr};
  int srcWidth{0};
  int srcHeight{0};
  int dstX{0};
  int dstY{0};
  int dstWidth{0};
  int dstHeight{0};
  bool hasTransparentColor{false};
  uint32_t transparentColor{0};
};

void* overlayPngOpen(const char* filename, int32_t* size) {
  auto* file = new (std::nothrow) HalFile();
  if (!file || !Storage.openFileForRead("SLP", std::string(filename), *file)) {
    delete file;
    return nullptr;
  }
  *size = file->size();
  return file;
}

void overlayPngClose(void* handle) {
  auto* file = reinterpret_cast<HalFile*>(handle);
  if (file) {
    file->close();
    delete file;
  }
}

int32_t overlayPngRead(PNGFILE* pngFile, uint8_t* buffer, int32_t length) {
  auto* file = reinterpret_cast<HalFile*>(pngFile->fHandle);
  return file ? file->read(buffer, length) : 0;
}

int32_t overlayPngSeek(PNGFILE* pngFile, int32_t position) {
  auto* file = reinterpret_cast<HalFile*>(pngFile->fHandle);
  return file ? file->seek(position) : -1;
}

int overlayBytesPerPixel(int pixelType) {
  switch (pixelType) {
    case PNG_PIXEL_TRUECOLOR:
      return 3;
    case PNG_PIXEL_GRAY_ALPHA:
      return 2;
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      return 4;
    default:
      return 1;
  }
}

int overlayPackedRowBytes(int width, int bitsPerSample) { return (width * bitsPerSample + 7) / 8; }

int overlayRequiredPngBufferBytes(int width, int pixelType, int bitsPerSample) {
  int pitch = width * overlayBytesPerPixel(pixelType);
  if ((pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED) && bitsPerSample < 8) {
    pitch = overlayPackedRowBytes(width, bitsPerSample);
  }
  return (pitch + 1) * 2 + 32;
}

bool overlaySupportedBitDepth(int pixelType, int bitsPerSample) {
  if (bitsPerSample == 8) return true;
  if (bitsPerSample != 1 && bitsPerSample != 2 && bitsPerSample != 4) return false;
  return pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED;
}

uint8_t overlayReadPackedSample(const uint8_t* pixels, int x, int bitsPerSample) {
  if (bitsPerSample == 8) return pixels[x];
  const int bitOffset = x * bitsPerSample;
  const int shift = 8 - bitsPerSample - (bitOffset & 7);
  const uint8_t mask = static_cast<uint8_t>((1U << bitsPerSample) - 1U);
  return static_cast<uint8_t>((pixels[bitOffset >> 3] >> shift) & mask);
}

uint8_t overlayExpandSample(uint8_t sample, int bitsPerSample) {
  if (bitsPerSample == 8) return sample;
  const uint8_t maxSample = static_cast<uint8_t>((1U << bitsPerSample) - 1U);
  return static_cast<uint8_t>((sample * 255U) / maxSample);
}

bool overlayPixel(const OverlayPngContext& ctx, const PNGDRAW& draw, int sourceX, bool& black) {
  const uint8_t* pixels = draw.pPixels;
  uint8_t gray = 255;
  uint8_t alpha = 255;

  switch (draw.iPixelType) {
    case PNG_PIXEL_GRAYSCALE: {
      const uint8_t sample = overlayReadPackedSample(pixels, sourceX, draw.iBpp);
      gray = overlayExpandSample(sample, draw.iBpp);
      if (ctx.hasTransparentColor && sample == static_cast<uint8_t>(ctx.transparentColor & 0xffU)) alpha = 0;
      break;
    }
    case PNG_PIXEL_TRUECOLOR: {
      const uint8_t* p = &pixels[sourceX * 3];
      gray = static_cast<uint8_t>((p[0] * 77U + p[1] * 150U + p[2] * 29U) >> 8);
      if (ctx.hasTransparentColor && ((static_cast<uint32_t>(p[0]) << 16) |
                                      (static_cast<uint32_t>(p[1]) << 8) | p[2]) == ctx.transparentColor) {
        alpha = 0;
      }
      break;
    }
    case PNG_PIXEL_INDEXED: {
      const uint8_t index = overlayReadPackedSample(pixels, sourceX, draw.iBpp);
      if (!draw.pPalette) return false;
      const uint8_t* p = &draw.pPalette[index * 3];
      gray = static_cast<uint8_t>((p[0] * 77U + p[1] * 150U + p[2] * 29U) >> 8);
      if (draw.iHasAlpha) alpha = draw.pPalette[768 + index];
      break;
    }
    case PNG_PIXEL_GRAY_ALPHA:
      gray = pixels[sourceX * 2];
      alpha = pixels[sourceX * 2 + 1];
      break;
    case PNG_PIXEL_TRUECOLOR_ALPHA: {
      const uint8_t* p = &pixels[sourceX * 4];
      gray = static_cast<uint8_t>((p[0] * 77U + p[1] * 150U + p[2] * 29U) >> 8);
      alpha = p[3];
      break;
    }
    default:
      return false;
  }

  // A threshold keeps the overlay crisp on the X4's 1-bit panel. Transparent
  // pixels are never written, so the current reader page remains visible.
  if (alpha < 128) return false;
  black = gray < 128;
  return true;
}

int overlayPngDraw(PNGDRAW* draw) {
  auto* ctx = reinterpret_cast<OverlayPngContext*>(draw ? draw->pUser : nullptr);
  if (!ctx || !ctx->renderer || !draw || draw->y < 0 || draw->y >= ctx->srcHeight) return 1;

  int firstDstY = (draw->y * ctx->dstHeight) / ctx->srcHeight;
  int endDstY = ((draw->y + 1) * ctx->dstHeight) / ctx->srcHeight;
  if (endDstY <= firstDstY) endDstY = firstDstY + 1;
  if (firstDstY >= ctx->dstHeight) return 1;
  endDstY = std::min(endDstY, ctx->dstHeight);

  for (int dstY = firstDstY; dstY < endDstY; ++dstY) {
    const int outputY = ctx->dstY + dstY;
    for (int dstX = 0; dstX < ctx->dstWidth; ++dstX) {
      const int sourceX = std::min(ctx->srcWidth - 1, (dstX * ctx->srcWidth) / ctx->dstWidth);
      bool black = false;
      if (overlayPixel(*ctx, *draw, sourceX, black)) {
        ctx->renderer->drawPixel(ctx->dstX + dstX, outputY, black);
      }
    }
  }
  return 1;
}

void preconditionSleepRefresh(GfxRenderer& renderer) {
  // A clean FAST pass resets the panel charge state before the final sleep
  // image. This is the key ghosting mitigation used by CrossPet.
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  renderer.clearScreen();
}

void displaySleepFrame(GfxRenderer& renderer, HalDisplay::RefreshMode mode) {
  // Power down the analog driver after the sleep frame so gray/black pixels do
  // not slowly fade while the device is idle. Restore the user's normal value.
  renderer.setFadingFix(true);
  renderer.displayBuffer(mode);
  renderer.setFadingFix(SETTINGS.fadingFix);
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  // Overlay mode intentionally leaves the reader framebuffer intact: the PNG
  // contributes only opaque pixels and transparent pixels are skipped.
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY && renderOverlaySleepScreen()) {
    return;
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY):
      return renderDefaultSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /.sleep (preferred) or /sleep directory. Some devices
  // always have an empty /.sleep directory, so only prefer it when it actually
  // contains an image; otherwise fall through to /sleep.
  const char* sleepDir = nullptr;
  std::vector<std::string> files;
  auto collectImages = [](HalFile& folder, std::vector<std::string>& output) {
    char name[500];
    for (auto dirFile = folder.openNextFile(); dirFile; dirFile = folder.openNextFile()) {
      if (dirFile.isDirectory()) {
        dirFile.close();
        continue;
      }
      dirFile.getName(name, sizeof(name));
      const std::string filename(name);
      if (filename.empty() || filename[0] == '.') {
        dirFile.close();
        continue;
      }
      const bool isBmp = FsHelpers::hasBmpExtension(filename);
      const bool isPng = FsHelpers::hasPngExtension(filename);
      if (!isBmp && !isPng) {
        dirFile.close();
        continue;
      }
      if (isBmp) {
        Bitmap bitmap(dirFile);
        if (bitmap.parseHeaders() != BmpReaderError::Ok) {
          dirFile.close();
          continue;
        }
      }
      output.emplace_back(filename);
      dirFile.close();
    }
  };

  auto dir = Storage.open("/.sleep");
  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
    collectImages(dir, files);
  }
  if (files.empty()) {
    if (dir) dir.close();
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
      collectImages(dir, files);
    }
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  if (renderCustomImage("/sleep.bmp") || renderCustomImage("/sleep.png")) {
    if (dir) dir.close();
    return;
  }

  if (sleepDir) {
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Try a random starting point, then fall through the remaining files if
      // a damaged/unsupported image is encountered. One bad PNG must not make
      // every Custom sleep screen fall back to the default logo.
      const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
      const uint16_t randomStart = static_cast<uint16_t>(random(fileCount));
      for (uint8_t pass = 0; pass < 2; ++pass) {
        for (uint16_t attempt = 0; attempt < fileCount; ++attempt) {
          const uint16_t randomFileIndex = static_cast<uint16_t>((randomStart + attempt) % fileCount);
          if (pass == 0 && APP_STATE.isRecentSleep(randomFileIndex, window)) continue;
          const auto filename = std::string(sleepDir) + "/" + files[randomFileIndex];
          LOG_DBG("SLP", "Trying custom sleep image: %s", filename.c_str());
          if (renderCustomImage(filename)) {
            APP_STATE.pushRecentSleep(randomFileIndex);
            APP_STATE.saveToFile();
            if (dir) dir.close();
            return;
          }
        }
      }
    }
  }
  if (dir) dir.close();

  renderDefaultSleepScreen();
}

bool SleepActivity::renderCustomImage(const std::string& path) const {
  HalFile imageFile;
  if (!Storage.openFileForRead("SLP", path, imageFile)) return false;

  if (FsHelpers::hasBmpExtension(path)) {
    Bitmap bitmap(imageFile, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      LOG_DBG("SLP", "Invalid BMP sleep image: %s", path.c_str());
      return false;
    }
    LOG_DBG("SLP", "Loading BMP: %s", path.c_str());
    renderBitmapSleepScreen(bitmap);
    return true;
  }

  if (!FsHelpers::hasPngExtension(path)) return false;

  // Decode to an SD-backed BMP rather than a full in-memory image. The PNG
  // converter holds scanlines only, which is safe on the X4's 380 KB RAM.
  constexpr const char* PNG_CACHE_PATH = "/.crosspoint/.sleep-image.bmp";
  Storage.mkdir("/.crosspoint");
  HalFile bmpOut;
  if (!Storage.openFileForWrite("SLP", PNG_CACHE_PATH, bmpOut)) return false;

  const bool crop = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
  const bool converted = PngToBmpConverter::pngFileToBmpStream(imageFile, bmpOut, crop);
  // Both handles must be closed before reopening the generated BMP.
  imageFile.close();
  bmpOut.close();
  if (!converted) {
    LOG_ERR("SLP", "Failed to decode PNG sleep image: %s", path.c_str());
    Storage.remove(PNG_CACHE_PATH);
    return false;
  }

  HalFile cachedBmp;
  if (!Storage.openFileForRead("SLP", PNG_CACHE_PATH, cachedBmp)) return false;
  Bitmap bitmap(cachedBmp, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid converted sleep image: %s", path.c_str());
    return false;
  }
  LOG_DBG("SLP", "Loading PNG: %s", path.c_str());
  renderBitmapSleepScreen(bitmap);
  return true;
}

bool SleepActivity::renderOverlaySleepScreen() const {
  // A named root file wins for users who want a fixed overlay. If it is not
  // present, use a random PNG from the same folders as Custom sleep images.
  if (renderOverlayPng("/sleep-overlay.png")) {
    LOG_DBG("SLP", "Rendering fixed page overlay: /sleep-overlay.png");
    return true;
  }

  const char* sleepDir = nullptr;
  std::vector<std::string> files;
  auto collectPngs = [](HalFile& folder, std::vector<std::string>& output) {
    char name[500];
    for (auto file = folder.openNextFile(); file; file = folder.openNextFile()) {
      if (file.isDirectory()) {
        file.close();
        continue;
      }
      file.getName(name, sizeof(name));
      const std::string filename(name);
      if (!filename.empty() && filename[0] != '.' && FsHelpers::hasPngExtension(filename)) output.push_back(filename);
      file.close();
    }
  };
  auto dir = Storage.open("/.sleep");
  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
    collectPngs(dir, files);
  }
  if (files.empty()) {
    if (dir) dir.close();
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
      collectPngs(dir, files);
    }
  }
  if (sleepDir) {
    if (!files.empty()) {
      const size_t start = static_cast<size_t>(random(files.size()));
      for (size_t attempt = 0; attempt < files.size(); ++attempt) {
        const size_t index = (start + attempt) % files.size();
        const std::string path = std::string(sleepDir) + "/" + files[index];
        if (renderOverlayPng(path)) {
          if (dir) dir.close();
          LOG_DBG("SLP", "Rendering random page overlay: %s", path.c_str());
          return true;
        }
      }
    }
  }
  if (dir) dir.close();
  LOG_DBG("SLP", "No page overlay PNG found; using the default sleep screen");
  return false;
}

bool SleepActivity::renderOverlayPng(const std::string& path) const {
  constexpr size_t PNG_DECODER_APPROX_SIZE = 44 * 1024;
  constexpr size_t MIN_FREE_HEAP_FOR_PNG = PNG_DECODER_APPROX_SIZE + 16 * 1024;
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("SLP", "Not enough heap for page overlay PNG");
    return false;
  }

  auto png = makeUniqueNoThrow<PNG>();
  if (!png) return false;

  const int openResult = png->open(path.c_str(), overlayPngOpen, overlayPngClose, overlayPngRead, overlayPngSeek,
                                   overlayPngDraw);
  const ScopedCleanup cleanup{[&png]() { png->close(); }};
  if (openResult != PNG_SUCCESS) return false;

  const int srcWidth = png->getWidth();
  const int srcHeight = png->getHeight();
  const int pixelType = png->getPixelType();
  const int bitsPerSample = png->getBpp();
  if (srcWidth <= 0 || srcHeight <= 0 || png->isInterlaced() ||
      overlayRequiredPngBufferBytes(srcWidth, pixelType, bitsPerSample) > PNG_MAX_BUFFERED_PIXELS ||
      !overlaySupportedBitDepth(pixelType, bitsPerSample)) {
    LOG_ERR("SLP", "Unsupported page overlay PNG: %s", path.c_str());
    return false;
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  int dstWidth = srcWidth;
  int dstHeight = srcHeight;
  if (dstWidth > screenWidth || dstHeight > screenHeight) {
    if (static_cast<int64_t>(srcWidth) * screenHeight > static_cast<int64_t>(srcHeight) * screenWidth) {
      dstWidth = screenWidth;
      dstHeight = std::max(1, (srcHeight * screenWidth) / srcWidth);
    } else {
      dstHeight = screenHeight;
      dstWidth = std::max(1, (srcWidth * screenHeight) / srcHeight);
    }
  }

  OverlayPngContext context;
  context.renderer = &renderer;
  context.srcWidth = srcWidth;
  context.srcHeight = srcHeight;
  context.dstWidth = dstWidth;
  context.dstHeight = dstHeight;
  context.dstX = (screenWidth - dstWidth) / 2;
  context.dstY = (screenHeight - dstHeight) / 2;
  context.transparentColor = png->getTransparentColor();
  context.hasTransparentColor = png->hasAlpha() &&
                                (pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_TRUECOLOR);

  LOG_DBG("SLP", "Overlay PNG %dx%d -> %dx%d", srcWidth, srcHeight, dstWidth, dstHeight);
  const int decodeResult = png->decode(&context, 0);
  if (decodeResult != PNG_SUCCESS) {
    LOG_ERR("SLP", "Failed to decode page overlay PNG: %d", decodeResult);
    return false;
  }

  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
  return true;
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  preconditionSleepRefresh(renderer);
  const int logoY = pageHeight / 2 - NOOIR_LOGO_HEIGHT / 2 - 24;
  renderer.drawImage(NooirLogo360, (pageWidth - NOOIR_LOGO_WIDTH) / 2, logoY, NOOIR_LOGO_WIDTH, NOOIR_LOGO_HEIGHT);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 54, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  preconditionSleepRefresh(renderer);

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.setFadingFix(true);
    renderer.displayGrayBuffer();
    renderer.setFadingFix(SETTINGS.fadingFix);
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  // Quick Resume deliberately keeps the reader page; use only the driver
  // shutdown fix here, not the full pre-clear sequence.
  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBlankSleepScreen() const {
  preconditionSleepRefresh(renderer);
  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
}
