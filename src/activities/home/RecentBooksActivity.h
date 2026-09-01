#pragma once
#include <I18n.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/CoverStackGeometry.h"
#include "components/OptionPopup.h"

class RecentBooksActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;
  OptionPopup menuPopup;
  OptionPopup bookActionsPopup;

  size_t selectorIndex = 0;
  uint8_t activeTab = 1;  // 1 Recent/ongoing, 2 Finished; Library opens the browser
  uint8_t visibleBookIndexes[10] = {};
  uint8_t visibleBookCount = 0;

  // Recent tab state
  std::vector<RecentBook> recentBooks;
  size_t nextCoverToGenerate = 0;
  bool coverGenerationActive = false;
  bool coverGenerationRequested = false;
  bool manualSingleRefresh = false;
  bool retrievingBookCache = false;
  uint8_t retrievingBookCacheProgress = 0;
  size_t retrievingBookCacheIndex = SIZE_MAX;
  volatile bool retrievingBookCachePopupRendered = false;
  // Navigation must never cache bitmap pixels, but it can remember that a
  // prepared 360px cover was unavailable during this activity session. This
  // avoids reopening the same missing HQ file on every Carousel redraw.
  struct CarouselHqProbe {
    bool valid = false;
    uint64_t identity = 0;
    bool unavailable = false;
  };
  std::array<CarouselHqProbe, 10> carouselHqProbes{};
  static constexpr size_t CAROUSEL_SOURCE_CACHE_SIZE = 6;
  struct CarouselSourceCacheEntry {
    bool valid = false;
    uint32_t lastUsed = 0;
    std::string path;
    HalFile file;
    std::unique_ptr<Bitmap> bitmap;
  };
  struct CarouselSourceTiming {
    bool reused = false;
  };
  static constexpr size_t CAROUSEL_FRAME_SLOT_COUNT = 5;
  static constexpr size_t CAROUSEL_FRAME_MAX_PROTECTED_PATHS = 6;
  struct CarouselSourceFrame {
    struct Slot {
      bool valid = false;
      bool hqAttempted = false;
      bool resolvedHq = false;
      bool resolvedFromCache = false;
      std::string primaryPath;
      std::string fallbackPath;
      std::string resolvedPath;
    };
    std::array<Slot, CAROUSEL_FRAME_SLOT_COUNT> slots{};
    std::array<std::string, CAROUSEL_FRAME_MAX_PROTECTED_PATHS> protectedPaths{};
    size_t protectedCount = 0;

    void protectPath(const std::string& path) {
      if (path.empty()) return;
      for (size_t i = 0; i < protectedCount; ++i) {
        if (protectedPaths[i] == path) return;
      }
      if (protectedCount < protectedPaths.size()) protectedPaths[protectedCount++] = path;
    }

    bool protects(const std::string& path) const {
      for (size_t i = 0; i < protectedCount; ++i) {
        if (protectedPaths[i] == path) return true;
      }
      return false;
    }

    void retainResolvedPaths() {
      protectedPaths = {};
      protectedCount = 0;
      for (const auto& slot : slots) protectPath(slot.resolvedPath);
    }
  };
  std::array<CarouselSourceCacheEntry, CAROUSEL_SOURCE_CACHE_SIZE> carouselSourceCache{};
  uint32_t carouselSourceCacheClock = 0;
  // Optional high-quality Carousel covers are prepared only by the explicit
  // Home-menu action. Missing HQ files never block normal shelf navigation.
  std::vector<size_t> carouselHqQueue;
  size_t carouselHqQueueIndex = 0;
  bool carouselHqPreparationActive = false;
  volatile bool carouselHqPopupRendered = false;
  bool carouselHqCompletionPopupPending = false;
  // Refresh Book Cache regenerates 360px only when that exact file existed
  // before the refresh; ordinary refreshes must not create HQ files.
  std::string refreshCarouselHqPath;
  // One-time post-install/update bootstrap: build missing recent-book
  // metadata one book at a time while showing feedback. Thumbnails are
  // intentionally left to the explicit Refresh Book Cache action.
  bool recentCacheWarmupActive = false;
  bool recentCacheBootstrapProbePending = false;
  unsigned long recentCacheWarmupNextMs = 0;
  volatile bool recentCacheWarmupPopupRendered = false;
  bool snapshotRestored = false;
  size_t snapshotPageStart = SIZE_MAX;
  size_t snapshotSelectorIndex = SIZE_MAX;
  // When the persisted frame is still valid (for example after leaving
  // Settings), one panel refresh is enough. The first render can reuse the
  // frame instead of rebuilding all cover/text geometry.
  bool snapshotFastPathUsed = false;
  size_t lastRenderedSelectorIndex = SIZE_MAX;
  size_t lastRenderedPageStart = SIZE_MAX;
  uint8_t lastRenderedTab = 0;
  // The selector can point to a different book after Recent reorders entries
  // when a reader closes. Track the actual featured book instead of relying on
  // its numeric position in the shelf snapshot.
  std::string lastFeaturedPath;
  std::string lastFeaturedCoverPath;
  bool overlayFrameShown = false;
  bool snapshotWritePending = false;
  unsigned long snapshotWriteRequestedMs = 0;
  bool initialRenderPending = true;
  bool swallowMenuBackRelease = false;
  bool longPressActionShown = false;
  bool swallowBookConfirmRelease = false;
  bool swallowBookBackRelease = false;

  static constexpr int BOOKS_PER_PAGE = 8;
  static constexpr int BOOKSHELF_COVER_HEIGHT = 220;
  static constexpr int CAROUSEL_HQ_COVER_HEIGHT = 360;

  // Data loading
  void loadRecentBooks();
  void invalidateCarouselHqProbes();
  void invalidateCarouselHqProbesLocked();
  void invalidateCarouselSourceCache();
  void invalidateCarouselSourceCacheLocked();
  void invalidateCarouselSource(const std::string& path);
  void invalidateCarouselSourceLocked(const std::string& path);
  CarouselSourceCacheEntry* getCarouselSource(const std::string& path, CarouselSourceTiming& timing,
                                              CarouselSourceFrame* frame = nullptr);
  void prepareCarouselSourceFrame(const std::array<CoverStackSlot, CAROUSEL_FRAME_SLOT_COUNT>& stackSlots,
                                  CarouselSourceFrame& frame);
  uint64_t carouselCoverIdentity(const RecentBook& book) const;
  CarouselHqProbe& carouselHqProbe(const RecentBook& book);
  void rebuildVisibleBooks();
  size_t selectedRecentIndex() const;
  bool hasMissingRecentCache() const;
  void generateNextCover();
  void startCarouselHqPreparation();
  void prepareNextCarouselCover();
  bool generateCarouselHqThumbnail(const RecentBook& book, bool force);
  void showMenu();
  void showBookActions();
  uint64_t snapshotKey() const;
  bool restoreSnapshot();
  void writeSnapshot();
  uint8_t activeBookLayout() const;
  bool usesCarouselLayout() const;
  bool usesThreeCoverGrid() const;
  bool usesFourByTwoGrid() const;
  int activePageItems() const;
  void renderCarousel(bool threeCover);
  void renderFolioCarouselShelf(int shelfTop, int shelfHeight, bool threeCover);

  // Show an OK/Cancel prompt to remove the given book from the Recent Books list.
  void promptRemoveBook(const std::string& path, const std::string& title);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint8_t initialTab = 1)
      : Activity("RecentBooks", renderer, mappedInput), activeTab(initialTab == 2 ? 2 : 1) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
