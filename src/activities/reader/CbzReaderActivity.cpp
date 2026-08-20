#include "CbzReaderActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/converters/TjpgdToFramebufferConverter.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "BookStateStore.h"
#include "BookmarkEntry.h"
#include "CrossPointState.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "util/BookmarkFile.h"
#include "components/UITheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "fontIds.h"
#include "Memory.h"
#include "util/CbzDiagnostics.h"

namespace {
#ifndef NOOIR_CBZ_QUALITY_DIAGNOSTICS
#define NOOIR_CBZ_QUALITY_DIAGNOSTICS 0
#endif
constexpr size_t PROGRESS_BYTES = 4;
constexpr int MAX_ZOOM_LEVEL = 4;
constexpr float ZOOM_STEP = 0.25f;
// Keep pathological webtoon/scan dimensions bounded on X3. Normal comic
// pages are far below these limits; oversized images are fit down while still
// preserving their aspect ratio rather than allocating a full enlarged frame.
constexpr int MAX_RENDER_DIMENSION = 8192;
constexpr int64_t MAX_RENDER_PIXELS = 8ll * 1024ll * 1024ll;
// Prefetch is intentionally X4-only for now. The synchronous decoder path
// has not been validated against X3's panel timing and smaller heap, so X3
// skips it rather than taking a risk with input latency or memory pressure.
constexpr unsigned long CBZ_PREFETCH_IDLE_DELAY_MS = 900UL;
constexpr size_t CBZ_PREFETCH_MIN_FREE_HEAP = 80 * 1024;
constexpr size_t CBZ_PREFETCH_MIN_MAX_ALLOC = 32 * 1024;
constexpr uint16_t CBZ_PREFETCH_MIN_BATTERY_PERCENT = 15;
constexpr const char* VIEW_MODE_LABELS[] = {"Fit Width", "Fit Page", "Landscape", "Zoom", "Reset View",
                                            "Go to Page...", "Toggle Bookmark", "Bookmarks", "Reading Direction"};
constexpr int VIEW_MODE_LABEL_COUNT = static_cast<int>(sizeof(VIEW_MODE_LABELS) / sizeof(VIEW_MODE_LABELS[0]));
constexpr const char* READING_DIRECTION_LABELS[] = {"LTR", "RTL"};
constexpr int READING_DIRECTION_COUNT = static_cast<int>(sizeof(READING_DIRECTION_LABELS) /
                                                          sizeof(READING_DIRECTION_LABELS[0]));

const char* inputButtonName(const MappedInputManager::Button button) {
  switch (button) {
    case MappedInputManager::Button::Back:
      return "Back";
    case MappedInputManager::Button::Confirm:
      return "Confirm";
    case MappedInputManager::Button::Left:
      return "Left";
    case MappedInputManager::Button::Right:
      return "Right";
    case MappedInputManager::Button::Up:
      return "Up";
    case MappedInputManager::Button::Down:
      return "Down";
    case MappedInputManager::Button::PageBack:
      return "PageBack";
    case MappedInputManager::Button::PageForward:
      return "PageForward";
    case MappedInputManager::Button::Power:
      return "Power";
    default:
      return "input";
  }
}

const char* viewModeName(const CbzReaderActivity::ViewMode mode) {
  switch (mode) {
    case CbzReaderActivity::ViewMode::FitWidth:
      return "fit_width";
    case CbzReaderActivity::ViewMode::FitPage:
      return "fit_page";
    case CbzReaderActivity::ViewMode::Landscape:
      return "landscape";
    case CbzReaderActivity::ViewMode::Zoom:
      return "zoom";
    case CbzReaderActivity::ViewMode::SourceOneToOne:
      return "source_1to1";
  }
  return "fit_width";
}

}

void CbzReaderActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderMappingMode(true);
  readingSessionStartedMs = millis();
  prefetchReady = false;
  prefetchRunning = false;
  prefetchCancelRequested = false;
  prefetchGeneration = 0;
  prefetchPage = SIZE_MAX;
  prefetchAttemptedPage = SIZE_MAX;
  prefetchSkipLoggedPage = SIZE_MAX;
  prefetchImagePath.clear();
  prefetchCachePath.clear();
  prefetchSourceWidth = 0;
  prefetchSourceHeight = 0;
  prefetchWaitStartedMs = 0UL;
  prefetchWaitMs = 0;
  cacheOnlyCurrentPage = false;
  currentCachedWidth = 0;
  currentCachedHeight = 0;
  pendingNavigation = false;
  pendingNavigationCancelPrefetch = false;
  loadingUiPending = false;
  loadingUiRenderArmed = false;
  lastRenderCompleteMs = 0UL;

  if (!cbz) return;
  logCbzPath("cbz-activity-entry", cbz->getPath());
  cbz->setupCacheDir();
  const std::string renderCachePath = cbz->getRenderCachePath();
  if (Storage.exists(renderCachePath.c_str())) {
    logCbzCacheAction("remove", "reader_enter_reset", renderCachePath);
    Storage.remove(renderCachePath.c_str());
  }
  const std::string candidateRenderCachePaths[] = {cbz->getCachePath() + "/render.next.pxc",
                                                   cbz->getCachePath() + "/render.pending.pxc"};
  for (const auto& candidateRenderCachePath : candidateRenderCachePaths) {
    if (!Storage.exists(candidateRenderCachePath.c_str())) continue;
    logCbzCacheAction("remove", "reader_enter_reset_candidate", candidateRenderCachePath);
    Storage.remove(candidateRenderCachePath.c_str());
  }
  ImageBlock::releaseRenderCache();
  activeRenderCachePath.clear();
  forceRenderCacheRebuild = false;
  inputGateAfterRender = false;
  busyLogEmitted = false;
  busyLogButton = -1;
  staleLogEmitted = false;
  staleLogButton = -1;
  sourceOneToOneBwMode = false;
  directBwDiagnosticMode = false;
  directCropDumpRequested = false;
  directCropDumpFired = false;
  pagePickerActive = false;
  pagePickerSelection = currentPage;
  bookmarksActive = false;
  bookmarkSelection = 0;
  bookmarksLoaded = false;
  bookmarks.clear();
  readingRtl = false;
  loadProgress();
  loadBookmarks();

  const std::string path = cbz->getPath();
  APP_STATE.openEpubPath = path;
  APP_STATE.saveToFile();
  // Resolve the height template before logging or storing the shelf cover.
  // Passing the raw template through this reader used to make diagnostics and
  // the Recent record point at the literal "thumb_[HEIGHT].bmp" path.
  const std::string shelfThumb = UITheme::getCoverThumbPath(cbz->getThumbBmpPath(), FolioNooirTheme::COVER_HEIGHT);
  logCbzCacheLookup(shelfThumb, Storage.exists(shelfThumb.c_str()));
  // Keep the persistent shelf thumbnail when opening an existing CBZ. Passing
  // an empty cover path here used to replace the Recent entry's valid cover
  // metadata, so the cover vanished as soon as the reader exited and Recent
  // rebuilt its visible cards.
  RECENT_BOOKS.addBook(path, cbz->getTitle(), cbz->getAuthor(), shelfThumb, cbz->getSynopsis());
  lastPersistedPage = currentPage;
  progressDirty = false;
  requestPageRender();
}

void CbzReaderActivity::requestPageRender(const bool immediate) {
  // If the loading phase has already painted the current page, a newer input
  // request should run the real render next rather than inserting another
  // loading refresh. Navigation itself remains serialized by renderBusy and
  // the existing queued-navigation path.
  if (!loadingUiRenderArmed) loadingUiPending = true;
  requestUpdate(immediate);
}

void CbzReaderActivity::onExit() {
  Activity::onExit();
  longOperationIndicator.cancel("reader_exit");
  cancelPrefetch("navigation");
  viewModePopup.dismiss();
  directionPopup.dismiss();
  pagePickerActive = false;
  bookmarksActive = false;
  renderBusy = false;
  loadingUiPending = false;
  loadingUiRenderArmed = false;
  clearPendingNavigation("exit");
  inputGateAfterRender = false;
  mappedInput.setReaderMappingMode(false);
  renderer.setDarkMode(false);

  if (cbz) {
    // A pan/zoom/mode redraw must not rewrite progress. Persist only an actual
    // page change, including a final change made immediately before leaving.
    saveProgress();
    const ScreenshotInfo info = getScreenshotInfo();
    // Release the extracted page, pixel-cache RAM slot and render.pxc before
    // the store serializers allocate their JSON buffers.  The reader owns
    // these resources; keeping them live while several stores write can
    // exhaust/fragment the X3/X4 heap and makes teardown much more fragile.
    const std::string path = cbz->getPath();
    const uint32_t elapsedSeconds = (millis() - readingSessionStartedMs) / 1000UL;
    clearCurrentImage();
    const std::string shelfThumb = UITheme::getCoverThumbPath(cbz->getThumbBmpPath(), FolioNooirTheme::COVER_HEIGHT);
    logCbzCacheLookup(shelfThumb, Storage.exists(shelfThumb.c_str()));
    cbz.reset();
    RECENT_BOOKS.recordReading(path, static_cast<uint8_t>(info.progressPercent), elapsedSeconds,
                               sessionPagesTurned);
    BOOK_STATES.recordReading(path, static_cast<uint8_t>(info.progressPercent), elapsedSeconds,
                              sessionPagesTurned);
    READING_STATS.recordSession(halClock.getDateKey(), elapsedSeconds, sessionPagesTurned);
  }

  ReaderUtils::clearGhostingOnExit(renderer);
  if (APP_STATE.readerActivityLoadCount != 0) {
    APP_STATE.readerActivityLoadCount = 0;
    APP_STATE.saveToFile();
  }
}

void CbzReaderActivity::clearCurrentImage() {
  if (!cbz) return;
  if (!currentImagePath.empty()) logCbzCacheAction("remove", "reader_current_image", currentImagePath);
  clearRenderCache();
  if (!currentImagePath.empty()) Storage.remove(currentImagePath.c_str());
  const char* transientPageFiles[] = {"/current.jpg", "/current.jpeg", "/current.png",
                                      "/next.jpg",    "/next.jpeg",    "/next.png",
                                      "/pending.jpg", "/pending.jpeg", "/pending.png"};
  for (const char* suffix : transientPageFiles) {
    const std::string path = cbz->getCachePath() + suffix;
    if (Storage.exists(path.c_str())) {
      logCbzCacheAction("remove", "reader_current_cleanup", path);
      Storage.remove(path.c_str());
    }
  }
  currentImagePath.clear();
  renderedImagePage = SIZE_MAX;
  activeRenderCachePath.clear();
  forceRenderCacheRebuild = false;
  cacheOnlyCurrentPage = false;
  currentCachedWidth = 0;
  currentCachedHeight = 0;
  currentMaxPanX = 0;
  currentMaxPanY = 0;
  zoomLongPressFired = false;
}

void CbzReaderActivity::clearRenderCache() {
  if (!cbz) return;
  ImageBlock::releaseRenderCache();
  const std::string paths[] = {activeRenderCachePath, cbz->getRenderCachePath(),
                               cbz->getCachePath() + "/render.next.pxc",
                               cbz->getCachePath() + "/render.pending.pxc"};
  for (const auto& path : paths) {
    if (path.empty() || !Storage.exists(path.c_str())) continue;
    logCbzCacheAction("remove", "reader_render_cache", path);
    Storage.remove(path.c_str());
  }
  activeRenderCachePath.clear();
}

void CbzReaderActivity::cancelPrefetch(const char* reason) {
  if (!cbz) return;
  const char* cancelReason = reason ? reason : "stale";
  if (prefetchRunning) {
    prefetchCancelRequested = true;
    LOG_DBG("CBZPREFETCH", "hardware=%s state=cancel_requested page=%lu generation=%lu reason=%s",
            gpio.deviceIsX3() ? "X3" : "X4",
            prefetchPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(prefetchPage + 1),
            static_cast<unsigned long>(prefetchGeneration), cancelReason);
    // The synchronous decoder owns the staging paths until it returns.  Do
    // not delete or replace anything here; the owner releases them at the
    // next cancellation checkpoint before a foreground render can proceed.
    return;
  }
  if (!prefetchReady && prefetchImagePath.empty() && prefetchCachePath.empty()) return;

  const size_t cancelledPage = prefetchPage;
  const uint32_t cancelledGeneration = prefetchGeneration;
  LOG_DBG("CBZPREFETCH", "hardware=%s state=cancel page=%lu generation=%lu reason=%s",
          gpio.deviceIsX3() ? "X3" : "X4",
          cancelledPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(cancelledPage + 1),
          static_cast<unsigned long>(cancelledGeneration), cancelReason);

  if (!prefetchCachePath.empty() && prefetchCachePath != activeRenderCachePath &&
      Storage.exists(prefetchCachePath.c_str())) {
    logCbzCacheAction("remove", "prefetch_cancel", prefetchCachePath);
    Storage.remove(prefetchCachePath.c_str());
  }
  if (!prefetchImagePath.empty() && prefetchImagePath != currentImagePath &&
      Storage.exists(prefetchImagePath.c_str())) {
    logCbzCacheAction("remove", "prefetch_cancel", prefetchImagePath);
    Storage.remove(prefetchImagePath.c_str());
  }
  prefetchReady = false;
  prefetchPage = SIZE_MAX;
  prefetchImagePath.clear();
  prefetchCachePath.clear();
  prefetchSourceWidth = 0;
  prefetchSourceHeight = 0;
  prefetchCancelRequested = false;
  LOG_DBG("CBZPREFETCH", "hardware=%s state=cancelled page=%lu generation=%lu owner_released=1",
          gpio.deviceIsX3() ? "X3" : "X4",
          cancelledPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(cancelledPage + 1),
          static_cast<unsigned long>(cancelledGeneration));
}

bool CbzReaderActivity::isQueueableNavigationButton(const MappedInputManager::Button button) const {
  if (button == MappedInputManager::Button::Power &&
      SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::PAGE_TURN) {
    return false;
  }
  if (viewMode == ViewMode::Landscape) {
    return button == MappedInputManager::Button::Left || button == MappedInputManager::Button::Right ||
           button == MappedInputManager::Button::Up || button == MappedInputManager::Button::Down ||
           button == MappedInputManager::Button::Power ||
           button == MappedInputManager::Button::PageBack || button == MappedInputManager::Button::PageForward;
  }
  if (viewMode == ViewMode::FitWidth || viewMode == ViewMode::FitPage) {
    return button == MappedInputManager::Button::Left || button == MappedInputManager::Button::Right ||
           button == MappedInputManager::Button::PageBack || button == MappedInputManager::Button::PageForward ||
           button == MappedInputManager::Button::Power;
  }
  return false;
}

