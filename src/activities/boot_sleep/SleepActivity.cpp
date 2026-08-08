#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <PngToBmpConverter.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>

#include "BookStateStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/MoonIcon.h"
#include "images/NooirLogo360.h"
#include "util/ClipFile.h"

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
  // PNGdec processes tRNS while decode() is running, not during open().
  // -2 means not initialized, -1 means no color-key transparency, and a
  // non-negative value is the decoder's 0x00RRGGBB/low-byte gray key.
  int32_t transparentColor{-2};
  PNG* pngObj{nullptr};
  bool* transparencyDetected{nullptr};
  GfxRenderer::RenderMode renderMode{GfxRenderer::BW};
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

bool overlayPixel(OverlayPngContext& ctx, const PNGDRAW& draw, int sourceX, uint8_t& level) {
  const uint8_t* pixels = draw.pPixels;
  uint8_t gray = 255;
  uint8_t alpha = 255;

  // tRNS is parsed just before the first scanline callback. Resolve the
  // color-key at that point so grayscale/truecolor PNGs with a transparent
  // background do not get flattened into an opaque overlay.
  if (ctx.transparentColor == -2) {
    const int pixelType = draw.iPixelType;
    ctx.transparentColor = (draw.iHasAlpha &&
                            (pixelType == PNG_PIXEL_TRUECOLOR || pixelType == PNG_PIXEL_GRAYSCALE))
                               ? static_cast<int32_t>(ctx.pngObj->getTransparentColor())
                               : -1;
  }

  switch (draw.iPixelType) {
    case PNG_PIXEL_GRAYSCALE: {
      const uint8_t sample = overlayReadPackedSample(pixels, sourceX, draw.iBpp);
      gray = overlayExpandSample(sample, draw.iBpp);
      if (ctx.transparentColor >= 0 && sample == static_cast<uint8_t>(ctx.transparentColor & 0xffU)) alpha = 0;
      break;
    }
    case PNG_PIXEL_TRUECOLOR: {
      const uint8_t* p = &pixels[sourceX * 3];
      gray = static_cast<uint8_t>((p[0] * 77U + p[1] * 150U + p[2] * 29U) >> 8);
      if (ctx.transparentColor >= 0 && ((static_cast<uint32_t>(p[0]) << 16) |
                                        (static_cast<uint32_t>(p[1]) << 8) | p[2]) ==
                                           static_cast<uint32_t>(ctx.transparentColor)) {
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

  // Transparent pixels are never written, so the current reader page remains
  // visible. The PNG's gray value is kept as one of the X4's four levels.
  if (alpha < 255 && ctx.transparencyDetected) *ctx.transparencyDetected = true;
  if (alpha < 128) return false;
  level = static_cast<uint8_t>(gray >> 6);
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
      uint8_t level = 0;
      if (!overlayPixel(*ctx, *draw, sourceX, level)) continue;
      const int x = ctx->dstX + dstX;
      switch (ctx->renderMode) {
        case GfxRenderer::GRAYSCALE_LSB:
          if (level == 1) ctx->renderer->drawPixel(x, outputY, false);
          break;
        case GfxRenderer::GRAYSCALE_MSB:
          if (level == 1 || level == 2) ctx->renderer->drawPixel(x, outputY, false);
          break;
        case GfxRenderer::BW:
        default:
          // Match Bitmap::drawBitmap's four-level mapping: level 3 is white;
          // the other levels are part of the black base frame.
          ctx->renderer->drawPixel(x, outputY, level >= 3 ? false : true);
          break;
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

struct SleepBookSnapshot {
  bool hasBook = false;
  std::string title;
  uint8_t progress = 0;
  uint32_t readingSeconds = 0;
  uint32_t lastSessionSeconds = 0;
  uint32_t todaySeconds = 0;
  uint16_t sessions = 0;
  BookStatus status = BookStatus::New;
};

struct SleepLibrarySnapshot {
  uint32_t readingSeconds = 0;
  uint32_t todaySeconds = 0;
  uint32_t sessions = 0;
  uint16_t finished = 0;
};

struct SleepClippingSnapshot {
  bool valid = false;
  std::string path;
  std::string text;
  std::string title;
  uint16_t page = 0;
};

const RecentBook* recentBookForSleepPath(const std::string& path) {
  const auto it = std::find_if(RECENT_BOOKS.getBooks().begin(), RECENT_BOOKS.getBooks().end(),
                               [&](const RecentBook& book) { return book.path == path; });
  return it == RECENT_BOOKS.getBooks().end() ? nullptr : &*it;
}

std::string sleepBookPath() {
  // When sleeping from Reader, keep the book that is actually open. From the
  // bookshelf or another activity, use the most recent book as the cover
  // source so Cover + Overlay and Clipping + Cover remain useful there too.
  if (APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty()) return APP_STATE.openEpubPath;
  const auto& recent = RECENT_BOOKS.getBooks();
  return recent.empty() ? APP_STATE.openEpubPath : recent.front().path;
}

std::string sleepFilename(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string sleepDuration(const uint32_t seconds) {
  const uint32_t minutes = (seconds + 30) / 60;
  if (minutes < 60) return std::to_string(minutes) + " min";
  const uint32_t hours = minutes / 60;
  const uint32_t rest = minutes % 60;
  return rest == 0 ? std::to_string(hours) + " h" : std::to_string(hours) + " h " + std::to_string(rest) + " min";
}

const char* sleepStatusText(const BookStatus status, const uint8_t progress) {
  if (progress >= 100 || status == BookStatus::Finished) return "Finished";
  if (status == BookStatus::OnHold) return "On hold";
  if (progress > 0 || status == BookStatus::Reading) return "Ongoing";
  return "New";
}

SleepBookSnapshot loadSleepBookSnapshot() {
  SleepBookSnapshot snapshot;
  const std::string& path = APP_STATE.openEpubPath;
  if (path.empty()) return snapshot;

  snapshot.hasBook = true;
  const RecentBook* recent = recentBookForSleepPath(path);
  const BookState* state = BOOK_STATES.find(path);
  snapshot.title = recent && !recent->title.empty() ? recent->title : sleepFilename(path);
  snapshot.progress = std::max<uint8_t>(state ? state->progressPercent : 0, recent ? recent->progressPercent : 0);
  snapshot.readingSeconds = std::max<uint32_t>(state ? state->readingSeconds : 0, recent ? recent->readingSeconds : 0);
  snapshot.lastSessionSeconds = recent ? recent->lastSessionSeconds : 0;
  snapshot.sessions = std::max<uint16_t>(state ? state->readingSessions : 0, recent ? recent->readingSessions : 0);
  snapshot.status = state ? state->status : (snapshot.progress > 0 ? BookStatus::Reading : BookStatus::New);
  if (snapshot.progress >= 100) snapshot.status = BookStatus::Finished;
  const uint32_t today = halClock.getDateKey();
  if (recent && today != 0 && recent->dailyReadingDateKey == today) snapshot.todaySeconds = recent->dailyReadingSeconds;
  return snapshot;
}

SleepClippingSnapshot loadSleepClippingSnapshot() {
  SleepClippingSnapshot snapshot;
  std::vector<std::string> candidates;
  const auto addCandidate = [&candidates](const std::string& path) {
    if (path.empty() || !FsHelpers::hasEpubExtension(path) ||
        std::find(candidates.begin(), candidates.end(), path) != candidates.end())
      return;
    candidates.push_back(path);
  };
  if (!APP_STATE.openEpubPath.empty()) addCandidate(APP_STATE.openEpubPath);
  for (const auto& recent : RECENT_BOOKS.getBooks()) addCandidate(recent.path);
  if (candidates.empty()) return snapshot;

  // Start at a random recent book, then stop at the first book with saved
  // clippings. The selected clipping and its source path travel together so
  // the matching cover can be rendered underneath it.
  const size_t start = static_cast<size_t>(random(static_cast<long>(candidates.size())));
  for (size_t offset = 0; offset < candidates.size(); ++offset) {
    const std::string& path = candidates[(start + offset) % candidates.size()];
    std::vector<ClippingEntry> clippings;
    if (!ClipFile::load(path, clippings) || clippings.empty()) continue;
    const size_t selected = static_cast<size_t>(random(static_cast<long>(clippings.size())));
    const ClippingEntry& clipping = clippings[std::min(selected, clippings.size() - 1)];
    if (clipping.text.empty()) continue;

    snapshot.valid = true;
    snapshot.path = path;
    snapshot.text = clipping.text;
    snapshot.page = clipping.page;
    const RecentBook* recent = recentBookForSleepPath(path);
    snapshot.title = recent && !recent->title.empty() ? recent->title : sleepFilename(path);
    break;
  }
  return snapshot;
}

SleepLibrarySnapshot loadSleepLibrarySnapshot() {
  SleepLibrarySnapshot snapshot;
  uint32_t bookSeconds = 0;
  uint32_t bookSessions = 0;
  auto add = [&](const uint8_t progress, const BookStatus status, const uint32_t seconds, const uint16_t sessions) {
    bookSeconds += std::min(seconds, UINT32_MAX - bookSeconds);
    bookSessions += std::min<uint32_t>(sessions, UINT32_MAX - bookSessions);
    if (progress >= 100 || status == BookStatus::Finished) {
      if (snapshot.finished < UINT16_MAX) ++snapshot.finished;
    }
  };

  for (const auto& state : BOOK_STATES.getBooks()) {
    const RecentBook* recent = recentBookForSleepPath(state.path);
    const uint8_t progress = std::max<uint8_t>(state.progressPercent, recent ? recent->progressPercent : 0);
    const BookStatus status = progress >= 100 ? BookStatus::Finished : state.status;
    add(progress, status, std::max<uint32_t>(state.readingSeconds, recent ? recent->readingSeconds : 0),
        std::max<uint16_t>(state.readingSessions, recent ? recent->readingSessions : 0));
  }
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (BOOK_STATES.find(recent.path)) continue;
    add(recent.progressPercent, recent.progressPercent >= 100 ? BookStatus::Finished
                                                               : (recent.progressPercent > 0 ? BookStatus::Reading
                                                                                            : BookStatus::New),
        recent.readingSeconds, recent.readingSessions);
  }

  snapshot.readingSeconds = std::max(bookSeconds, READING_STATS.totalSeconds());
  snapshot.sessions = std::max(bookSessions, READING_STATS.totalSessions());
  const uint32_t today = halClock.getDateKey();
  snapshot.todaySeconds = today == 0 ? 0 : READING_STATS.secondsForDate(today);
  return snapshot;
}

void drawSleepMetric(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const char* label, const std::string& value) {
  renderer.drawRoundedRect(x, y, width, height, 1, 8, true);
  renderer.drawText(SMALL_FONT_ID, x + 12, y + 13, label, true, EpdFontFamily::BOLD);
  const int valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x + (width - valueWidth) / 2, y + height - 24, value.c_str(), true,
                    EpdFontFamily::BOLD);
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
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_OVERLAY):
      return renderCoverOverlaySleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY):
      return renderDefaultSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP):
      return renderReadingStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS):
      return renderMinimalStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CLIPPING_COVER):
      return renderClippingCoverSleepScreen();
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
  // Keep an 8-bit grayscale intermediate for custom sleep PNGs. The X4's
  // grayscale waveform performs the final four-level conversion; reducing to
  // 2-bit here makes photographs and soft artwork look unnecessarily harsh.
  const bool converted = PngToBmpConverter::pngFileTo8BitBmpStream(imageFile, bmpOut, crop);
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
  // Do not treat ordinary custom sleep artwork as an overlay: most of those
  // PNGs are opaque and would hide the cover. Use one explicitly named file,
  // in priority order, so transparency is intentional and predictable.
  const char* candidates[] = {"/sleep-overlay.png", "/.sleep/overlay.png", "/sleep/overlay.png"};
  for (const char* path : candidates) {
    if (!Storage.exists(path)) continue;
    if (renderOverlayPng(path)) {
      LOG_DBG("SLP", "Rendering page overlay: %s", path);
      return true;
    }
    LOG_ERR("SLP", "Overlay PNG is invalid or has no transparent pixels: %s", path);
    return false;
  }
  LOG_DBG("SLP", "No dedicated page overlay PNG found");
  return false;
}

