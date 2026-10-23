/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for CrossPoint Reader
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>

#include <optional>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "activities/Activity.h"
#include "activities/reader/EpubReaderMenuActivity.h"

class XtcReaderActivity final : public Activity {
  std::shared_ptr<Xtc> xtc;

  uint32_t currentPage = 0;
  std::optional<uint32_t> initialBookmarkPage;
  uint32_t lastPersistedPage = 0;
  bool hasPersistedProgressPage = false;
  bool progressLoadUncertain = false;
  bool progressPositionChangedSinceLoad = false;
  int pagesUntilFullRefresh = 0;
  unsigned long readingSessionStartedMs = 0UL;
  uint32_t sessionPagesTurned = 0;
  bool darkShortcutFired = false;
  bool longPowerShortcutFired = false;
  bool skipNextButtonCheck = false;
  uint8_t autoTurnOption = 0;
  unsigned long nextAutoTurnMs = 0UL;
  // Render the same saving feedback as EPUB before queuing the home transition.
  bool exitHomePending = false;
  bool exitPopupShown = false;
  // Next-book suggestion menu for the End-of-Book screen
  EndOfBookOptions endOfBookOptions;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  // Opens the XTC/XTCH reader menu (short-press Confirm or the touch menu gesture).
  void openReaderMenu();
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action, int quickAction);
  void executeQuickAction(int actionValue);
  void launchKOReaderSync();
  void toggleCurrentBookmark();
  void updateAutoPageTurn(uint8_t pageTurnOption);
  void renderStatusBarOverlay(StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  bool saveProgress();
  void loadProgress();
  void setCurrentPage(uint32_t page);
  void requestExitToHome();

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Xtc> xtc,
                             std::optional<uint32_t> initialBookmarkPage = std::nullopt)
      : Activity("XtcReader", renderer, mappedInput),
        xtc(std::move(xtc)),
        initialBookmarkPage(std::move(initialBookmarkPage)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};