bool CbzReaderActivity::isLogicalNextButton(const MappedInputManager::Button button) const {
  if (viewMode == ViewMode::Landscape) {
    if (button == MappedInputManager::Button::Down || button == MappedInputManager::Button::PageForward ||
        button == MappedInputManager::Button::Power) {
      return true;
    }
    // In Landscape, Left/Right pan the image-local vertical axis until an
    // edge is reached. At an edge the same physical button becomes a page
    // turn, following the selected reading direction.
    if (button == MappedInputManager::Button::Left && panY <= 0) return readingRtl;
    if (button == MappedInputManager::Button::Right && panY >= currentMaxPanY) return !readingRtl;
    return false;
  }
  if (button == MappedInputManager::Button::PageForward || button == MappedInputManager::Button::Power) return true;
  if (button == MappedInputManager::Button::Left || button == MappedInputManager::Button::Right) {
    // ReaderUtils applies the same front-button swap when it detects a page
    // turn. Keep queued navigation consistent with that mapping.
    const bool swapped = mappedInput.isNavDirectionSwapped();
    return swapped ? button == MappedInputManager::Button::Left : button == MappedInputManager::Button::Right;
  }
  return false;
}

void CbzReaderActivity::logPageIntent(const bool next) const {
  size_t target = currentPage;
  if (cbz && cbz->getPageCount() > 0) {
    if (readingRtl) {
      if (next) {
        if (currentPage > 0) target = currentPage - 1;
      } else if (currentPage + 1 < cbz->getPageCount()) {
        target = currentPage + 1;
      }
    } else if (next) {
      if (currentPage + 1 < cbz->getPageCount()) target = currentPage + 1;
    } else if (currentPage > 0) {
      target = currentPage - 1;
    }
  }
  LOG_DBG("CBZUI", "intent=%s mode=%s from=%lu to=%lu", next ? "page_next" : "page_previous",
          viewModeName(viewMode), static_cast<unsigned long>(currentPage + 1),
          static_cast<unsigned long>(target + 1));
}

void CbzReaderActivity::queueNavigationButton(const MappedInputManager::Button button, const char* state) {
  if (!isQueueableNavigationButton(button)) return;
  if (pendingNavigation) {
    // One physical press is enough to express the user's intent while a
    // render is busy. Do not let a held/repeating button skip extra pages.
    LOG_DBG("CBZUI", "input_collapsed button=%s", inputButtonName(button));
    return;
  }
  const char* prefetchState = prefetchRunning ? "running" : (prefetchReady ? "done" : "none");
  LOG_DBG("CBZUI", "input_accepted button=%s busy=%d prefetch=%s", inputButtonName(button), renderBusy ? 1 : 0,
          prefetchState);
  pendingNavigation = true;
  // Any user navigation, including a Landscape pan, invalidates a lookahead
  // transaction. A foreground action must never share the prefetch owner.
  pendingNavigationCancelPrefetch = true;
  pendingNavigationButton = button;
  pendingNavigationViewMode = viewMode;
  LOG_DBG("CBZCTRL", "queued=%s button=%s", state ? state : "busy", inputButtonName(button));
  const bool pageIntent = viewMode != ViewMode::Landscape ||
                          (button != MappedInputManager::Button::Left &&
                           button != MappedInputManager::Button::Right) ||
                          (button == MappedInputManager::Button::Left && panY <= 0) ||
                          (button == MappedInputManager::Button::Right && panY >= currentMaxPanY);
  const bool nextIntent = isLogicalNextButton(button);
  LOG_DBG("CBZUI", "queued=%s", pageIntent ? (nextIntent ? "page_next" : "page_previous")
                                              : (button == MappedInputManager::Button::Left ? "pan_left"
                                                                                             : "pan_right"));
  LOG_DBG("CBZUI", "input_queued button=%s", inputButtonName(button));

  // A queued page turn should acknowledge the press on the next safe event
  // loop pass, before extraction/decode/cache replay begins. Landscape
  // Left/Right remain on the fast redraw path only while they still have
  // content to pan; an edge press is a real page turn.
  const bool landscapeHorizontal = viewMode == ViewMode::Landscape &&
                                   (button == MappedInputManager::Button::Left ||
                                    button == MappedInputManager::Button::Right);
  const bool landscapePan = landscapeHorizontal &&
                            !((button == MappedInputManager::Button::Left && panY <= 0) ||
                              (button == MappedInputManager::Button::Right && panY >= currentMaxPanY));
  if (!landscapePan) requestPageRender(true);
}

void CbzReaderActivity::clearPendingNavigation(const char* reason) {
  if (pendingNavigation) {
    LOG_DBG("CBZCTRL", "dropped=%s", reason ? reason : "stale");
  }
  pendingNavigation = false;
  pendingNavigationCancelPrefetch = false;
}

void CbzReaderActivity::executePendingNavigation() {
  if (!pendingNavigation) return;
  if (pendingNavigationViewMode != viewMode) {
    clearPendingNavigation("stale");
    return;
  }

  const auto button = pendingNavigationButton;
  const bool cancelLookahead = pendingNavigationCancelPrefetch;
  pendingNavigation = false;
  pendingNavigationCancelPrefetch = false;
  const bool logicalNextButton = isLogicalNextButton(button);
  const size_t queuedTarget = logicalNextButton ? (readingRtl ? (currentPage == 0 ? SIZE_MAX : currentPage - 1)
                                                             : currentPage + 1)
                                                 : (readingRtl ? currentPage + 1
                                                               : (currentPage == 0 ? SIZE_MAX : currentPage - 1));
  const bool matchingPrefetch = logicalNextButton && queuedTarget != SIZE_MAX &&
                                (prefetchReady || prefetchRunning) && prefetchPage == queuedTarget &&
                                prefetchViewMode == viewMode && prefetchZoomLevel == zoomLevel;
  const bool preserveMatchingPrefetch = matchingPrefetch && prefetchReady;
  if (matchingPrefetch && prefetchRunning) {
    // Keep the request queued while the matching serialized lookahead owner
    // finishes. It is the same page the user requested, so cancelling it would
    // throw away the work and force a duplicate foreground decode.
    pendingNavigation = true;
    pendingNavigationCancelPrefetch = false;
    pendingNavigationButton = button;
    pendingNavigationViewMode = viewMode;
    if (prefetchWaitStartedMs == 0UL) {
      prefetchWaitStartedMs = millis();
      LOG_DBG("CBZPREFETCH", "state=wait_for_use page=%lu generation=%lu",
              static_cast<unsigned long>(queuedTarget + 1), static_cast<unsigned long>(prefetchGeneration));
    }
    requestPageRender(true);
    return;
  }
  if (cancelLookahead && !preserveMatchingPrefetch) {
    cancelPrefetch("navigation");
    if (prefetchRunning) {
      // The prefetch decoder owns its staging files until its cancellation
      // checkpoint. Keep the request queued rather than entering foreground
      // rendering against those files.
      pendingNavigation = true;
      pendingNavigationCancelPrefetch = true;
      pendingNavigationButton = button;
      pendingNavigationViewMode = viewMode;
      LOG_DBG("CBZUI", "waiting_for_prefetch_release page=%lu",
              static_cast<unsigned long>(currentPage + 1));
      return;
    }
  }

  if (!prefetchRunning && prefetchWaitStartedMs != 0UL) {
    prefetchWaitMs += millis() - prefetchWaitStartedMs;
    prefetchWaitStartedMs = 0UL;
  }

  const bool pageIntent = viewMode != ViewMode::Landscape ||
                          (button != MappedInputManager::Button::Left &&
                           button != MappedInputManager::Button::Right) ||
                          (button == MappedInputManager::Button::Left && panY <= 0) ||
                          (button == MappedInputManager::Button::Right && panY >= currentMaxPanY);
  const bool nextIntent = isLogicalNextButton(button);
  LOG_DBG("CBZCTRL", "executing_queued=%s", inputButtonName(button));
  LOG_DBG("CBZUI", "executing=%s", pageIntent ? (nextIntent ? "page_next" : "page_previous")
                                                   : (button == MappedInputManager::Button::Left ? "pan_left"
                                                                                                  : "pan_right"));
  LOG_DBG("CBZUI", "navigation_executing button=%s", inputButtonName(button));
  if (viewMode == ViewMode::Landscape) {
    if (button == MappedInputManager::Button::Left || button == MappedInputManager::Button::Right) {
      handleLandscapeHorizontalNavigation(button);
      return;
    }
    const bool previous = !logicalNextButton;
    logPageIntent(!previous);
    handlePageTurn(previous, false);
    return;
  }
  if (viewMode == ViewMode::FitWidth || viewMode == ViewMode::FitPage) {
    const bool previous = !logicalNextButton;
    logPageIntent(!previous);
    // Fit modes always turn pages. Do not let handlePageTurn's optional
    // vertical-scroll resolution consume Next/Previous as a same-page pan.
    handlePageTurn(previous, false);
  } else {
    clearPendingNavigation("stale");
  }
}

bool CbzReaderActivity::renderNextPageToCache(const size_t page, const std::string& imagePath,
                                              const std::string& cachePath, size_t& freeBefore,
                                              size_t& freeAfter, size_t& maxAllocBefore,
                                              size_t& maxAllocAfter) {
  freeBefore = ESP.getFreeHeap();
  maxAllocBefore = ESP.getMaxAllocHeap();
  freeAfter = freeBefore;
  maxAllocAfter = maxAllocBefore;
  if (!cbz || page >= cbz->getPageCount() || imagePath.empty() || cachePath.empty()) return false;

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  ImageDimensions dimensions{};
  if (!decoder || !decoder->getDimensions(imagePath, dimensions) || dimensions.width <= 0 || dimensions.height <= 0) {
    return false;
  }
  prefetchSourceWidth = dimensions.width;
  prefetchSourceHeight = dimensions.height;

  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int statusHeight = UITheme::getStatusBarHeight();
  const int availableWidth = std::max(1, safe.width);
  const int availableHeight = std::max(1, safe.height - statusHeight);
  const float fitWidth = static_cast<float>(availableWidth) / dimensions.width;
  const float fitHeight = static_cast<float>(availableHeight) / dimensions.height;
  float scale = std::min(fitWidth, fitHeight);
  if (viewMode == ViewMode::FitWidth) {
    scale = std::min(1.5f, fitWidth);
  } else if (viewMode == ViewMode::FitPage) {
    scale = std::min(scale, 1.0f);
  } else {
    return false;
  }

  int targetWidth = std::max(1, static_cast<int>(std::floor(dimensions.width * scale)));
  int targetHeight = std::max(1, static_cast<int>(std::floor(dimensions.height * scale)));
  const int64_t targetPixels = static_cast<int64_t>(targetWidth) * targetHeight;
  if (targetWidth > MAX_RENDER_DIMENSION || targetHeight > MAX_RENDER_DIMENSION || targetPixels > MAX_RENDER_PIXELS) {
    const float dimensionScale = std::min(static_cast<float>(MAX_RENDER_DIMENSION) / targetWidth,
                                          static_cast<float>(MAX_RENDER_DIMENSION) / targetHeight);
    const float pixelScale = std::sqrt(static_cast<float>(MAX_RENDER_PIXELS) /
                                       static_cast<float>(std::max<int64_t>(1, targetPixels)));
    scale *= std::min(1.0f, std::min(dimensionScale, pixelScale));
    targetWidth = std::max(1, static_cast<int>(std::floor(dimensions.width * scale)));
    targetHeight = std::max(1, static_cast<int>(std::floor(dimensions.height * scale)));
  }

  RenderConfig config;
  // Keep all decoded pixels off the visible framebuffer. CBZ cache coordinates
  // are image-local when cbzQualityMode is enabled, so the negative origin
  // affects only the DirectPixelWriter and never the .pxc payload.
  config.x = -targetWidth - 1;
  config.y = -targetHeight - 1;
  config.maxWidth = targetWidth;
  config.maxHeight = targetHeight;
  config.useGrayscale = true;
  config.useDithering = false;
  config.performanceMode = false;
  config.useExactDimensions = true;
  config.cbzQualityMode = true;
  config.cachePath = cachePath;

  const bool useTjpgd = strcmp(decoder->getFormatName(), "JPEG") == 0 &&
                        TjpgdToFramebufferConverter::requiresFallback(imagePath);
  TjpgdToFramebufferConverter tjpgd;
  bool rendered = false;
  const auto previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  ImageBlock::releaseRenderCache();
  if (useTjpgd) {
    rendered = tjpgd.decodeToFramebuffer(imagePath, renderer, config);
  } else {
    rendered = decoder->decodeToFramebuffer(imagePath, renderer, config);
  }
  renderer.setOrientation(previousOrientation);
  ImageBlock::releaseRenderCache();
  freeAfter = ESP.getFreeHeap();
  maxAllocAfter = ESP.getMaxAllocHeap();
  if (!rendered || !Storage.exists(cachePath.c_str())) {
    if (Storage.exists(cachePath.c_str())) Storage.remove(cachePath.c_str());
    return false;
  }
  return true;
}

