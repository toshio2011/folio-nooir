#include "XtcReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdlib>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkFile.h"

namespace {
constexpr const char* kXtcBookmarkPrefix = "xtc-page:";

bool readBookmarkedPage(const BookmarkEntry& bookmark, uint32_t& page) {
  if (bookmark.xpath.compare(0, 9, kXtcBookmarkPrefix) != 0) return false;
  const char* value = bookmark.xpath.c_str() + 9;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed > UINT32_MAX) return false;
  page = static_cast<uint32_t>(parsed);
  return true;
}
}  // namespace

void XtcReaderBookmarksActivity::onEnter() {
  Activity::onEnter();
  std::vector<BookmarkEntry> loaded;
  BookmarkFile::load(bookPath, loaded);
  bookmarks.reserve(loaded.size());
  for (auto& bookmark : loaded) {
    uint32_t page = 0;
    if (readBookmarkedPage(bookmark, page)) bookmarks.push_back(std::move(bookmark));
  }
  requestUpdate();
}

void XtcReaderBookmarksActivity::onExit() { Activity::onExit(); }

void XtcReaderBookmarksActivity::loop() {
  const int count = static_cast<int>(bookmarks.size());
  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int listTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = screen.height - (listTop - screen.y) - metrics.verticalSpacing * 2;
  auto openSelected = [this, count] {
    if (count == 0) return;
    uint32_t page = 0;
    if (readBookmarkedPage(bookmarks[selectedIndex], page)) {
      setResult(PageResult{page});
      finish();
    }
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (handleListTouch(selectedIndex, count, listTop, listHeight, true) == ListTouchResult::Activated) {
    openSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && count > 0) {
    openSelected();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up && count > 0) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, count, GUI.getListPageItems(listHeight, true));
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down && count > 0) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, count, GUI.getListPageItems(listHeight, true));
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, count] {
    if (count == 0) return;
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    if (count == 0) return;
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void XtcReaderBookmarksActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();

  auto& theme = UITheme::getInstance();
  const auto metrics = theme.getMetrics();
  const Rect screen = theme.getScreenSafeArea(renderer, true, false);
  const int titleY = screen.y + metrics.topPadding;
  const int listTop = titleY + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = screen.height - (listTop - screen.y) - metrics.verticalSpacing * 2;
  GUI.drawHeader(renderer, Rect{screen.x, titleY, screen.width, metrics.headerHeight}, tr(STR_BOOKMARKS));

  if (bookmarks.empty()) {
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, listTop + listHeight / 2, tr(STR_NONE_OPT), true);
  } else {
    GUI.drawList(
        renderer, Rect{screen.x, listTop, screen.width, listHeight}, static_cast<int>(bookmarks.size()), selectedIndex,
        [this](int index) { return bookmarks[index].summary; },
        [this](int index) {
          const auto& bookmark = bookmarks[index];
          const int percent = std::clamp(static_cast<int>(bookmark.percentage * 100.0f + 0.5f), 0, 100);
          return std::to_string(percent) + "%";
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), bookmarks.empty() ? "" : tr(STR_SELECT),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
