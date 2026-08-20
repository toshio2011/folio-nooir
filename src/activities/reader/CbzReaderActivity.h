#pragma once

#include <Cbz.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BookmarkEntry.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/LongOperationIndicator.h"
#include "components/OptionPopup.h"

class CbzReaderActivity final : public Activity {
 public:
  enum class ViewMode : uint8_t { FitWidth, FitPage, Landscape, Zoom, SourceOneToOne };

 private:

  std::unique_ptr<Cbz> cbz;
  size_t currentPage = 0;
  int pagesUntilFullRefresh = 0;
  unsigned long readingSessionStartedMs = 0UL;
  uint32_t sessionPagesTurned = 0;
  bool pageLoadFailed = false;
  ViewMode viewMode = ViewMode::FitWidth;
  uint8_t zoomLevel = 0;
  int panX = 0;
  int panY = 0;
  int currentMaxPanX = 0;
  int currentMaxPanY = 0;
  bool zoomLongPressFired = false;
  // Temporary CBZ-only A/B test.  Normal comic rendering remains GRAY4;
  // diagnostic mode emits black/white pixels and skips the gray-plane pass.
  bool bwDiagnosticMode = false;
  // Temporary source-native B/W comparison. This is independent from the
  // general Manga B/W toggle so the two physical tests can be compared.
  bool sourceOneToOneBwMode = false;
  // Temporary direct-framebuffer A/B test. Unlike Source 1:1 B/W, this
  // deliberately bypasses the persistent CBZ PixelCache.
  bool directBwDiagnosticMode = false;
  bool directCropDumpRequested = false;
  bool directCropDumpFired = false;
  bool renderBusy = false;
  // Page requests use a short event-loop phase to show the loading line on
  // the already-presented page before the synchronous render starts.
  bool loadingUiPending = false;
  bool loadingUiRenderArmed = false;
  bool pendingNavigation = false;
  bool pendingNavigationCancelPrefetch = false;
  MappedInputManager::Button pendingNavigationButton = MappedInputManager::Button::Right;
  ViewMode pendingNavigationViewMode = ViewMode::FitWidth;
  bool busyLogEmitted = false;
  int busyLogButton = -1;
  bool staleLogEmitted = false;
  int staleLogButton = -1;
  bool inputGateAfterRender = false;
  bool restorePreviousPageBottom = false;
  size_t renderedImagePage = SIZE_MAX;
  std::string currentImagePath;
  std::string activeRenderCachePath;
  bool forceRenderCacheRebuild = false;
  // Conservative one-page lookahead. X4 may pre-render the next page during
  // an idle window; X3 is deliberately guarded out until its timing/heap
  // path can be validated independently.
  bool prefetchReady = false;
  // Prefetch and foreground rendering share the same decoder/cache resources.
  // This gate makes cancellation explicit: a foreground render may not start
  // until the prefetch owner has released its files and decoder state.
  bool prefetchRunning = false;
  bool prefetchCancelRequested = false;
  uint32_t prefetchGeneration = 0;
  size_t prefetchPage = SIZE_MAX;
  size_t prefetchAttemptedPage = SIZE_MAX;
  size_t prefetchSkipLoggedPage = SIZE_MAX;
  ViewMode prefetchViewMode = ViewMode::FitWidth;
  uint8_t prefetchZoomLevel = 0;
  std::string prefetchImagePath;
  std::string prefetchCachePath;
  int prefetchSourceWidth = 0;
  int prefetchSourceHeight = 0;
  unsigned long prefetchWaitStartedMs = 0UL;
  uint32_t prefetchWaitMs = 0;
  bool cacheOnlyCurrentPage = false;
  int currentCachedWidth = 0;
  int currentCachedHeight = 0;
  unsigned long lastRenderCompleteMs = 0UL;
  OptionPopup viewModePopup;
  OptionPopup directionPopup;
  bool pagePickerActive = false;
  size_t pagePickerSelection = 0;
  bool bookmarksActive = false;
  size_t bookmarkSelection = 0;
  bool bookmarksLoaded = false;
  bool bookmarkLongPressFired = false;
  std::vector<BookmarkEntry> bookmarks;
  LongOperationIndicator longOperationIndicator;
  // Navigation direction changes page order only; image content and geometry
  // remain exactly the same. It is intentionally session-scoped until a
  // per-book settings format is established for CBZ.
  bool readingRtl = false;
  size_t lastPersistedPage = SIZE_MAX;
  bool progressDirty = false;
  // Diagnostic-only render transition state.  It is deliberately tiny and is
  // not used for pagination or cache invalidation.
  bool hasRenderedRefreshState = false;
  size_t lastRefreshPage = SIZE_MAX;
  ViewMode lastRefreshViewMode = ViewMode::FitWidth;
  uint8_t lastRefreshZoom = 0;
  int lastRefreshPanX = 0;
  int lastRefreshPanY = 0;

  void renderPage();
  void requestPageRender(bool immediate = false);
  void renderStatusBar() const;
  void saveProgress();
  void loadProgress();
  void clearRenderCache();
  void clearCurrentImage();
  void openViewModePopup();
  void openPagePicker();
  void handlePagePickerInput();
  void renderPagePicker();
  void openBookmarks();
  void handleBookmarksInput();
  void renderBookmarks();
  void loadBookmarks();
  void saveBookmarks();
  void toggleBookmark();
  bool isCurrentPageBookmarked() const;
  void openDirectionPopup();
  void resetView();
  void applyViewMode(int index);
  void toggleBwDiagnosticMode();
  void adjustZoom(int direction);
  void panBy(int dx, int dy);
  void handleLandscapeHorizontalNavigation(MappedInputManager::Button button);
  bool handlePageTurn(bool previous, bool resolveVerticalPan = true);
  void cancelPrefetch(const char* reason);
  bool isQueueableNavigationButton(MappedInputManager::Button button) const;
  bool isLogicalNextButton(MappedInputManager::Button button) const;
  void logPageIntent(bool next) const;
  void queueNavigationButton(MappedInputManager::Button button, const char* state);
  void clearPendingNavigation(const char* reason);
  void executePendingNavigation();
  void maybePrefetchNextPage();
  bool renderNextPageToCache(size_t page, const std::string& imagePath, const std::string& cachePath,
                             size_t& freeBefore, size_t& freeAfter, size_t& maxAllocBefore,
                             size_t& maxAllocAfter);

 public:
  explicit CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Cbz> cbz)
      : Activity("CbzReader", renderer, mappedInput), cbz(std::move(cbz)), longOperationIndicator(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