void CbzReaderActivity::maybePrefetchNextPage() {
  const auto logSkip = [&](const char* reason, const bool permanent) {
    if (prefetchSkipLoggedPage != currentPage) {
      LOG_DBG("CBZPREFETCH", "hardware=%s state=skip reason=%s", gpio.deviceIsX3() ? "X3" : "X4", reason);
      prefetchSkipLoggedPage = currentPage;
    }
    if (permanent) prefetchAttemptedPage = currentPage;
  };
  const auto logBlocked = [&](const char* reason) {
    if (prefetchSkipLoggedPage != currentPage) {
      LOG_DBG("CBZPREFETCH", "hardware=%s state=blocked reason=%s", gpio.deviceIsX3() ? "X3" : "X4", reason);
      prefetchSkipLoggedPage = currentPage;
    }
  };

  if (!cbz || pageLoadFailed || prefetchReady) return;
  if (prefetchRunning || renderBusy || loadingUiPending || loadingUiRenderArmed || pendingNavigation) {
    logBlocked("foreground_render");
    return;
  }
  if (viewModePopup.isActive() || pagePickerActive || directionPopup.isActive()) {
    logSkip("busy", false);
    return;
  }
  if (prefetchAttemptedPage == currentPage) return;
  if (lastRenderCompleteMs == 0 || millis() - lastRenderCompleteMs < CBZ_PREFETCH_IDLE_DELAY_MS) return;

  if (gpio.deviceIsX3()) {
    logSkip("unsupported", true);
    return;
  }
  if (viewMode != ViewMode::FitWidth && viewMode != ViewMode::FitPage) {
    logSkip("unsupported", true);
    return;
  }
  const bool hasForwardPage = readingRtl ? currentPage > 0 : currentPage + 1 < cbz->getPageCount();
  if (!hasForwardPage) {
    logSkip("boundary", true);
    return;
  }
  if (!hasRenderedRefreshState || inputGateAfterRender || mappedInput.wasAnyPressed() ||
      mappedInput.wasAnyReleased()) {
    logSkip("input", false);
    return;
  }

  // The main loop lowers the CPU frequency between activity iterations. Keep
  // the low-power guard, but use the existing RAII power lock for this one
  // bounded X4 decode so an idle reader can actually perform the lookahead.
  // The lock is scoped to this function and never changes refresh behavior.
  const uint16_t battery = powerManager.getBatteryPercentage();
  if (battery > 0 && battery < CBZ_PREFETCH_MIN_BATTERY_PERCENT) {
    logSkip("power", true);
    return;
  }

  std::unique_ptr<HalPowerManager::Lock> prefetchPowerLock;
  if (getCpuFrequencyMhz() <= 40) {
    // Wake only when the idle loop actually lowered the CPU. If another lock
    // owns the power manager, or allocation fails, retry later without
    // entering a decode loop.
    prefetchPowerLock = makeUniqueNoThrow<HalPowerManager::Lock>();
    if (!prefetchPowerLock || getCpuFrequencyMhz() <= 40) {
      logSkip("power", false);
      return;
    }
  }

  // A same-page pan keeps the bounded pixel-cache payload resident to avoid
  // repeated SD reads. Release that optional slot immediately before the
  // lookahead so the prefetch's own decoder gets the full measured headroom;
  // page/mode transitions and reader exit already release it as well.
  ImageBlock::releaseRenderCache();
  const size_t freeBefore = ESP.getFreeHeap();
  const size_t maxAllocBefore = ESP.getMaxAllocHeap();
  if (freeBefore < CBZ_PREFETCH_MIN_FREE_HEAP || maxAllocBefore < CBZ_PREFETCH_MIN_MAX_ALLOC) {
    logSkip("memory", true);
    return;
  }

  const size_t nextPage = readingRtl ? currentPage - 1 : currentPage + 1;
  const std::string_view entry = cbz->getPageEntry(nextPage);
  const size_t dot = entry.find_last_of('.');
  if (dot == std::string_view::npos) {
    logSkip("unsupported", true);
    return;
  }
  std::string extension(entry.substr(dot));
  std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  // Give every lookahead its own staging names.  Foreground page transitions
  // can therefore never reopen, truncate, or promote the file being written by
  // a still-running prefetch operation.
  const uint32_t generation = ++prefetchGeneration;
  const std::string generationName = std::to_string(static_cast<unsigned long>(generation));
  const std::string imagePath = cbz->getCachePath() + "/prefetch_" + generationName + extension;
  const std::string cachePath = cbz->getCachePath() + "/render.prefetch_" + generationName + ".pxc";

  prefetchAttemptedPage = currentPage;
  prefetchPage = nextPage;
  prefetchRunning = true;
  prefetchCancelRequested = false;
  const size_t operationPage = nextPage;
  const size_t operationCurrentPage = currentPage;
  const ViewMode operationViewMode = viewMode;
  const uint8_t operationZoomLevel = zoomLevel;
  const unsigned long startedMs = millis();
  LOG_DBG("CBZPREFETCH", "hardware=X4 state=start page=%lu generation=%lu freeBefore=%lu maxAllocBefore=%lu",
          static_cast<unsigned long>(nextPage + 1), static_cast<unsigned long>(generation),
          static_cast<unsigned long>(freeBefore), static_cast<unsigned long>(maxAllocBefore));

  const auto cancelled = [&]() {
    return prefetchCancelRequested || currentPage != operationCurrentPage ||
           viewMode != operationViewMode || zoomLevel != operationZoomLevel || pendingNavigation ||
           loadingUiPending || loadingUiRenderArmed || viewModePopup.isActive() || pagePickerActive ||
           directionPopup.isActive() || mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased();
  };
  const auto releaseCandidate = [&](const bool keep, const char* failureReason) {
    const bool wasCancelled = prefetchCancelRequested;
    if (!keep) {
      if (Storage.exists(cachePath.c_str())) {
        logCbzCacheAction("remove", wasCancelled ? "prefetch_cancel" : "prefetch_failed", cachePath);
        Storage.remove(cachePath.c_str());
      }
      if (Storage.exists(imagePath.c_str())) {
        logCbzCacheAction("remove", wasCancelled ? "prefetch_cancel" : "prefetch_failed", imagePath);
        Storage.remove(imagePath.c_str());
      }
      prefetchReady = false;
      prefetchPage = SIZE_MAX;
      prefetchImagePath.clear();
      prefetchCachePath.clear();
      prefetchSourceWidth = 0;
      prefetchSourceHeight = 0;
      if (wasCancelled) {
        LOG_DBG("CBZPREFETCH", "hardware=X4 state=cancelled page=%lu generation=%lu owner_released=1",
                static_cast<unsigned long>(operationPage + 1), static_cast<unsigned long>(generation));
      } else if (failureReason) {
        LOG_DBG("CBZPREFETCH", "hardware=X4 state=skip reason=%s", failureReason);
      }
    }
    // Only release the serialized owner after all staging files have been
    // closed/removed. A foreground render cannot enter during this cleanup.
    renderBusy = false;
    prefetchRunning = false;
    prefetchCancelRequested = false;
  };

  // Recheck immediately before extraction/decode.  A foreground request that
  // arrived after the idle check must win without sharing any staging path.
  if (cancelled()) {
    releaseCandidate(false, "stale");
    return;
  }

  renderBusy = true;
  const bool extracted = cbz->extractPageTo(nextPage, imagePath);
  bool rendered = false;
  size_t decodeFreeBefore = freeBefore;
  size_t decodeFreeAfter = freeBefore;
  size_t decodeMaxBefore = maxAllocBefore;
  size_t decodeMaxAfter = maxAllocBefore;
  if (extracted && !cancelled()) {
    rendered = renderNextPageToCache(nextPage, imagePath, cachePath, decodeFreeBefore, decodeFreeAfter,
                                     decodeMaxBefore, decodeMaxAfter);
  }
  if (!rendered) {
    releaseCandidate(false, prefetchCancelRequested ? "stale" : (extracted ? "decode" : "extract"));
    return;
  }

  // Recheck immediately before publication.  A cancelled/stale candidate is
  // never promoted over the active page cache.
  if (cancelled()) {
    releaseCandidate(false, "stale");
    return;
  }

  prefetchReady = true;
  prefetchPage = nextPage;
  prefetchViewMode = viewMode;
  prefetchZoomLevel = zoomLevel;
  prefetchImagePath = imagePath;
  prefetchCachePath = cachePath;
  size_t cacheBytes = 0;
  HalFile cacheFile;
  if (Storage.openFileForRead("CBZPREFETCH", cachePath, cacheFile)) {
    cacheBytes = cacheFile.size();
    cacheFile.close();
  }
  // Keep the serialized owner active until the staging file has been closed.
  // A concurrent foreground request may cancel during this final window; in
  // that case retire the candidate instead of publishing it as ready.
  if (prefetchCancelRequested) {
    releaseCandidate(false, "stale");
    return;
  }
  renderBusy = false;
  prefetchRunning = false;
  prefetchCancelRequested = false;
  LOG_DBG("CBZPREFETCH", "hardware=X4 state=done page=%lu generation=%lu duration=%lums freeAfter=%lu maxAllocAfter=%lu cacheBytes=%lu",
          static_cast<unsigned long>(nextPage + 1), static_cast<unsigned long>(generation),
          static_cast<unsigned long>(millis() - startedMs),
          static_cast<unsigned long>(decodeFreeAfter), static_cast<unsigned long>(decodeMaxAfter),
          static_cast<unsigned long>(cacheBytes));
}

void CbzReaderActivity::openViewModePopup() {
  const int selected = static_cast<int>(viewMode);
  const int popupIndex = selected >= 0 && selected < VIEW_MODE_LABEL_COUNT ? selected : 0;
  viewModePopup.show("Reader Menu", VIEW_MODE_LABELS, VIEW_MODE_LABEL_COUNT, popupIndex, [this](const int index) {
    switch (index) {
      case 0:
      case 1:
      case 2:
      case 3:
        applyViewMode(index);
        break;
      case 4:
        resetView();
        break;
      case 5:
        openPagePicker();
        break;
      case 6:
        toggleBookmark();
        break;
      case 7:
        openBookmarks();
        break;
      case 8:
        openDirectionPopup();
        break;
      default:
        break;
    }
  });
  LOG_DBG("CBZMODE", "current=%s selected=menu render=%s", viewModeName(viewMode),
          bwDiagnosticMode ? "bw_diagnostic" : "gray4");
  requestUpdate();
}

void CbzReaderActivity::resetView() {
  const bool changed = viewMode != ViewMode::FitWidth || zoomLevel != 0 || panX != 0 || panY != 0 ||
                       sourceOneToOneBwMode || directBwDiagnosticMode || bwDiagnosticMode;
  if (!changed) return;

  cancelPrefetch("mode_change");
  viewMode = ViewMode::FitWidth;
  zoomLevel = 0;
  panX = 0;
  panY = 0;
  restorePreviousPageBottom = false;
  sourceOneToOneBwMode = false;
  directBwDiagnosticMode = false;
  directCropDumpRequested = false;
  directCropDumpFired = false;
  bwDiagnosticMode = false;
  pageLoadFailed = false;
  ImageBlock::releaseRenderCache();
  if (cacheOnlyCurrentPage) {
    currentImagePath.clear();
    renderedImagePage = SIZE_MAX;
    cacheOnlyCurrentPage = false;
    currentCachedWidth = 0;
    currentCachedHeight = 0;
  }
  forceRenderCacheRebuild = true;
  LOG_DBG("CBZMODE", "current=fit_width selected=reset changed=1");
  requestPageRender(true);
}

void CbzReaderActivity::openPagePicker() {
  if (!cbz || cbz->getPageCount() == 0) return;
  pagePickerSelection = std::min(currentPage, cbz->getPageCount() - 1);
  pagePickerActive = true;
  requestUpdate();
}

