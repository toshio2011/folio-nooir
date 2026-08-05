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
  bool snapshotRestored = false;
  bool snapshotWritePending = false;
  bool initialRenderPending = true;
  bool swallowMenuBackRelease = false;
  bool longPressActionShown = false;
  bool swallowBookConfirmRelease = false;
  bool swallowBookBackRelease = false;

  static constexpr int BOOKS_PER_PAGE = 8;
  static constexpr int BOOKSHELF_COVER_HEIGHT = 150;

  // Data loading
  void loadRecentBooks();
  void rebuildVisibleBooks();
  size_t selectedRecentIndex() const;
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