bool SleepActivity::renderOverlayPng(const std::string& path) const {
  constexpr size_t PNG_DECODER_APPROX_SIZE = 44 * 1024;
  constexpr size_t MIN_FREE_HEAP_FOR_PNG = PNG_DECODER_APPROX_SIZE + 16 * 1024;
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("SLP", "Not enough heap for page overlay PNG");
    return false;
  }

  bool transparencyDetected = false;

  // Decode the same PNG three times. The first pass creates the opaque
  // black/white base while preserving transparent pixels. The next two passes
  // add the LSB/MSB grayscale planes on top of that base, so an overlay can
  // retain the page underneath instead of clearing it to white.
  auto decodePass = [&](const GfxRenderer::RenderMode mode) -> bool {
    auto png = makeUniqueNoThrow<PNG>();
    if (!png) return false;

    const int openResult = png->open(path.c_str(), overlayPngOpen, overlayPngClose, overlayPngRead,
                                     overlayPngSeek, overlayPngDraw);
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
    // The decoder fills tRNS metadata during decode(), so the callback lazily
    // resolves the color key on the first scanline.
    context.transparentColor = -2;
    context.pngObj = png.get();
    context.transparencyDetected = &transparencyDetected;
    context.renderMode = mode;

    LOG_DBG("SLP", "Overlay PNG %dx%d -> %dx%d (%s)", srcWidth, srcHeight, dstWidth, dstHeight,
            mode == GfxRenderer::BW ? "BW" : (mode == GfxRenderer::GRAYSCALE_LSB ? "LSB" : "MSB"));
    const int decodeResult = png->decode(&context, 0);
    if (decodeResult != PNG_SUCCESS) {
      LOG_ERR("SLP", "Failed to decode page overlay PNG: %d", decodeResult);
      return false;
    }
    return true;
  };

  renderer.setRenderMode(GfxRenderer::BW);
  if (!decodePass(GfxRenderer::BW)) return false;
  if (!transparencyDetected) {
    // The first pass may have touched the framebuffer, but it has not been
    // sent to the panel yet. The caller can redraw the cover/default screen.
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }

  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  // Each grayscale plane is an independent bit plane. Start it from an
  // all-black scratch frame, just like ReaderUtils::renderAntiAliased(), so
  // white pixels from the BW pass do not accidentally become set bits in the
  // gray planes. Transparent overlay pixels intentionally remain zero here;
  // the underlying reader page is already represented by the BW base frame.
  renderer.clearScreen(0x00);
  if (!decodePass(GfxRenderer::GRAYSCALE_LSB)) {
    renderer.setRenderMode(GfxRenderer::BW);
    displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.clearScreen(0x00);
  if (!decodePass(GfxRenderer::GRAYSCALE_MSB)) {
    renderer.setRenderMode(GfxRenderer::BW);
    displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
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

void SleepActivity::renderCoverSleepScreen(const std::string& requestedBookPath) const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  const std::string bookPath = requestedBookPath.empty() ? sleepBookPath() : requestedBookPath;
  if (bookPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(bookPath)) {
    // Handle XTC file
    Xtc lastXtc(bookPath, "/.crosspoint");
    // The bookshelf normally creates this cache before the device sleeps.
    // Reuse it without reopening/parsing the XTC container; that avoids a
    // noticeable pause when entering sleep after a large book was read.
    coverBmpPath = lastXtc.getCoverBmpPath();
    if (!Storage.exists(coverBmpPath.c_str())) {
      if (!lastXtc.load()) {
        LOG_ERR("SLP", "Failed to load last XTC");
        return (this->*renderNoCoverSleepScreen)();
      }

      if (!lastXtc.generateCoverBmp()) {
        LOG_ERR("SLP", "Failed to generate XTC cover bmp");
        return (this->*renderNoCoverSleepScreen)();
      }
    }
  } else if (FsHelpers::hasTxtExtension(bookPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(bookPath, "/.crosspoint");
    coverBmpPath = lastTxt.getCoverBmpPath();
    if (!Storage.exists(coverBmpPath.c_str())) {
      if (!lastTxt.load()) {
        LOG_ERR("SLP", "Failed to load last TXT");
        return (this->*renderNoCoverSleepScreen)();
      }

      if (!lastTxt.generateCoverBmp()) {
        LOG_ERR("SLP", "No cover image found for TXT file");
        return (this->*renderNoCoverSleepScreen)();
      }
    }
  } else if (FsHelpers::hasEpubExtension(bookPath)) {
    // Handle EPUB file
    Epub lastEpub(bookPath, "/.crosspoint");
    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
    if (!Storage.exists(coverBmpPath.c_str())) {
      // Skip loading CSS since we only need metadata here. This path is only
      // reached when the cover cache is genuinely missing.
      if (!lastEpub.load(true, true)) {
        LOG_ERR("SLP", "Failed to load last epub");
        return (this->*renderNoCoverSleepScreen)();
      }

      if (!lastEpub.generateCoverBmp(cropped)) {
        LOG_ERR("SLP", "Failed to generate cover bmp");
        return (this->*renderNoCoverSleepScreen)();
      }
    }
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

void SleepActivity::renderCoverOverlaySleepScreen() const {
  // Always use the most recent/current book as the background. The overlay is
  // then composited above that cover, whether sleep was entered from Reader
  // or from the bookshelf.
  renderCoverSleepScreen();
  if (!renderOverlaySleepScreen()) {
    // An invalid/opaque overlay may have drawn into RAM during validation, so
    // restore the cover before completing the sleep screen.
    renderCoverSleepScreen();
  }
}

void SleepActivity::renderReadingStatsSleepScreen() const {
  const auto book = loadSleepBookSnapshot();
  const auto library = loadSleepLibrarySnapshot();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int side = 28;

  preconditionSleepRefresh(renderer);
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 34, "READING STATS", true, EpdFontFamily::BOLD);
  renderer.drawLine(side, 54, pageWidth - side - 1, 54);

  const std::string title = renderer.truncatedText(UI_10_FONT_ID,
                                                   (book.hasBook ? book.title : "No book open").c_str(),
                                                   pageWidth - side * 2);
  renderer.drawCenteredText(UI_10_FONT_ID, 78, title.c_str(), true, EpdFontFamily::BOLD);

  char progress[40];
  snprintf(progress, sizeof(progress), "%s  ·  %u%%", sleepStatusText(book.status, book.progress), book.progress);
  renderer.drawCenteredText(SMALL_FONT_ID, 104, progress);
  const int barX = side;
  const int barY = 122;
  const int barWidth = pageWidth - side * 2;
  renderer.drawRoundedRect(barX, barY, barWidth, 16, 1, 6, true);
  if (book.progress > 0) {
    const int fillWidth = std::max(4, (barWidth - 4) * book.progress / 100);
    renderer.fillRoundedRect(barX + 2, barY + 2, fillWidth, 12, 4, Color::Black);
  }

  const int gap = 12;
  const int cardWidth = (pageWidth - side * 2 - gap) / 2;
  const int cardHeight = 90;
  const int cardsTop = 170;
  drawSleepMetric(renderer, side, cardsTop, cardWidth, cardHeight, "LAST SESSION",
                  sleepDuration(book.lastSessionSeconds));
  drawSleepMetric(renderer, side + cardWidth + gap, cardsTop, cardWidth, cardHeight, "BOOK TOTAL",
                  sleepDuration(book.readingSeconds));
  drawSleepMetric(renderer, side, cardsTop + cardHeight + gap, cardWidth, cardHeight, "TODAY",
                  sleepDuration(book.hasBook && book.todaySeconds > 0 ? book.todaySeconds : library.todaySeconds));
  drawSleepMetric(renderer, side + cardWidth + gap, cardsTop + cardHeight + gap, cardWidth, cardHeight, "SESSIONS",
                  std::to_string(book.hasBook ? book.sessions : library.sessions));

  renderer.drawLine(side, pageHeight - 92, pageWidth - side - 1, pageHeight - 92);
  char footer[80];
  snprintf(footer, sizeof(footer), "Library  %s  ·  %lu sessions  ·  %u finished", sleepDuration(library.readingSeconds).c_str(),
           static_cast<unsigned long>(library.sessions), library.finished);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 64, footer);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 36, "FOLIO NOOIR", true, EpdFontFamily::BOLD);
  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderMinimalStatsSleepScreen() const {
  const auto book = loadSleepBookSnapshot();
  const auto library = loadSleepLibrarySnapshot();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int side = 34;

  preconditionSleepRefresh(renderer);
  renderer.clearScreen();

  renderer.drawText(SMALL_FONT_ID, side, 32, "FOLIO NOOIR", true, EpdFontFamily::BOLD);
  renderer.drawLine(side, 48, pageWidth - side - 1, 48);

  const std::string title = renderer.truncatedText(UI_10_FONT_ID,
                                                   (book.hasBook ? book.title : "No book open").c_str(),
                                                   pageWidth - side * 2);
  renderer.drawCenteredText(UI_10_FONT_ID, 104, title.c_str(), true, EpdFontFamily::BOLD);

  char percent[12];
  snprintf(percent, sizeof(percent), "%u%%", book.progress);
  const int percentWidth = renderer.getTextWidth(UI_12_FONT_ID, percent, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, (pageWidth - percentWidth) / 2, 172, percent, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, 202, sleepStatusText(book.status, book.progress));

  const int barX = 90;
  const int barY = 230;
  const int barWidth = pageWidth - barX * 2;
  renderer.drawRoundedRect(barX, barY, barWidth, 18, 1, 8, true);
  if (book.progress > 0) {
    const int fillWidth = std::max(4, (barWidth - 4) * book.progress / 100);
    renderer.fillRoundedRect(barX + 2, barY + 2, fillWidth, 14, 6, Color::Black);
  }

  const int statsY = pageHeight - 172;
  const int columnWidth = (pageWidth - side * 2) / 3;
  const std::string today = sleepDuration(book.hasBook && book.todaySeconds > 0 ? book.todaySeconds : library.todaySeconds);
  const std::string total = sleepDuration(book.hasBook ? book.readingSeconds : library.readingSeconds);
  const std::string sessions = std::to_string(book.hasBook ? book.sessions : library.sessions);
  const char* labels[] = {"TODAY", "TOTAL", "SESSIONS"};
  const std::string values[] = {today, total, sessions};
  for (int i = 0; i < 3; ++i) {
    const int x = side + i * columnWidth;
    if (i > 0) renderer.drawLine(x, statsY, x, statsY + 86);
    const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, values[i].c_str(), EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, x + (columnWidth - renderer.getTextWidth(SMALL_FONT_ID, labels[i])) / 2,
                      statsY + 8, labels[i], true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + (columnWidth - valueWidth) / 2, statsY + 40, values[i].c_str(), true,
                      EpdFontFamily::BOLD);
  }
  renderer.drawImage(MoonIcon, pageWidth - MOONICON_WIDTH - 16, pageHeight - MOONICON_HEIGHT - 12, MOONICON_WIDTH,
                     MOONICON_HEIGHT);
  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderClippingCoverSleepScreen() const {
  const auto clipping = loadSleepClippingSnapshot();
  // The clipping is selected from the same current/recent book used by the
  // cover renderer, so its own cover remains behind the quote card.
  renderCoverSleepScreen(clipping.valid ? clipping.path : std::string());

  if (!clipping.valid) {
    LOG_DBG("SLP", "No saved clipping for sleep card; keeping cover/default screen");
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int panelX = 18;
  const int panelY = pageHeight - 306;
  const int panelWidth = pageWidth - panelX * 2;
  const int panelHeight = 286;
  renderer.fillRoundedRect(panelX, panelY, panelWidth, panelHeight, 12, Color::White);
  renderer.drawRoundedRect(panelX, panelY, panelWidth, panelHeight, 2, 12, true);

  renderer.drawText(SMALL_FONT_ID, panelX + 18, panelY + 24, "FROM YOUR CLIPPINGS", true, EpdFontFamily::BOLD);
  renderer.drawLine(panelX + 18, panelY + 38, panelX + panelWidth - 18, panelY + 38);

  const int quoteX = panelX + 18;
  const int quoteY = panelY + 60;
  renderer.drawText(UI_12_FONT_ID, quoteX, quoteY, "\"", true, EpdFontFamily::BOLD);
  const int textX = quoteX + 22;
  const int textWidth = panelWidth - 42;
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, clipping.text.c_str(), textWidth, 5);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (size_t i = 0; i < lines.size(); ++i) {
    renderer.drawText(UI_10_FONT_ID, textX, quoteY + static_cast<int>(i) * lineHeight, lines[i].c_str());
  }

  const int footerY = panelY + panelHeight - 42;
  renderer.drawLine(panelX + 18, footerY - 10, panelX + panelWidth - 18, footerY - 10);
  const std::string attribution = renderer.truncatedText(SMALL_FONT_ID,
                                                          (clipping.title + "  ·  page " +
                                                           std::to_string(static_cast<unsigned>(clipping.page) + 1))
                                                              .c_str(),
                                                          textWidth);
  renderer.drawText(SMALL_FONT_ID, textX, footerY, attribution.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, panelX + panelWidth - 82, footerY, "RANDOM", true, EpdFontFamily::REGULAR);
  displaySleepFrame(renderer, HalDisplay::HALF_REFRESH);
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