void CbzReaderActivity::handlePagePickerInput() {
  if (!cbz || cbz->getPageCount() == 0) {
    pagePickerActive = false;
    requestUpdate();
    return;
  }

  const size_t pageCount = cbz->getPageCount();
  const auto moveSelection = [this, pageCount](const int delta) {
    const int64_t current = static_cast<int64_t>(pagePickerSelection);
    const int64_t maximum = static_cast<int64_t>(pageCount - 1);
    pagePickerSelection = static_cast<size_t>(std::clamp<int64_t>(current + delta, 0, maximum));
    requestUpdate();
  };

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    pagePickerActive = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveSelection(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveSelection(1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveSelection(-10);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveSelection(10);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    moveSelection(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    moveSelection(-1);
    return;
  }

  if (!mappedInput.wasPressed(MappedInputManager::Button::Confirm)) return;

  const size_t selectedPage = std::min(pagePickerSelection, pageCount - 1);
  pagePickerActive = false;
  if (selectedPage == currentPage) {
    requestUpdate();
    return;
  }

  clearPendingNavigation("stale");
  cancelPrefetch("navigation");
  prefetchAttemptedPage = SIZE_MAX;
  prefetchSkipLoggedPage = SIZE_MAX;
  currentPage = selectedPage;
  panX = 0;
  panY = 0;
  restorePreviousPageBottom = false;
  pageLoadFailed = false;
  progressDirty = true;
  requestPageRender(true);
}

void CbzReaderActivity::renderPagePicker() {
  renderer.clearScreen();

  auto& theme = UITheme::getInstance();
  const auto metrics = theme.getMetrics();
  const Rect screen = theme.getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 "Go to Page");

  const size_t pageCount = cbz ? cbz->getPageCount() : 0;
  const size_t selectedPage = pageCount == 0 ? 0 : std::min(pagePickerSelection, pageCount - 1);
  char summary[64];
  snprintf(summary, sizeof(summary), "Page %lu / %lu", static_cast<unsigned long>(selectedPage + 1),
           static_cast<unsigned long>(pageCount));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  renderer.drawCenteredText(UI_10_FONT_ID, contentTop, summary, true, EpdFontFamily::BOLD);
  const int listTop = contentTop + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;
  const int listHeight = std::max(renderer.getLineHeight(UI_10_FONT_ID), screen.y + screen.height - listTop - footerHeight);
  GUI.drawList(renderer, Rect{screen.x, listTop, screen.width, listHeight}, static_cast<int>(pageCount),
               static_cast<int>(selectedPage), [](const int index) { return std::to_string(index + 1); });

  renderer.drawCenteredText(SMALL_FONT_ID, screen.y + screen.height - footerHeight / 2,
                            "Left/Right: +/-10", true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void CbzReaderActivity::loadBookmarks() {
  bookmarks.clear();
  bookmarksLoaded = true;
  bookmarkSelection = 0;
  if (!cbz) return;

  std::vector<BookmarkEntry> stored;
  if (!BookmarkFile::load(cbz->getPath(), stored)) return;

  const size_t pageCount = cbz->getPageCount();
  for (const auto& entry : stored) {
    // CBZ bookmarks use only the compact computed page fields. Ignore entries
    // from malformed/old files rather than allowing an out-of-range jump.
    const size_t page = entry.computedSpineIndex;
    if (page >= pageCount) continue;
    if (std::find_if(bookmarks.begin(), bookmarks.end(),
                     [page](const BookmarkEntry& existing) {
                       return existing.computedSpineIndex == page;
                     }) != bookmarks.end()) {
      continue;
    }
    BookmarkEntry normalized = entry;
    normalized.computedChapterPageCount =
        static_cast<uint16_t>(std::min<size_t>(pageCount, UINT16_MAX));
    normalized.computedChapterProgress = static_cast<uint16_t>(page);
    normalized.xpath.clear();
    normalized.summary.clear();
    bookmarks.push_back(std::move(normalized));
  }
  std::sort(bookmarks.begin(), bookmarks.end(), [](const BookmarkEntry& lhs, const BookmarkEntry& rhs) {
    return lhs.computedSpineIndex < rhs.computedSpineIndex;
  });
  LOG_DBG("CBZBKM", "Loaded %lu bookmarks", static_cast<unsigned long>(bookmarks.size()));
}

void CbzReaderActivity::saveBookmarks() {
  if (!cbz) return;
  if (!BookmarkFile::save(cbz->getPath(), bookmarks)) {
    LOG_ERR("CBZBKM", "Failed to save bookmarks for %s", cbz->getPath().c_str());
    return;
  }
  LOG_DBG("CBZBKM", "Saved %lu bookmarks", static_cast<unsigned long>(bookmarks.size()));
}

bool CbzReaderActivity::isCurrentPageBookmarked() const {
  return std::find_if(bookmarks.begin(), bookmarks.end(), [this](const BookmarkEntry& entry) {
           return entry.computedSpineIndex == currentPage;
         }) != bookmarks.end();
}

void CbzReaderActivity::toggleBookmark() {
  if (!cbz || cbz->getPageCount() == 0) return;
  if (!bookmarksLoaded) loadBookmarks();

  const auto existing = std::find_if(bookmarks.begin(), bookmarks.end(), [this](const BookmarkEntry& entry) {
    return entry.computedSpineIndex == currentPage;
  });
  if (existing != bookmarks.end()) {
    bookmarks.erase(existing);
    LOG_DBG("CBZBKM", "Removed page=%lu", static_cast<unsigned long>(currentPage + 1));
  } else {
    BookmarkEntry entry;
    entry.percentage = cbz->getPageCount() > 0
                           ? static_cast<float>(currentPage + 1) / static_cast<float>(cbz->getPageCount())
                           : 0.0f;
    entry.computedSpineIndex = static_cast<uint16_t>(std::min<size_t>(currentPage, UINT16_MAX));
    entry.computedChapterPageCount =
        static_cast<uint16_t>(std::min<size_t>(cbz->getPageCount(), UINT16_MAX));
    entry.computedChapterProgress = entry.computedSpineIndex;
    auto insertAt = std::lower_bound(bookmarks.begin(), bookmarks.end(), entry.computedSpineIndex,
                                     [](const BookmarkEntry& item, const uint16_t page) {
                                       return item.computedSpineIndex < page;
                                     });
    bookmarks.insert(insertAt, std::move(entry));
    LOG_DBG("CBZBKM", "Added page=%lu", static_cast<unsigned long>(currentPage + 1));
  }
  saveBookmarks();
  requestUpdate();
}

void CbzReaderActivity::openBookmarks() {
  if (!cbz) return;
  if (!bookmarksLoaded) loadBookmarks();
  bookmarkSelection = bookmarks.empty() ? 0 : std::min(bookmarkSelection, bookmarks.size() - 1);
  bookmarkLongPressFired = false;
  bookmarksActive = true;
  requestUpdate();
}

void CbzReaderActivity::handleBookmarksInput() {
  if (!cbz) {
    bookmarksActive = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    bookmarksActive = false;
    bookmarkLongPressFired = false;
    requestUpdate();
    return;
  }

  const auto moveSelection = [this](const int delta) {
    if (bookmarks.empty()) return;
    const int64_t current = static_cast<int64_t>(bookmarkSelection);
    const int64_t maximum = static_cast<int64_t>(bookmarks.size() - 1);
    bookmarkSelection = static_cast<size_t>(std::clamp<int64_t>(current + delta, 0, maximum));
    requestUpdate();
  };
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveSelection(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveSelection(1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveSelection(-10);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveSelection(10);
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    moveSelection(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    moveSelection(-1);
    return;
  }

  // A long Confirm removes the selected bookmark; a short release opens it.
  // Removal is handled once per hold so a held button cannot delete several.
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= ReaderUtils::SKIP_HOLD_MS && !bookmarkLongPressFired) {
    bookmarkLongPressFired = true;
    if (!bookmarks.empty() && bookmarkSelection < bookmarks.size()) {
      const size_t removedPage = bookmarks[bookmarkSelection].computedSpineIndex;
      bookmarks.erase(bookmarks.begin() + bookmarkSelection);
      saveBookmarks();
      if (bookmarkSelection >= bookmarks.size() && !bookmarks.empty()) bookmarkSelection = bookmarks.size() - 1;
      LOG_DBG("CBZBKM", "Removed page=%lu from list", static_cast<unsigned long>(removedPage + 1));
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool wasLong = bookmarkLongPressFired;
    bookmarkLongPressFired = false;
    if (wasLong || bookmarks.empty() || bookmarkSelection >= bookmarks.size()) return;
    const size_t selectedPage = bookmarks[bookmarkSelection].computedSpineIndex;
    if (selectedPage >= cbz->getPageCount()) {
      bookmarks.erase(bookmarks.begin() + bookmarkSelection);
      saveBookmarks();
      if (bookmarkSelection >= bookmarks.size() && !bookmarks.empty()) bookmarkSelection = bookmarks.size() - 1;
      requestUpdate();
      return;
    }
    bookmarksActive = false;
    clearPendingNavigation("stale");
    cancelPrefetch("navigation");
    prefetchAttemptedPage = SIZE_MAX;
    prefetchSkipLoggedPage = SIZE_MAX;
    if (selectedPage != currentPage) {
      currentPage = selectedPage;
      panX = 0;
      panY = 0;
      restorePreviousPageBottom = false;
      pageLoadFailed = false;
      progressDirty = true;
      requestPageRender(true);
    } else {
      requestUpdate();
    }
  }
}

void CbzReaderActivity::renderBookmarks() {
  renderer.clearScreen();
  auto& theme = UITheme::getInstance();
  const auto metrics = theme.getMetrics();
  const Rect screen = theme.getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 "Bookmarks");

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;
  const int listTop = contentTop;
  const int listHeight = std::max(renderer.getLineHeight(UI_10_FONT_ID),
                                  screen.y + screen.height - listTop - footerHeight);
  if (bookmarks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + listHeight / 2, "No bookmarks", true);
  } else {
    GUI.drawList(renderer, Rect{screen.x, listTop, screen.width, listHeight},
                 static_cast<int>(bookmarks.size()), static_cast<int>(bookmarkSelection),
                 [this](const int index) {
                   if (index < 0 || static_cast<size_t>(index) >= bookmarks.size()) return std::string();
                   return "Page " + std::to_string(static_cast<size_t>(bookmarks[index].computedSpineIndex) + 1);
                 });
  }
  renderer.drawCenteredText(SMALL_FONT_ID, screen.y + screen.height - footerHeight / 2,
                            "Select: open  Hold: remove", true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void CbzReaderActivity::openDirectionPopup() {
  directionPopup.show("Reading Direction", READING_DIRECTION_LABELS, READING_DIRECTION_COUNT,
                      readingRtl ? 1 : 0, [this](const int index) {
                        const bool nextRtl = index == 1;
                        if (nextRtl == readingRtl) return;
                        readingRtl = nextRtl;
                        clearPendingNavigation("mode_change");
                        cancelPrefetch("mode_change");
                        prefetchAttemptedPage = SIZE_MAX;
                        prefetchSkipLoggedPage = SIZE_MAX;
                        LOG_DBG("CBZDIR", "direction=%s", readingRtl ? "RTL" : "LTR");
                        requestUpdate();
                      });
  requestUpdate();
}

void CbzReaderActivity::applyViewMode(const int index) {
  if (index < 0 || index > static_cast<int>(ViewMode::SourceOneToOne)) return;
  const ViewMode next = static_cast<ViewMode>(index);
  if (next == viewMode) {
    LOG_DBG("CBZMODE", "current=%s selected=%s changed=0", viewModeName(viewMode), viewModeName(next));
    return;
  }
  directBwDiagnosticMode = false;
  directCropDumpRequested = false;
  directCropDumpFired = false;
  pageLoadFailed = false;
  clearPendingNavigation("mode_change");
  cancelPrefetch("mode_change");
  viewMode = next;
  if (viewMode == ViewMode::Zoom) {
    zoomLevel = std::max<uint8_t>(zoomLevel, 2);
  } else if (viewMode != ViewMode::Zoom) {
    zoomLevel = 0;
  }
  panX = 0;
  panY = 0;
  restorePreviousPageBottom = false;
  ImageBlock::releaseRenderCache();
  if (cacheOnlyCurrentPage) {
    currentImagePath.clear();
    renderedImagePage = SIZE_MAX;
    cacheOnlyCurrentPage = false;
    currentCachedWidth = 0;
    currentCachedHeight = 0;
  }
  forceRenderCacheRebuild = true;
  LOG_DBG("CBZMODE", "current=%s selected=%s changed=1 rotation=%s zoom=%u", viewModeName(viewMode),
          viewModeName(viewMode), viewMode == ViewMode::Landscape ? "90" : "0", static_cast<unsigned>(zoomLevel));
  requestPageRender(true);
}

void CbzReaderActivity::toggleBwDiagnosticMode() {
  directBwDiagnosticMode = false;
  directCropDumpRequested = false;
  directCropDumpFired = false;
  bwDiagnosticMode = !bwDiagnosticMode;
  pageLoadFailed = false;
  clearPendingNavigation("mode_change");
  cancelPrefetch("mode_change");
  panX = 0;
  panY = 0;
  ImageBlock::releaseRenderCache();
  if (cacheOnlyCurrentPage) {
    currentImagePath.clear();
    renderedImagePage = SIZE_MAX;
    cacheOnlyCurrentPage = false;
    currentCachedWidth = 0;
    currentCachedHeight = 0;
  }
  forceRenderCacheRebuild = true;
  LOG_DBG("CBZQUALITY", "render=%s threshold=%d grayLevels=%d dithering=0 contrast=0 sharpen=0",
          bwDiagnosticMode ? "bw_diagnostic" : "gray4", bwDiagnosticMode ? 128 : 0,
          bwDiagnosticMode ? 2 : 4);
  requestPageRender(true);
}

void CbzReaderActivity::adjustZoom(const int direction) {
  if (viewMode != ViewMode::Zoom) return;
  pageLoadFailed = false;
  const int next = std::max(0, std::min(MAX_ZOOM_LEVEL, static_cast<int>(zoomLevel) + direction));
  if (next == zoomLevel) return;
  zoomLevel = static_cast<uint8_t>(next);
  clearPendingNavigation("mode_change");
  cancelPrefetch("mode_change");
  panX = 0;
  panY = 0;
  ImageBlock::releaseRenderCache();
  if (cacheOnlyCurrentPage) {
    currentImagePath.clear();
    renderedImagePage = SIZE_MAX;
    cacheOnlyCurrentPage = false;
    currentCachedWidth = 0;
    currentCachedHeight = 0;
  }
  forceRenderCacheRebuild = true;
  LOG_DBG("CBZMODE", "current=zoom selected=zoom zoom=%u", static_cast<unsigned>(zoomLevel));
  requestPageRender(true);
}

void CbzReaderActivity::panBy(const int dx, const int dy) {
  pageLoadFailed = false;
  // The actual limits are recomputed in renderPage; these conservative deltas
  // keep one gesture bounded even when the image dimensions are not available
  // on the input task.
  const int nextX = std::max(0, std::min(currentMaxPanX, panX + dx));
  const int nextY = std::max(0, std::min(currentMaxPanY, panY + dy));
  if (nextX == panX && nextY == panY) return;
  cancelPrefetch("navigation");
  panX = nextX;
  panY = nextY;
  // Panning replays the current bounded cache band; avoid an extra e-ink
  // refresh for every small scroll gesture.
  requestUpdate(true);
}

void CbzReaderActivity::handleLandscapeHorizontalNavigation(const MappedInputManager::Button button) {
  if (viewMode != ViewMode::Landscape ||
      (button != MappedInputManager::Button::Left && button != MappedInputManager::Button::Right)) {
    return;
  }

  const bool left = button == MappedInputManager::Button::Left;
  const bool atEdge = left ? panY <= 0 : panY >= currentMaxPanY;
  if (atEdge) {
    const bool next = isLogicalNextButton(button);
    logPageIntent(next);
    handlePageTurn(!next, false);
    return;
  }

  const int oldPan = panY;
  const int scrollStep = std::max(32, renderer.getScreenWidth() / 3);
  LOG_DBG("CBZUI", "intent=%s mode=landscape page=%lu", left ? "pan_left" : "pan_right",
          static_cast<unsigned long>(currentPage + 1));
  panBy(0, left ? -scrollStep : scrollStep);
  LOG_DBG("CBZPAN", "mode=landscape direction=%s old=0,%d new=0,%d bounds=0,%d result=%s",
          left ? "left" : "right", oldPan, panY, currentMaxPanY,
          oldPan == panY ? "boundary" : "scrolled");
}

bool CbzReaderActivity::handlePageTurn(const bool previous, const bool resolveVerticalPan) {
  if (!cbz || cbz->getPageCount() == 0) return false;

  // A page-sized scroll is resolved against the last rendered viewport. The
  // renderer clamps the final value again, so no image dimensions are kept in
  // RAM between frames.
  const int viewport = std::max(1, renderer.getScreenHeight() - UITheme::getStatusBarHeight());
  if (resolveVerticalPan && viewMode != ViewMode::FitPage) {
    if (!previous) {
      if (panY < currentMaxPanY) {
        panY = std::min(currentMaxPanY, panY + viewport);
        requestUpdate(true);
        return true;
      }
    } else if (panY > 0) {
      panY = std::max(0, panY - viewport);
      requestUpdate(true);
      return true;
    }
  }

  const size_t oldPage = currentPage;
  const bool logicalNext = !previous;
  // RTL changes only which stored page index is reached by Next/Previous.
  // The page image, its orientation, and all fit/pan geometry remain intact.
  const bool moveTowardHigherIndex = readingRtl ? !logicalNext : logicalNext;
  const size_t pageCount = cbz->getPageCount();
  if (logicalNext) {
    if (moveTowardHigherIndex) {
      if (currentPage + 1 >= pageCount) return true;
      ++currentPage;
    } else {
      if (currentPage == 0) return true;
      --currentPage;
    }
  } else if (moveTowardHigherIndex) {
    if (currentPage + 1 >= pageCount) return true;
    ++currentPage;
  } else {
    if (currentPage == 0) return true;
    --currentPage;
  }
  restorePreviousPageBottom = resolveVerticalPan && viewMode != ViewMode::FitPage && !moveTowardHigherIndex;
  LOG_DBG("CBZPAGE", "requested=%lu committed=%lu stage=request result=pending",
          static_cast<unsigned long>(oldPage + 1), static_cast<unsigned long>(currentPage + 1));
  panX = 0;
  panY = 0;
  if (currentPage != oldPage && sessionPagesTurned < UINT32_MAX) ++sessionPagesTurned;
  if (currentPage != oldPage) {
    progressDirty = true;
    // Deliberate page changes from landscape/zoom controls should begin at a
    // predictable position. The normal vertical page-turn path retains its
    // existing bottom-of-page behavior; horizontal long-press navigation does
    // not carry the previous page's pan into the new page.
    if (!resolveVerticalPan) {
      panX = 0;
      panY = 0;
    }
  }
  // Keep a ready lookahead only when this is the exact forward target it was
  // built for. Any other navigation makes the candidate stale.
  const bool consumesPrefetch = !previous && prefetchReady && prefetchPage == currentPage &&
                                prefetchViewMode == viewMode && prefetchZoomLevel == zoomLevel;
  if (!consumesPrefetch) cancelPrefetch("navigation");
  pageLoadFailed = false;
  requestPageRender(true);
  return true;
}

void CbzReaderActivity::loop() {
  if (!cbz) return;

  if (bookmarksActive) {
    handleBookmarksInput();
    return;
  }

  if (pagePickerActive) {
    handlePagePickerInput();
    return;
  }

  if (directionPopup.isActive()) {
    directionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  if (viewModePopup.isActive()) {
    viewModePopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  if (renderBusy) {
    int pressedButton = -1;
    const MappedInputManager::Button buttons[] = {
        MappedInputManager::Button::Back,       MappedInputManager::Button::Confirm,
        MappedInputManager::Button::Left,       MappedInputManager::Button::Right,
        MappedInputManager::Button::Up,         MappedInputManager::Button::Down,
        MappedInputManager::Button::PageBack,   MappedInputManager::Button::PageForward,
        MappedInputManager::Button::Power,
    };
    bool navigationQueued = false;
    for (const auto button : buttons) {
      if (mappedInput.wasPressed(button) || mappedInput.isPressed(button)) {
        pressedButton = static_cast<int>(button);
        if (!navigationQueued && isQueueableNavigationButton(button)) {
          queueNavigationButton(button, "busy");
          navigationQueued = true;
        }
      }
    }
    if (!navigationQueued && (!busyLogEmitted || pressedButton != busyLogButton)) {
      LOG_DBG("CBZCTRL", "ignored=busy button=%s", pressedButton >= 0
                                                       ? inputButtonName(static_cast<MappedInputManager::Button>(pressedButton))
                                                       : "none");
      busyLogEmitted = true;
      busyLogButton = pressedButton;
    }
    return;
  }
  if (busyLogEmitted) {
    LOG_DBG("CBZCTRL", "busy_end");
    busyLogEmitted = false;
    busyLogButton = -1;
  }

  // Execute a queued edge before the stale-after-render gate, which would
  // otherwise discard the press that arrived during refresh.
  if (pendingNavigation) {
    executePendingNavigation();
    return;
  }

  // A slow decode can finish after a user has already pressed a button. Drop
  // that stale edge (and any held repeat) once, so the completed page cannot
  // immediately receive a second navigation/mode mutation.
  if (inputGateAfterRender) {
    const MappedInputManager::Button buttons[] = {
        MappedInputManager::Button::Back,       MappedInputManager::Button::Confirm,
        MappedInputManager::Button::Left,       MappedInputManager::Button::Right,
        MappedInputManager::Button::Up,         MappedInputManager::Button::Down,
        MappedInputManager::Button::PageBack,   MappedInputManager::Button::PageForward,
        MappedInputManager::Button::Power,
    };
    bool held = false;
    int pressedButton = -1;
    MappedInputManager::Button navigationButton = MappedInputManager::Button::Right;
    bool navigationPressed = false;
    for (const auto button : buttons) {
      if (mappedInput.isPressed(button)) {
        held = true;
        if (pressedButton < 0) pressedButton = static_cast<int>(button);
      }
      if (!navigationPressed && mappedInput.wasPressed(button) && isQueueableNavigationButton(button)) {
        navigationButton = button;
        navigationPressed = true;
      }
    }
    if (navigationPressed) {
      queueNavigationButton(navigationButton, "stale");
      return;
    }
    if (held || mappedInput.wasAnyPressed()) {
      if (!staleLogEmitted || pressedButton != staleLogButton) {
        LOG_DBG("CBZCTRL", "ignored=stale_after_render button=%s", pressedButton >= 0
                                                                          ? inputButtonName(static_cast<MappedInputManager::Button>(pressedButton))
                                                                          : "none");
        staleLogEmitted = true;
        staleLogButton = pressedButton;
      }
      return;
    }
  if (staleLogEmitted) LOG_DBG("CBZCTRL", "stale_end");
    staleLogEmitted = false;
    staleLogButton = -1;
    inputGateAfterRender = false;
  }

  // Temporary direct-mode action: hold Confirm to capture a small source
  // gray8 crop around the current viewport to SD for PC comparison.
  if (directBwDiagnosticMode) {
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= 700) {
      if (!directCropDumpFired) {
        directCropDumpFired = true;
        directCropDumpRequested = true;
        LOG_DBG("CBZCTRL", "action=tjpg_gray_crop mode=source_1to1_direct");
        requestUpdate(true);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) directCropDumpFired = false;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, cbz->getPath().c_str(),
                                        {this, [](void* ctx) { static_cast<CbzReaderActivity*>(ctx)->onGoHome(); }})) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() < 700) {
    openViewModePopup();
    zoomLongPressFired = false;
    return;
  }

  // Landscape is a page-width mode on the panel's native landscape mapping.
  // Its long manga axis is kept in image-local Y, so Left/Right scroll that
  // axis while content remains. At the corresponding edge the same button
  // turns the page; Up/Down remain explicit page navigation controls.
  if (viewMode == ViewMode::Landscape) {
    const bool leftReleased = mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool rightReleased = mappedInput.wasReleased(MappedInputManager::Button::Right);
    const bool upReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::PageBack);
    const bool downReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::PageForward);
    if (leftReleased || rightReleased) {
      handleLandscapeHorizontalNavigation(leftReleased ? MappedInputManager::Button::Left
                                                       : MappedInputManager::Button::Right);
      return;
    }
    if (upReleased || downReleased) {
      LOG_DBG("CBZPAN", "mode=landscape direction=%s action=page old=%lu",
              upReleased ? "up" : "down", static_cast<unsigned long>(currentPage + 1));
      LOG_DBG("CBZUI", "input_accepted button=%s busy=%d prefetch=%s",
              upReleased ? "Up" : "Down", renderBusy ? 1 : 0,
              prefetchRunning ? "running" : (prefetchReady ? "done" : "none"));
      LOG_DBG("CBZUI", "navigation_executing button=%s", upReleased ? "Up" : "Down");
      logPageIntent(!upReleased);
      handlePageTurn(upReleased, false);
      return;
    }
    return;
  }

  // Source 1:1 is a resize-isolation diagnostic. Every directional action is
  // a bounded pan over the native-size image-local cache; it must never reach
  // the normal reader page-turn detector.
  if (viewMode == ViewMode::SourceOneToOne) {
    const auto panSource = [&](const MappedInputManager::Button button) {
      const int horizontalStep = std::max(32, renderer.getScreenWidth() / 3);
      const int verticalStep = std::max(32, renderer.getScreenHeight() / 3);
      int dx = 0;
      int dy = 0;
      const char* direction = "unknown";
      switch (button) {
        case MappedInputManager::Button::Left:
          dx = -horizontalStep;
          direction = "left";
          break;
        case MappedInputManager::Button::Right:
          dx = horizontalStep;
          direction = "right";
          break;
        case MappedInputManager::Button::Up:
          dy = -verticalStep;
          direction = "up";
          break;
        case MappedInputManager::Button::Down:
          dy = verticalStep;
          direction = "down";
          break;
        default:
          return false;
      }
      const int oldX = panX;
      const int oldY = panY;
      panBy(dx, dy);
      LOG_DBG("CBZPAN", "mode=%s direction=%s old=%d,%d new=%d,%d bounds=%d,%d result=%s",
              directBwDiagnosticMode ? "source_1to1_direct" : "source_1to1",
              direction, oldX, oldY, panX, panY, currentMaxPanX, currentMaxPanY,
              oldX == panX && oldY == panY ? "boundary" : "scrolled");
      return true;
    };
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) && panSource(MappedInputManager::Button::Up)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) &&
        panSource(MappedInputManager::Button::Down)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) &&
        panSource(MappedInputManager::Button::Up)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) &&
        panSource(MappedInputManager::Button::Down)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) &&
        panSource(MappedInputManager::Button::Left)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) &&
        panSource(MappedInputManager::Button::Right)) return;
    return;
  }

  // X3/X4 have physical buttons rather than a touchscreen. In Zoom mode every
  // directional action is a pan only. In particular, do not let an edge press
  // fall through to ReaderUtils::detectPageTurn: the user must leave Zoom
  // mode before page navigation is possible again.
  if (viewMode == ViewMode::Zoom) {
    // Long horizontal presses are the deliberate page-navigation escape from
    // Zoom. The fired latch prevents a held button from repeating pages, and
    // the release path below consumes the matching button-up without panning.
    if (mappedInput.isPressed(MappedInputManager::Button::Left) &&
        mappedInput.getHeldTime() >= ReaderUtils::SKIP_HOLD_MS && !zoomLongPressFired) {
      zoomLongPressFired = true;
      LOG_DBG("CBZCTRL", "button=Left action=prev_page mode=zoom press=long");
      handlePageTurn(true, false);
      return;
    }
    if (mappedInput.isPressed(MappedInputManager::Button::Right) &&
        mappedInput.getHeldTime() >= ReaderUtils::SKIP_HOLD_MS && !zoomLongPressFired) {
      zoomLongPressFired = true;
      LOG_DBG("CBZCTRL", "button=Right action=next_page mode=zoom press=long");
      handlePageTurn(false, false);
      return;
    }

    const auto panOrTurn = [&](const MappedInputManager::Button button) {
      const int horizontalStep = std::max(32, renderer.getScreenWidth() / 3);
      const int verticalStep = std::max(32, renderer.getScreenHeight() / 3);
      int dx = 0;
      int dy = 0;
      bool atEdge = false;
      switch (button) {
        case MappedInputManager::Button::Left:
          if (panX > 0) {
            dx = -horizontalStep;
          } else {
            atEdge = true;
          }
          break;
        case MappedInputManager::Button::Right:
          if (panX < currentMaxPanX) {
            dx = horizontalStep;
          } else {
            atEdge = true;
          }
          break;
        case MappedInputManager::Button::Up:
          if (panY > 0) {
            dy = -verticalStep;
          } else {
            atEdge = true;
          }
          break;
        case MappedInputManager::Button::Down:
          if (panY < currentMaxPanY) {
            dy = verticalStep;
          } else {
            atEdge = true;
          }
          break;
        default:
          return false;
      }
      if (!atEdge) {
        const int oldPanX = panX;
        const int oldPanY = panY;
        panBy(dx, dy);
        LOG_DBG("CBZCTRL", "button=%s action=pan pan=%d,%d zoom=%u edge=0 cache=hit",
                button == MappedInputManager::Button::Left   ? "Left"
                : button == MappedInputManager::Button::Right ? "Right"
                : button == MappedInputManager::Button::Up   ? "Up"
                                                              : "Down",
                panX, panY, static_cast<unsigned>(zoomLevel));
        LOG_DBG("CBZPAN", "mode=zoom direction=%s old=%d,%d new=%d,%d bounds=%d,%d result=scrolled",
                button == MappedInputManager::Button::Left   ? "left"
                : button == MappedInputManager::Button::Right ? "right"
                : button == MappedInputManager::Button::Up   ? "up"
                                                              : "down",
                oldPanX, oldPanY, panX, panY, currentMaxPanX, currentMaxPanY);
        return true;
      }
      LOG_DBG("CBZCTRL", "button=%s action=pan_edge pan=%d,%d zoom=%u edge=1 cache=hit",
              button == MappedInputManager::Button::Left   ? "Left"
              : button == MappedInputManager::Button::Right ? "Right"
              : button == MappedInputManager::Button::Up   ? "Up"
                                                             : "Down",
              panX, panY, static_cast<unsigned>(zoomLevel));
      LOG_DBG("CBZPAN", "mode=zoom direction=%s old=%d,%d new=%d,%d bounds=%d,%d result=boundary",
              button == MappedInputManager::Button::Left   ? "left"
              : button == MappedInputManager::Button::Right ? "right"
              : button == MappedInputManager::Button::Up   ? "up"
                                                            : "down",
              panX, panY, panX, panY, currentMaxPanX, currentMaxPanY);
      return true;
    };

    if (mappedInput.wasReleased(MappedInputManager::Button::Up) && panOrTurn(MappedInputManager::Button::Up)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) &&
        panOrTurn(MappedInputManager::Button::Down)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) &&
        panOrTurn(MappedInputManager::Button::Up)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) &&
        panOrTurn(MappedInputManager::Button::Down)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      const bool wasLong = zoomLongPressFired;
      zoomLongPressFired = false;
      if (wasLong) return;
      if (panOrTurn(MappedInputManager::Button::Left)) return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      const bool wasLong = zoomLongPressFired;
      zoomLongPressFired = false;
      if (wasLong) return;
      if (panOrTurn(MappedInputManager::Button::Right)) return;
    }
    // Consume held/repeated/queued directional events too. No Zoom input may
    // reach the generic reader page-turn detector below.
    return;
  }

  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      if (viewMode == ViewMode::Landscape)
        handlePageTurn(true, false);
      else if ((viewMode == ViewMode::Zoom) || currentMaxPanY > 0)
        panBy(0, -std::max(32, renderer.getScreenHeight() / 3));
      else
        handlePageTurn(true);
      return;
    case MappedInputManager::SwipeDir::Down:
      if (viewMode == ViewMode::Landscape)
        handlePageTurn(false, false);
      else if ((viewMode == ViewMode::Zoom) || currentMaxPanY > 0)
        panBy(0, std::max(32, renderer.getScreenHeight() / 3));
      else
        handlePageTurn(false);
      return;
    case MappedInputManager::SwipeDir::Left:
      if (viewMode == ViewMode::Zoom) {
        panBy(std::max(32, renderer.getScreenWidth() / 3), 0);
      } else if (viewMode == ViewMode::Landscape) {
        panBy(0, std::max(32, renderer.getScreenWidth() / 3));
      } else if (currentMaxPanX > 0) {
        panBy(std::max(32, renderer.getScreenWidth() / 3), 0);
      } else {
        handlePageTurn(false);
      }
      return;
    case MappedInputManager::SwipeDir::Right:
      if (viewMode == ViewMode::Zoom) {
        panBy(-std::max(32, renderer.getScreenWidth() / 3), 0);
      } else if (viewMode == ViewMode::Landscape) {
        panBy(0, -std::max(32, renderer.getScreenWidth() / 3));
      } else if (currentMaxPanX > 0) {
        panBy(-std::max(32, renderer.getScreenWidth() / 3), 0);
      } else {
        handlePageTurn(true);
      }
      return;
    case MappedInputManager::SwipeDir::None:
      break;
  }

  auto [prevTriggered, nextTriggered, fromTilt, fromSide] = ReaderUtils::detectPageTurn(mappedInput);
  (void)fromTilt;
  (void)fromSide;
  if (!prevTriggered && !nextTriggered) {
    maybePrefetchNextPage();
    return;
  }
  if (prevTriggered) {
    LOG_DBG("CBZUI", "input_accepted button=Previous busy=%d prefetch=%s", renderBusy ? 1 : 0,
            prefetchRunning ? "running" : (prefetchReady ? "done" : "none"));
    LOG_DBG("CBZUI", "navigation_executing button=Previous");
    logPageIntent(false);
    // Fit Width/Fit Page page buttons must never be consumed by the
    // same-page vertical-pan resolution in handlePageTurn().
    handlePageTurn(true, false);
    return;
  }
  if (nextTriggered) {
    LOG_DBG("CBZUI", "input_accepted button=Next busy=%d prefetch=%s", renderBusy ? 1 : 0,
            prefetchRunning ? "running" : (prefetchReady ? "done" : "none"));
    LOG_DBG("CBZUI", "navigation_executing button=Next");
    logPageIntent(true);
    handlePageTurn(false, false);
    return;
  }
}

