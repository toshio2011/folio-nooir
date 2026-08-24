#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
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
  // Optional high-quality Carousel covers are prepared only by the explicit
  // Home-menu action. Missing HQ files never block normal shelf navigation.
  std::vector<size_t> carouselHqQueue;
  size_t carouselHqQueueIndex = 0;
  bool carouselHqPreparationActive = false;
  volatile bool carouselHqPopupRendered = false;
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
