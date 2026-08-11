#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

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
  unsigned long retrievingBookCacheStartedMs = 0;
  // First visit after an install/cache clear: build missing recent-book
  // metadata and thumbnails one book at a time while showing feedback.
  bool recentCacheWarmupActive = false;
  unsigned long recentCacheWarmupNextMs = 0;
  volatile bool recentCacheWarmupPopupRendered = false;
  bool snapshotRestored = false;
  size_t snapshotPageStart = SIZE_MAX;
  size_t snapshotSelectorIndex = SIZE_MAX;
  size_t lastRenderedSelectorIndex = SIZE_MAX;
  size_t lastRenderedPageStart = SIZE_MAX;
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

  // Data loading
  void loadRecentBooks();
  void rebuildVisibleBooks();
  size_t selectedRecentIndex() const;
  bool hasMissingRecentCache() const;
  bool hasAnyRecentCache() const;
  void generateNextCover();
  void showMenu();
  void showBookActions();
  uint64_t snapshotKey() const;
  bool restoreSnapshot();
  void writeSnapshot();

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