void CbzReaderActivity::render(RenderLock&&) {
  if (!cbz) return;

  renderer.setDarkMode(SETTINGS.readerDarkMode != 0);

  // Foreground rendering owns the decoder, PixelCache and staging files. Keep
  // a valid candidate only when this render is the matching page turn; every
  // other render invalidates lookahead before drawing any UI or page content.
  const bool matchingPrefetch = prefetchReady && prefetchPage == currentPage &&
                                prefetchViewMode == viewMode && prefetchZoomLevel == zoomLevel;
  const bool waitingForMatchingPrefetch = [&]() {
    if (!prefetchRunning || !pendingNavigation || pendingNavigationViewMode != viewMode) return false;
    const bool logicalNextButton = isLogicalNextButton(pendingNavigationButton);
    if (!logicalNextButton) return false;
    const size_t target = readingRtl ? (currentPage == 0 ? SIZE_MAX : currentPage - 1) : currentPage + 1;
    return target != SIZE_MAX && prefetchPage == target && prefetchViewMode == viewMode &&
           prefetchZoomLevel == zoomLevel;
  }();
  if (!matchingPrefetch && !waitingForMatchingPrefetch) cancelPrefetch("foreground_render");
  if (prefetchRunning) {
    if (waitingForMatchingPrefetch && loadingUiPending) {
      loadingUiPending = false;
      loadingUiRenderArmed = true;
      longOperationIndicator.showBeforeWork("CBZ_RENDER", true);
      requestUpdate(true);
      return;
    }
    LOG_DBG("CBZPREFETCH", "hardware=%s state=blocked reason=foreground_render",
            gpio.deviceIsX3() ? "X3" : "X4");
    LOG_DBG("CBZUI", "waiting_for_prefetch_release page=%lu",
            static_cast<unsigned long>(currentPage + 1));
    requestUpdate(true);
    return;
  }

  if (bookmarksActive) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderBookmarks();
    return;
  }
  if (pagePickerActive) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderPagePicker();
    return;
  }
  if (viewModePopup.isActive()) {
    viewModePopup.processRender(renderer, mappedInput);
    return;
  }
  if (directionPopup.isActive()) {
    directionPopup.processRender(renderer, mappedInput);
    return;
  }

  // A page request is split across two render-task turns. The first turn
  // refreshes the already-presented page with one static loading line; the
  // second turn performs the existing synchronous extraction/decode/cache
  // work. This keeps the reader responsive without concurrent decoders.
  if (loadingUiPending) {
    loadingUiPending = false;
    if (cbz->getPageCount() > 0 && currentPage < cbz->getPageCount()) {
      loadingUiRenderArmed = true;
      longOperationIndicator.showBeforeWork("CBZ_RENDER", true);
      requestUpdate(true);
      return;
    }
  }
  if (loadingUiRenderArmed) loadingUiRenderArmed = false;

  renderer.setRenderMode(GfxRenderer::BW);
  if (cbz->getPageCount() == 0) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_EMPTY_FILE), true,
                              EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }
  if (currentPage >= cbz->getPageCount()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_END_OF_BOOK), true,
                              EpdFontFamily::BOLD);
    renderStatusBar();
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    saveProgress();
    return;
  }

  renderBusy = true;
  renderPage();
  renderBusy = false;
  // renderPage() returns only after the display/gray refresh path has
  // completed. This timestamp starts the conservative idle window for the
  // optional next-page cache.
  lastRenderCompleteMs = millis();
  inputGateAfterRender = true;
  saveProgress();
  LOG_DBG("CBZUI", "page_ready page=%lu prefetch=%s", static_cast<unsigned long>(currentPage + 1),
          prefetchRunning ? "running" : (prefetchReady ? "done" : "none"));
}

void CbzReaderActivity::renderPage() {
  const uint32_t renderStartedMs = millis();
  auto longOperation = longOperationIndicator.scoped("CBZ_RENDER");
  longOperationIndicator.stage("preparing");
  uint32_t extractMs = 0;
  uint32_t probeMs = 0;
  uint32_t decoderSelectMs = 0;
  uint32_t renderMs = 0;
  uint32_t refreshMs = 0;
  uint32_t cacheRenderMs = 0;
  ImageRenderDiagnostics imageDiagnostics;
  ImageDimensions imageDimensions{};
  int diagnosticTargetWidth = 0;
  int diagnosticTargetHeight = 0;
  int diagnosticViewportWidth = 0;
  int diagnosticViewportHeight = 0;
#if NOOIR_CBZ_QUALITY_DIAGNOSTICS
  ImageQualityProbe qualityProbe;
#endif
  bool diagnosticsActive = false;
  bool directRenderActive = false;
  bool fallbackDecoder = false;
  bool cacheHit = false;
  constexpr bool cbzDithering = false;
  const char* selectedDecoder = "none";
  int renderedX = 0;
  int renderedY = 0;
  int renderedWidth = 0;
  int renderedHeight = 0;
  std::string renderedCachePath;
  bool candidateImageActive = false;
  std::string candidateImagePath;
  const size_t previousRenderedPage = renderedImagePage;
  const std::string previousImagePath = currentImagePath;
  const auto previousOrientation = renderer.getOrientation();
  const bool landscapeMode = viewMode == ViewMode::Landscape;
  const bool sourceOneToOneMode = viewMode == ViewMode::SourceOneToOne;
  const bool nativeBwMode = bwDiagnosticMode || (sourceOneToOneMode && sourceOneToOneBwMode);
  const bool directBwMode = sourceOneToOneMode && sourceOneToOneBwMode && directBwDiagnosticMode;
  if (landscapeMode) renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);

  std::string imagePath = currentImagePath;
  bool rendered = false;
  const bool pageSourceNeeded = renderedImagePage != currentPage || (!cacheOnlyCurrentPage && imagePath.empty());
  bool usingPrefetchedPage = pageSourceNeeded && prefetchReady && prefetchPage == currentPage &&
                             prefetchViewMode == viewMode && prefetchZoomLevel == zoomLevel &&
                             !prefetchImagePath.empty() && !prefetchCachePath.empty();
  if (usingPrefetchedPage && !Storage.exists(prefetchCachePath.c_str())) {
    LOG_DBG("CBZPREFETCH", "hardware=%s state=skip reason=stale", gpio.deviceIsX3() ? "X3" : "X4");
    cancelPrefetch("stale");
    usingPrefetchedPage = false;
  }
  bool candidateCacheActive = false;
  if (!pageLoadFailed && (forceRenderCacheRebuild || pageSourceNeeded)) {
    longOperationIndicator.stage("extracting");
    const uint32_t extractStartedMs = millis();
    // Keep the committed cache readable until the candidate page has decoded
    // and written its own cache. Only the bounded RAM slot and any abandoned
    // candidate are retired here; the active render.pxc remains available for
    // recovery if extraction or decoding fails.
    ImageBlock::releaseRenderCache();
    const std::string staleRenderCandidates[] = {cbz->getCachePath() + "/render.next.pxc",
                                                 cbz->getCachePath() + "/render.pending.pxc"};
    for (const auto& staleRenderCandidate : staleRenderCandidates) {
      if (usingPrefetchedPage && staleRenderCandidate == prefetchCachePath) continue;
      if (staleRenderCandidate == activeRenderCachePath || !Storage.exists(staleRenderCandidate.c_str())) continue;
      logCbzCacheAction("remove", "page_candidate_stale_cache", staleRenderCandidate);
      Storage.remove(staleRenderCandidate.c_str());
    }
    // The direct A/B path intentionally renders from current.* without a
    // render.pxc candidate. Normal modes retain the atomic candidate flow.
    candidateCacheActive = !directBwMode;
    if (usingPrefetchedPage) {
      candidateImagePath = prefetchImagePath;
      candidateImageActive = Storage.exists(prefetchImagePath.c_str());
      // The prefetched .pxc is the complete page candidate. The extracted
      // next.* file is only an optional source fallback and must never be a
      // prerequisite for promoting a valid cache.
      cacheOnlyCurrentPage = !candidateImageActive;
      currentCachedWidth = prefetchSourceWidth;
      currentCachedHeight = prefetchSourceHeight;
      imagePath = candidateImageActive ? prefetchImagePath : std::string();
       LOG_DBG("CBZPREFETCH", "hardware=%s state=use page=%lu generation=%lu cache=%s",
               gpio.deviceIsX3() ? "X3" : "X4", static_cast<unsigned long>(currentPage + 1),
               static_cast<unsigned long>(prefetchGeneration), prefetchCachePath.c_str());
      LOG_DBG("CBZPREFETCH", "candidate=cache source=%s dimensions=%dx%d",
              candidateImageActive ? "present" : "cache_only", currentCachedWidth, currentCachedHeight);
    } else if (pageSourceNeeded) {
      cacheOnlyCurrentPage = false;
      currentCachedWidth = 0;
      currentCachedHeight = 0;
      const std::string_view entry = cbz->getPageEntry(currentPage);
      const size_t dot = entry.find_last_of('.');
      if (dot != std::string_view::npos) {
      std::string extension(entry.substr(dot));
      std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      candidateImagePath = cbz->getCachePath() + "/next" + extension;
      // If a filesystem rename previously failed, the active page may still
      // be /next.*. Never extract the next page over that valid source.
      if (currentImagePath == candidateImagePath) {
        candidateImagePath = cbz->getCachePath() + "/pending" + extension;
      }
      const char* candidateFiles[] = {"/next.jpg",    "/next.jpeg",    "/next.png",
                                      "/pending.jpg", "/pending.jpeg", "/pending.png"};
      for (const char* suffix : candidateFiles) {
        const std::string stale = cbz->getCachePath() + suffix;
        if (Storage.exists(stale.c_str()) && stale != candidateImagePath && stale != currentImagePath) {
          logCbzCacheAction("remove", "page_candidate_stale", stale);
          Storage.remove(stale.c_str());
        }
      }
      LOG_DBG("CBZPAGE", "from=%lu to=%lu stage=extract result=start",
              previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
              static_cast<unsigned long>(currentPage + 1));
      if (cbz->extractPageTo(currentPage, candidateImagePath)) {
        imagePath = candidateImagePath;
        candidateImageActive = true;
        LOG_DBG("CBZPAGE", "from=%lu to=%lu stage=extract result=ok path=\"%s\"",
                previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
                static_cast<unsigned long>(currentPage + 1), imagePath.c_str());
      } else {
        imagePath.clear();
        LOG_ERR("CBZPAGE", "from=%lu to=%lu stage=extract result=fail previousPagePreserved=%d",
                previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
                static_cast<unsigned long>(currentPage + 1), previousRenderedPage != SIZE_MAX ? 1 : 0);
      }
      } else {
        imagePath.clear();
        LOG_ERR("CBZPAGE", "from=%lu to=%lu stage=extract result=fail reason=unsupported_extension previousPagePreserved=%d",
                previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
                static_cast<unsigned long>(currentPage + 1), previousRenderedPage != SIZE_MAX ? 1 : 0);
      }
    }
    extractMs = millis() - extractStartedMs;
  }
  renderer.clearScreen();

  if (!pageLoadFailed && (!imagePath.empty() || usingPrefetchedPage || cacheOnlyCurrentPage)) {
    longOperationIndicator.stage("decoding");
    // A prefetched cache is authoritative when its extracted source is gone.
    // If the optional source is still present, keep a decoder available as a
    // safe fallback for a stale/corrupt candidate cache.
    const bool prefetchDimensionsReady = usingPrefetchedPage && currentCachedWidth > 0 && currentCachedHeight > 0;
    const bool activeCacheOnly = cacheOnlyCurrentPage && renderedImagePage == currentPage &&
                                 currentCachedWidth > 0 && currentCachedHeight > 0;
    const bool cacheOnlyRender = (prefetchDimensionsReady && !candidateImageActive) || activeCacheOnly;
    ImageToFramebufferDecoder* decoder = cacheOnlyRender || imagePath.empty()
                                             ? nullptr
                                             : ImageDecoderFactory::getDecoder(imagePath);
    const uint32_t probeStartedMs = millis();
    const bool dimensionsReady = (prefetchDimensionsReady || activeCacheOnly)
                                     ? ((imageDimensions.width = currentCachedWidth) > 0 &&
                                        (imageDimensions.height = currentCachedHeight) > 0)
                                     : (decoder && decoder->getDimensions(imagePath, imageDimensions) &&
                                        imageDimensions.width > 0 && imageDimensions.height > 0);
    probeMs = millis() - probeStartedMs;
    selectedDecoder = decoder ? decoder->getFormatName() : "none";
    if (dimensionsReady) {
      const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const int statusHeight = UITheme::getStatusBarHeight();
      const int availableWidth = std::max(1, safe.width);
      const int availableHeight = std::max(1, safe.height - statusHeight);
      const float fitWidth = static_cast<float>(availableWidth) / imageDimensions.width;
      const float fitHeight = static_cast<float>(availableHeight) / imageDimensions.height;
      float scale = std::min(fitWidth, fitHeight);
      if (sourceOneToOneMode) {
        // Diagnostic-only native rendering: keep the source dimensions exactly
        // and let the streamed PixelCache provide the viewport pan.
        scale = 1.0f;
      } else if (viewMode == ViewMode::FitWidth) {
        scale = std::min(1.5f, static_cast<float>(availableWidth) / imageDimensions.width);
      } else if (viewMode == ViewMode::FitPage) {
        scale = std::min(scale, 1.0f);
      } else if (viewMode == ViewMode::Landscape) {
        // Manga Landscape Page Width is based on the source page width, not
        // on a rotated height. Keep the source page tall and scroll vertically
        // through it in the landscape viewport.
        scale = std::min(1.5f, fitWidth);
      } else {
        const float fitWidth = static_cast<float>(availableWidth) / imageDimensions.width;
        scale = std::min(2.5f, fitWidth * (1.0f + zoomLevel * ZOOM_STEP));
      }
      const int targetWidth = std::max(1, static_cast<int>(std::floor(imageDimensions.width * scale)));
      int boundedTargetWidth = targetWidth;
      int boundedTargetHeight = std::max(1, static_cast<int>(std::floor(imageDimensions.height * scale)));
      const int64_t targetPixels = static_cast<int64_t>(boundedTargetWidth) * boundedTargetHeight;
      if (boundedTargetWidth > MAX_RENDER_DIMENSION || boundedTargetHeight > MAX_RENDER_DIMENSION ||
          targetPixels > MAX_RENDER_PIXELS) {
        const float dimensionScale = std::min(static_cast<float>(MAX_RENDER_DIMENSION) / boundedTargetWidth,
                                              static_cast<float>(MAX_RENDER_DIMENSION) / boundedTargetHeight);
        const float pixelScale = std::sqrt(static_cast<float>(MAX_RENDER_PIXELS) /
                                           static_cast<float>(std::max<int64_t>(1, targetPixels)));
        const float limitScale = std::min(dimensionScale, pixelScale);
        scale *= std::min(1.0f, limitScale);
        boundedTargetWidth = std::max(1, static_cast<int>(std::floor(imageDimensions.width * scale)));
        boundedTargetHeight = std::max(1, static_cast<int>(std::floor(imageDimensions.height * scale)));
        LOG_DBG("CBZR", "Bounded oversized page render to %dx%d", boundedTargetWidth, boundedTargetHeight);
      }
       const int targetHeight = boundedTargetHeight;
       const bool directPageDimensions = boundedTargetWidth == imageDimensions.width &&
                                         targetHeight == imageDimensions.height;
       const bool directRender = directBwMode && directPageDimensions && decoder &&
                                 strcmp(decoder->getFormatName(), "JPEG") == 0;
       directRenderActive = directRender;
       // If the safety bound prevents a true 1:1 render, fall back to the
       // normal atomic cache path rather than silently rendering uncached.
       if (directBwMode && !directRender) candidateCacheActive = true;
       diagnosticTargetWidth = boundedTargetWidth;
       diagnosticTargetHeight = targetHeight;
       diagnosticViewportWidth = availableWidth;
       diagnosticViewportHeight = availableHeight;
      const int maxPanX = std::max(0, boundedTargetWidth - availableWidth);
      const int maxPanY = std::max(0, targetHeight - availableHeight);
      // Keep cache coordinates in source-image space so Landscape scrolling
      // uses image-local Y while portrait Zoom continues to use panX/panY.
      currentMaxPanX = landscapeMode ? 0 : maxPanX;
      currentMaxPanY = maxPanY;
      if (restorePreviousPageBottom && !landscapeMode) panY = maxPanY;
      panX = std::max(0, std::min(panX, maxPanX));
      panY = std::max(0, std::min(panY, currentMaxPanY));
      restorePreviousPageBottom = false;
      RenderConfig config;
      // LandscapeCounterClockwise is the panel's native landscape mapping.
      // Keep the CBZ page in source orientation: source width fills the
      // logical landscape width and source height is the scroll axis.
      config.x = boundedTargetWidth > availableWidth ? safe.x - panX
                                                      : safe.x + (availableWidth - boundedTargetWidth) / 2;
      config.y = targetHeight > availableHeight ? safe.y - panY
                                                 : safe.y + (availableHeight - targetHeight) / 2;
      config.maxWidth = boundedTargetWidth;
      config.maxHeight = targetHeight;
      config.useGrayscale = true;
      // CBZ pages have already gone through bounded area reduction.  Preserve
      // those averaged gray values directly on the four-level display instead
      // of adding a second Bayer pattern that makes manga screentones/noise
      // look speckled. EPUB and the other readers keep their existing
      // dithering configuration.
      config.useDithering = cbzDithering;
      config.performanceMode = false;
      config.useExactDimensions = true;
      config.cbzQualityMode = true;
       config.cbzBwDiagnostic = nativeBwMode;
       config.cbzDirectBwDiagnostic = directRender;
       config.cbzDirectViewportX = safe.x;
       config.cbzDirectViewportY = safe.y;
       config.cbzDirectViewportWidth = availableWidth;
       config.cbzDirectViewportHeight = availableHeight;
       if (directRender && directCropDumpRequested) {
         constexpr int MAX_CROP_SIZE = 200;
         const int cropWidth = std::min(MAX_CROP_SIZE, static_cast<int>(imageDimensions.width));
         const int cropHeight = std::min(MAX_CROP_SIZE, static_cast<int>(imageDimensions.height));
         const int cropCenterX = panX + availableWidth / 2;
         const int cropCenterY = panY + availableHeight / 2;
         config.cbzDumpTjpgGrayCrop = true;
         config.cbzDumpCropWidth = cropWidth;
         config.cbzDumpCropHeight = cropHeight;
         config.cbzDumpCropX = std::max(0, std::min(imageDimensions.width - cropWidth,
                                                    cropCenterX - cropWidth / 2));
         config.cbzDumpCropY = std::max(0, std::min(imageDimensions.height - cropHeight,
                                                    cropCenterY - cropHeight / 2));
         config.cbzDumpCropPath = cbz->getCachePath() + "/tjpg_gray_crop.bmp";
         directCropDumpRequested = false;
       }
      const std::string renderCachePath = cbz->getRenderCachePath();
      std::string candidateRenderCachePath = cbz->getCachePath() + "/render.next.pxc";
      if (candidateRenderCachePath == activeRenderCachePath) {
        candidateRenderCachePath = cbz->getCachePath() + "/render.pending.pxc";
      }
      renderedX = config.x;
      renderedY = config.y;
      renderedWidth = boundedTargetWidth;
      renderedHeight = targetHeight;
      // A new page is rendered into a candidate cache. Pan/zoom/mode redraws
      // keep using the committed cache for the current page.
       if (usingPrefetchedPage) {
         // Keep the exact candidate selected by the prefetch transaction. Do
         // not recompute render.next/render.pending from mutable active state.
         renderedCachePath = prefetchCachePath;
       } else {
         renderedCachePath = candidateCacheActive
                                 ? candidateRenderCachePath
                                 : (activeRenderCachePath.empty() ? renderCachePath : activeRenderCachePath);
       }
      if (landscapeMode) {
        LOG_DBG("CBZLAND",
                "source=%dx%d scaleBasis=source_width viewport=%dx%d scale=%.4f scaledPage=%dx%d visibleViewport=%dx%d scroll=%d maxScroll=%d rotation=90 cache=%s destOrigin=%d,%d clipRect=%d,%d %dx%d",
                imageDimensions.width, imageDimensions.height, availableWidth, availableHeight,
                static_cast<double>(scale), boundedTargetWidth, targetHeight, availableWidth, availableHeight, panY,
                currentMaxPanY, renderedCachePath.c_str(), config.x, config.y, safe.x, safe.y, availableWidth,
                availableHeight);
      }
       // Pan/zoom redraws replay the bounded 2-bit cache instead of decoding
       // the source image again. The direct A/B mode is the deliberate
       // exception: it bypasses the cache and streams visible source pixels
       // straight to the native BW framebuffer.
       if (!directRender) {
         longOperationIndicator.stage("cache_replay");
         ImageBlock::resetPixelCacheReplayStats();
         const uint32_t cacheStartedMs = millis();
         rendered = ImageBlock::renderFromPixelCache(renderer, renderedCachePath, config.x, config.y,
                                                     boundedTargetWidth, targetHeight);
         cacheRenderMs += millis() - cacheStartedMs;
         cacheHit = rendered;
       } else {
         renderedCachePath.clear();
         cacheHit = false;
       }
       if (!rendered) {
         longOperationIndicator.stage("decoding");
         diagnosticsActive = true;
#if NOOIR_CBZ_QUALITY_DIAGNOSTICS
        qualityProbe.reset(boundedTargetWidth, targetHeight);
#endif
        config.diagnostics = &imageDiagnostics;
#if NOOIR_CBZ_QUALITY_DIAGNOSTICS
        config.qualityProbe = &qualityProbe;
#endif
        config.cachePath = directRender ? std::string() : renderedCachePath;
        // Keep the same bounded fallback used by EPUB for JPEGs whose entropy
        // tables JPEGDEC cannot safely accept.
        TjpgdToFramebufferConverter tjpgd;
        const uint32_t decoderStartedMs = millis();
        const bool useTjpgd = decoder && strcmp(decoder->getFormatName(), "JPEG") == 0 &&
                              (directRender || TjpgdToFramebufferConverter::requiresFallback(imagePath));
        decoderSelectMs = millis() - decoderStartedMs;
        fallbackDecoder = useTjpgd;
        selectedDecoder = useTjpgd ? "TJpgDec"
                                   : (decoder ? (strcmp(decoder->getFormatName(), "JPEG") == 0 ? "JPEGDEC"
                                                                                              : decoder->getFormatName())
                                               : "PXC");
        rendered = decoder && (useTjpgd ? tjpgd.decodeToFramebuffer(imagePath, renderer, config)
                                        : decoder->decodeToFramebuffer(imagePath, renderer, config));
      }
      if (sourceOneToOneMode) {
        imageDiagnostics.resampleMode =
            boundedTargetWidth == imageDimensions.width && targetHeight == imageDimensions.height ? "none"
                                                                                                   : "bounded_safety";
        imageDiagnostics.areaResampling = false;
        LOG_DBG("CBZ1TO1",
                "source=%dx%d target=%dx%d viewport=%dx%d scale=%.3f resample=%s render=%s cache=%s pan=%d,%d bounds=%d,%d",
                imageDimensions.width, imageDimensions.height, boundedTargetWidth, targetHeight, availableWidth,
                availableHeight, static_cast<double>(scale), imageDiagnostics.resampleMode,
                directRender ? "bw_direct" : (sourceOneToOneBwMode ? "bw_native" : "gray4"),
                directRender ? "none" : (cacheHit ? "hit" : "miss"), panX, panY,
                currentMaxPanX, currentMaxPanY);
      }
    }
  }

  if (rendered && candidateCacheActive && !renderedCachePath.empty() &&
      !Storage.exists(renderedCachePath.c_str())) {
    LOG_ERR("CBZCACHE", "generation=page state=validate result=missing_candidate path=\"%s\"",
            renderedCachePath.c_str());
    rendered = false;
  }

  if (rendered && (candidateCacheActive || candidateImageActive)) {
    // Publish the freshly decoded page only after extraction, probing and
    // rendering succeeded. The old current.* file remains available until
    // this point, so a failed transition can safely return to it.
    LOG_DBG("CBZPAGE", "from=%lu to=%lu stage=validate result=ok",
            previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
            static_cast<unsigned long>(currentPage + 1));
    if (candidateCacheActive) {
      const std::string canonicalCache = cbz->getRenderCachePath();
      const std::string candidateCache = usingPrefetchedPage
                                             ? renderedCachePath
                                             : (renderedCachePath == cbz->getCachePath() + "/render.pending.pxc"
                                                    ? cbz->getCachePath() + "/render.pending.pxc"
                                                    : cbz->getCachePath() + "/render.next.pxc");
      if (Storage.exists(candidateCache.c_str())) {
        if (Storage.exists(canonicalCache.c_str())) {
          logCbzCacheAction("remove", "page_swap_replace_cache", canonicalCache);
          Storage.remove(canonicalCache.c_str());
        }
        if (Storage.rename(candidateCache.c_str(), canonicalCache.c_str())) {
          activeRenderCachePath = canonicalCache;
          renderedCachePath = activeRenderCachePath;
          LOG_DBG("CBZCACHE", "generation=page state=publish path=\"%s\"", canonicalCache.c_str());
        } else {
          activeRenderCachePath = candidateCache;
          renderedCachePath = activeRenderCachePath;
          LOG_ERR("CBZCACHE", "generation=page state=publish result=rename_failed path=\"%s\"",
                  candidateCache.c_str());
        }
      } else {
        LOG_ERR("CBZCACHE", "generation=page state=publish result=missing_candidate path=\"%s\"",
                candidateCache.c_str());
      }
    }

    if (candidateImageActive) {
      const size_t dot = candidateImagePath.find_last_of('.');
      const std::string canonical = dot == std::string::npos
                                        ? cbz->getCachePath() + "/current.jpg"
                                        : cbz->getCachePath() + "/current" + candidateImagePath.substr(dot);
      const char* currentFiles[] = {"/current.jpg", "/current.jpeg", "/current.png"};
      for (const char* suffix : currentFiles) {
        const std::string oldPath = cbz->getCachePath() + suffix;
        if (oldPath != canonical && oldPath != candidateImagePath && Storage.exists(oldPath.c_str())) {
          logCbzCacheAction("remove", "page_swap_old_current", oldPath);
          Storage.remove(oldPath.c_str());
        }
      }
      if (!previousImagePath.empty() && previousImagePath != canonical && previousImagePath != candidateImagePath &&
          Storage.exists(previousImagePath.c_str())) {
        logCbzCacheAction("remove", "page_swap_old_source", previousImagePath);
        Storage.remove(previousImagePath.c_str());
      }
      if (canonical != candidateImagePath && Storage.exists(canonical.c_str())) {
        logCbzCacheAction("remove", "page_swap_replace_current", canonical);
        Storage.remove(canonical.c_str());
      }
      if (canonical != candidateImagePath && Storage.rename(candidateImagePath.c_str(), canonical.c_str())) {
        currentImagePath = canonical;
      } else {
        // Keep the validated candidate as the active source if the filesystem
        // cannot rename it. clearCurrentImage() also removes next.* on exit.
        currentImagePath = candidateImagePath;
      }
    }
    if (candidateImageActive || (usingPrefetchedPage && cacheOnlyCurrentPage)) renderedImagePage = currentPage;
    if (usingPrefetchedPage) {
      if (!candidateImageActive && !prefetchImagePath.empty() && prefetchImagePath != currentImagePath &&
          Storage.exists(prefetchImagePath.c_str())) {
        // The cache candidate is now committed; the extracted source was only
        // a fallback artifact and can be retired after promotion.
        logCbzCacheAction("remove", "prefetch_source_promoted", prefetchImagePath);
        Storage.remove(prefetchImagePath.c_str());
      }
      // The candidate has now become the active current page. Forget the
      // lookahead bookkeeping without deleting the published assets.
      prefetchReady = false;
      prefetchPage = SIZE_MAX;
      prefetchImagePath.clear();
      prefetchCachePath.clear();
      prefetchSourceWidth = 0;
      prefetchSourceHeight = 0;
    }
    candidateCacheActive = false;
    candidateImageActive = false;
    forceRenderCacheRebuild = false;
    LOG_DBG("CBZPAGE", "from=%lu to=%lu stage=swap result=ok path=\"%s\"",
            previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
            static_cast<unsigned long>(currentPage + 1), currentImagePath.c_str());
  }
  // A direct redraw has no cache candidate to publish. Clear the one-shot
  // rebuild request after the successful decode so later pans only reuse the
  // already extracted current.* source (and re-decode it deliberately).
  if (rendered && directRenderActive) forceRenderCacheRebuild = false;

  if (!rendered) {
    // Remove only the uncommitted candidate/partial cache. The previous
    // current.* page is deliberately retained for recovery and retry.
    if (candidateCacheActive && !renderedCachePath.empty()) {
      logCbzCacheAction("remove", "page_candidate_cache_failed", renderedCachePath);
      Storage.remove(renderedCachePath.c_str());
    }
    if (candidateImageActive && !candidateImagePath.empty()) {
      logCbzCacheAction("remove", "page_candidate_failed", candidateImagePath);
      Storage.remove(candidateImagePath.c_str());
    }
    if (usingPrefetchedPage) {
      prefetchReady = false;
      prefetchPage = SIZE_MAX;
      prefetchImagePath.clear();
      prefetchCachePath.clear();
      prefetchSourceWidth = 0;
      prefetchSourceHeight = 0;
    }
    if (previousRenderedPage != SIZE_MAX && previousRenderedPage != currentPage && !previousImagePath.empty()) {
      currentPage = previousRenderedPage;
      if (sessionPagesTurned > 0) --sessionPagesTurned;
      progressDirty = lastPersistedPage != currentPage;
      panX = 0;
      panY = 0;
      restorePreviousPageBottom = false;
      LOG_ERR("CBZPAGE", "to=%lu stage=prepare result=fail previousPagePreserved=1",
              static_cast<unsigned long>(currentPage + 1));
    }
    candidateImageActive = false;
    pageLoadFailed = true;
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_PAGE_LOAD_ERROR), true,
                              EpdFontFamily::BOLD);
  }
  // Keep the landscape UI orientation for the status bar and refresh policy;
  // only the image writer temporarily switches to Portrait for its physical
  // 90-degree mapping. The activity restores the caller's orientation below.
  if (landscapeMode) renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  renderStatusBar();
  renderMs = millis() - renderStartedMs;
  const bool useGray4 = rendered && !nativeBwMode && renderer.supportsStripGrayscale();
  const bool pageTransition = !hasRenderedRefreshState || lastRefreshPage != currentPage;
  const bool viewChanged = hasRenderedRefreshState &&
                           (lastRefreshViewMode != viewMode || lastRefreshZoom != zoomLevel);
  const bool panChanged = hasRenderedRefreshState &&
                          (lastRefreshPanX != panX || lastRefreshPanY != panY);
  if (rendered) {
    const bool pageChanged = pageTransition;
    const char* reason = !hasRenderedRefreshState
                             ? "first_render"
                             : pageChanged ? "page_turn" : viewChanged ? "zoom" : panChanged ? "pan"
                                                                                               : cacheHit ? "cache_replay"
                                                                                                          : "rerender";
    const char* baseRefresh = useGray4
                                  ? (pageTransition || viewChanged || pagesUntilFullRefresh <= 1 ? "half_precondition"
                                                                                              : "gray_base_fast")
                                  : "standard";
    LOG_DBG("CBZREFRESH",
            "reason=%s hardware=%s baseRefresh=%s grayPasses=%s cleanup=%s cache=%s page=%lu mode=%s zoom=%u pan=%d,%d",
            reason, gpio.deviceIsX3() ? "X3" : "X4", baseRefresh,
            useGray4 ? "LSB+MSB" : "none",
            useGray4 ? "framebuffer" : "none", cacheHit ? "hit" : "miss",
            static_cast<unsigned long>(currentPage + 1),
            viewModeName(viewMode),
            static_cast<unsigned>(zoomLevel), panX, panY);
    hasRenderedRefreshState = true;
    lastRefreshPage = currentPage;
    lastRefreshViewMode = viewMode;
    lastRefreshZoom = zoomLevel;
    lastRefreshPanX = panX;
    lastRefreshPanY = panY;
  }
  longOperationIndicator.stage("refreshing");
  // Paint the one-shot hint into the refresh that was already required for
  // this page. This adds no separate e-ink update or page-turn delay.
  longOperationIndicator.drawIfDue();
  const uint32_t refreshStartedMs = millis();
  // Seed the panel's grayscale base with the same device-aware policy used by
  // XTC/EPUB readers.  The normal BW helper is retained as the fallback for a
  // simulator/controller that does not advertise strip grayscale support.
  if (useGray4) {
    // A comic page turn changes a dense full-screen image.  Use the panel's
    // clean half refresh for that transition so the next two-plane grayscale
    // pass does not inherit the previous page's charge.  Pan/zoom redraws on
    // the same page keep the lighter differential base.
    if (pageTransition || viewChanged || pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
      pagesUntilFullRefresh = ReaderUtils::refreshCadence(renderer);
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      --pagesUntilFullRefresh;
    }
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  refreshMs = millis() - refreshStartedMs;

  // CBZ stores four logical gray levels in the 2-bpp pixel cache.  The base
  // pass above intentionally remains BW, but leaving the reader there would
  // collapse levels 0..2 into one black state before they reach the panel.
  // Reuse the existing bounded tiled grayscale path used by the EPUB reader:
  // one small strip scratch buffer, two 1-bpp planes, and the panel's normal
  // X3/X4 gray waveform.  Each plane replays the same current-page cache, so
  // the source JPEG is not decoded again and no full page framebuffer is made.
  if (useGray4 && !renderedCachePath.empty()) {
    constexpr int STRIP_ROWS = 80;
    const int displayHeight = renderer.getDisplayHeight();
    const int displayWidthBytes = renderer.getDisplayWidthBytes();
    const uint8_t* baseFrameBuffer = renderer.getFrameBuffer();
    auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(displayWidthBytes) * STRIP_ROWS);
    if (scratch) {
      bool grayscaleCacheValid = true;
      // Render both grayscale planes for one physical strip before moving to
      // the next strip. ImageBlock keeps the active bounded cache band in RAM;
      // this ordering lets the MSB pass replay the same band loaded by LSB
      // instead of reopening the .pxc for a second full set of bands.
      const GfxRenderer::RenderMode planeModes[] = {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB};
      const bool lsbPlanes[] = {true, false};
      for (int stripY = 0; stripY < displayHeight; stripY += STRIP_ROWS) {
        const int rows = std::min(STRIP_ROWS, displayHeight - stripY);
        for (int planeIndex = 0; planeIndex < 2; ++planeIndex) {
          renderer.setRenderMode(planeModes[planeIndex]);
          renderer.beginStripTarget(scratch.get(), stripY, rows);
          // Preserve the already-rendered BW base (status bar, margins and
          // the image's black/white endpoints) in both planes. Replaying
          // only the CBZ image into an all-zero strip used to erase those
          // pixels before the gray waveform and made page transitions look
          // speckled/ghosted outside the picture.
          if (baseFrameBuffer) {
            std::memcpy(scratch.get(), baseFrameBuffer + static_cast<size_t>(stripY) * displayWidthBytes,
                        static_cast<size_t>(rows) * displayWidthBytes);
          } else {
            renderer.clearScreen(0x00);
          }
          const uint32_t cacheStartedMs = millis();
          if (!ImageBlock::renderFromPixelCacheBand(renderer, renderedCachePath, renderedX, renderedY, renderedWidth,
                                                    renderedHeight)) {
            grayscaleCacheValid = false;
          }
          cacheRenderMs += millis() - cacheStartedMs;
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(lsbPlanes[planeIndex], scratch.get(), stripY, rows);
        }
      }
      renderer.setRenderMode(GfxRenderer::BW);
      if (grayscaleCacheValid) {
        renderer.displayGrayBuffer();
        renderer.cleanupGrayscaleWithFrameBuffer();
      } else {
        LOG_ERR("CBZR", "Skipping CBZ grayscale overlay: current pixel cache became unavailable");
        renderer.cleanupGrayscaleWithFrameBuffer();
      }
      const auto cacheStats = ImageBlock::getPixelCacheReplayStats();
      const char* cacheStrategy = cacheStats.bandsRead > 0
                                      ? (landscapeMode ? "landscape-active-band" : "bounded-active-band")
                                      : "streaming-fallback";
      LOG_DBG("CBZCACHE", "page=%lu mode=%s pan=%d,%d cachePasses=%lu bandsRead=%lu replayCalls=%lu bytesRead=%lu cacheMs=%lums strategy=%s",
              static_cast<unsigned long>(currentPage + 1), viewModeName(viewMode), panX, panY,
              static_cast<unsigned long>(cacheStats.cachePasses), static_cast<unsigned long>(cacheStats.bandsRead),
              static_cast<unsigned long>(cacheStats.replayCalls), static_cast<unsigned long>(cacheStats.bytesRead),
              static_cast<unsigned long>(cacheRenderMs),
              cacheStrategy);
    } else {
      LOG_ERR("CBZR", "Skipping CBZ grayscale planes: %d-byte strip scratch unavailable",
              displayWidthBytes * STRIP_ROWS);
      renderer.setRenderMode(GfxRenderer::BW);
    }
  }
  if (rendered) {
    LOG_DBG("CBZPAGE", "from=%lu to=%lu stage=display result=ok",
            previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
            static_cast<unsigned long>(currentPage + 1));
  }
  if (diagnosticsActive) {
    const char* reason = fallbackDecoder ? "long_huffman" : "normal";
    LOG_DBG("CBZIMG", "decoder=%s reason=%s source=%dx%d target=%dx%d mode=%s zoom=%u area=%d dithering=%d logicalGrayLevels=%d renderMode=%s threshold=%d tone=%s",
            selectedDecoder, reason, imageDimensions.width, imageDimensions.height, diagnosticTargetWidth,
            diagnosticTargetHeight,
            viewModeName(viewMode),
            static_cast<unsigned>(zoomLevel), imageDiagnostics.areaResampling ? 1 : 0, cbzDithering ? 1 : 0,
            nativeBwMode ? 2 : 4, nativeBwMode ? "BW" : "GRAY4", nativeBwMode ? 128 : 0,
            nativeBwMode ? "bw_diagnostic" : "cbz_manga");
    LOG_DBG("CBZIMG", "logical_map=0:black 1:dark_gray 2:light_gray 3:white planes=%s cache=%s coordinates=image-local",
            directRenderActive || nativeBwMode ? "bypassed" : "LSB(level1)+MSB(level1,2)",
            directRenderActive ? "none" : "pxc-2bpp");
    LOG_DBG("CBZQUALITY", "render=%s resize=%s source=%dx%d target=%dx%d grayBins=%lu,%lu,%lu,%lu quant4=%lu,%lu,%lu,%lu threshold=%d grayLevels=%d dithering=%d contrast=off sharpen=off",
            directRenderActive ? "bw_direct" : (nativeBwMode ? "bw_diagnostic" : "gray4"), imageDiagnostics.resampleMode, imageDimensions.width,
            imageDimensions.height, diagnosticTargetWidth,
            diagnosticTargetHeight, static_cast<unsigned long>(imageDiagnostics.grayBins[0]),
            static_cast<unsigned long>(imageDiagnostics.grayBins[1]),
            static_cast<unsigned long>(imageDiagnostics.grayBins[2]),
            static_cast<unsigned long>(imageDiagnostics.grayBins[3]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[0]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[1]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[2]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[3]), nativeBwMode ? 128 : 0,
            nativeBwMode ? 2 : 4, cbzDithering ? 1 : 0);
    LOG_DBG("CBZSTAGE", "decode=ok resize=%s native_prepare=%s plane_compose=%s display=%s",
            imageDiagnostics.resampleMode,
            directRenderActive ? "direct-BW" : (nativeBwMode ? "pxc-2bpp" : "pxc-2bpp"),
            directRenderActive || nativeBwMode ? "bypassed" : (renderer.supportsStripGrayscale() ? "LSB+MSB" : "BW-only"),
            directRenderActive ? "BW-native" : (nativeBwMode ? "BW-native" : (renderer.supportsStripGrayscale() ? "GRAY4-2plane" : "BW")));
    if (directRenderActive) {
      LOG_DBG("CBZDIRECT",
              "source=%dx%d target=%dx%d viewport=%dx%d pan=%d,%d bounds=%d,%d scale=1.000 resample=none decoder=TJpgDec render=BW-native threshold=128 pxc=bypassed cache=none",
              imageDimensions.width, imageDimensions.height, diagnosticTargetWidth, diagnosticTargetHeight,
              diagnosticViewportWidth, diagnosticViewportHeight, panX, panY, currentMaxPanX,
              currentMaxPanY);
      LOG_DBG("CBZSTAGE", "decode=ok resize=none pxc_pack=bypassed pxc_replay=bypassed plane_compose=bypassed display=BW-native");
    }
    LOG_DBG("CBZIMG",
            "stages=gray8[%u..%u] grayBins=%lu,%lu,%lu,%lu quant=%s quantBins=%lu,%lu,%lu,%lu pixels=%lu cache=%s framebuffer=%s",
            static_cast<unsigned>(imageDiagnostics.grayMin), static_cast<unsigned>(imageDiagnostics.grayMax),
            static_cast<unsigned long>(imageDiagnostics.grayBins[0]),
            static_cast<unsigned long>(imageDiagnostics.grayBins[1]),
            static_cast<unsigned long>(imageDiagnostics.grayBins[2]),
            static_cast<unsigned long>(imageDiagnostics.grayBins[3]),
            directRenderActive || nativeBwMode ? "bw_threshold_128" : "quant4",
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[0]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[1]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[2]),
            static_cast<unsigned long>(imageDiagnostics.quantizedBins[3]),
            static_cast<unsigned long>(imageDiagnostics.emittedPixels), directRenderActive ? "none" : "pxc-2bpp",
            directRenderActive || nativeBwMode ? "BW-native" : (renderer.supportsStripGrayscale() ? "GRAY4-2plane" : "BW"));
    LOG_DBG("CBZPERF", "extract=%lums probe=%lums decoder=%lums decode=%lums resample=%lums dither=%lums cache=%lums render=%lums refresh=%lums",
            static_cast<unsigned long>(extractMs), static_cast<unsigned long>(probeMs),
            static_cast<unsigned long>(decoderSelectMs), static_cast<unsigned long>(imageDiagnostics.decodeMs),
            static_cast<unsigned long>(imageDiagnostics.resampleUs / 1000U),
            static_cast<unsigned long>(imageDiagnostics.pixelEmitUs / 1000U),
            static_cast<unsigned long>(imageDiagnostics.cacheWriteUs / 1000U), static_cast<unsigned long>(renderMs),
            static_cast<unsigned long>(refreshMs));
    LOG_DBG("CBZPERF", "stage_note=dither_is_pixel_emit_and_includes_framebuffer_or_cache_band_writes");
#if NOOIR_CBZ_QUALITY_DIAGNOSTICS
    qualityProbe.logSummary(imagePath.c_str());
#endif
  }
  LOG_DBG("CBZPERF", "foreground_ms=%lums prefetch_wait_ms=%lums cache_replay_ms=%lums",
          static_cast<unsigned long>(renderMs), static_cast<unsigned long>(prefetchWaitMs),
          static_cast<unsigned long>(cacheRenderMs));
  LOG_DBG("CBZPERF", "page=%lu total_ms=%lu prefetch_wait_ms=%lu cache_ms=%lu refresh_ms=%lu",
          static_cast<unsigned long>(currentPage + 1), static_cast<unsigned long>(renderMs),
          static_cast<unsigned long>(prefetchWaitMs), static_cast<unsigned long>(cacheRenderMs),
          static_cast<unsigned long>(refreshMs));
  prefetchWaitMs = 0;
  if (rendered) {
    LOG_DBG("CBZPAGE", "from=%lu to=%lu stage=cleanup result=ok",
            previousRenderedPage == SIZE_MAX ? 0UL : static_cast<unsigned long>(previousRenderedPage + 1),
            static_cast<unsigned long>(currentPage + 1));
  }
  longOperation.complete();
  // Keep the bounded pixel-cache slot resident for same-page pan redraws.
  // It is explicitly released on page/mode changes, reader exit, and before
  // a new page candidate is rendered, so this does not retain old pages.
  renderer.setOrientation(previousOrientation);
}

void CbzReaderActivity::renderStatusBar() const {
  const int pageCount = static_cast<int>(cbz->getPageCount());
  const int displayedPage = pageCount > 0 ? std::min(static_cast<int>(currentPage) + 1, pageCount) : 0;
  const float progress = pageCount > 0 ? (static_cast<float>(displayedPage) * 100.0f) / pageCount : 0.0f;
  const std::string title = SETTINGS.statusBarSpec().showsTitle() ? cbz->getTitle() : std::string();
  GUI.drawStatusBar(renderer, progress, displayedPage, pageCount, title, 0, 0, true,
                    isCurrentPageBookmarked());
}

void CbzReaderActivity::saveProgress() {
  if (!cbz || (!progressDirty && lastPersistedPage == currentPage)) return;
  uint8_t data[PROGRESS_BYTES];
  const uint32_t page = static_cast<uint32_t>(currentPage);
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = (page >> 16) & 0xFF;
  data[3] = (page >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(cbz->getCachePath(), data, sizeof(data))) {
    LOG_ERR("CBZR", "Failed to save progress: page %lu", static_cast<unsigned long>(page));
    return;
  }
  lastPersistedPage = currentPage;
  progressDirty = false;
}

void CbzReaderActivity::loadProgress() {
  lastPersistedPage = currentPage;
  progressDirty = false;
  HalFile file;
  if (!Storage.openFileForRead("CBZR", cbz->getCachePath() + "/progress.bin", file)) return;
  uint8_t data[PROGRESS_BYTES]{};
  if (file.read(data, sizeof(data)) == sizeof(data)) {
    currentPage = static_cast<size_t>(data[0]) | (static_cast<size_t>(data[1]) << 8) |
                  (static_cast<size_t>(data[2]) << 16) | (static_cast<size_t>(data[3]) << 24);
    if (currentPage > cbz->getPageCount()) currentPage = 0;
  }
  file.close();
  lastPersistedPage = currentPage;
}

ScreenshotInfo CbzReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Cbz;
  if (!cbz) return info;
  const std::string title = cbz->getTitle();
  snprintf(info.title, sizeof(info.title), "%s", title.c_str());
  info.totalPages = static_cast<int>(cbz->getPageCount());
  info.currentPage = info.totalPages > 0 ? std::min(static_cast<int>(currentPage) + 1, info.totalPages) : 0;
  info.progressPercent = info.totalPages > 0 ? (info.currentPage * 100) / info.totalPages : 0;
  return info;
}
