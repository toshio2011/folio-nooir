#include "EpubReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "../../util/BookmarkFile.h"
#include "BookStateStore.h"
#include "MappedInputManager.h"
#include <FsHelpers.h>
#include "ProgressMapper.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;

// Layout constants used in renderScreen
constexpr int LINE_HEIGHT = 60;
}  // namespace

void EpubReaderBookmarksActivity::onEnter() {
  Activity::onEnter();
  swallowInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  bookmarks.clear();
  if (allBooks) {
    for (const auto& recent : RECENT_BOOKS.getBooks()) {
      if (!FsHelpers::hasEpubExtension(recent.path)) continue;
      std::vector<BookmarkEntry> bookBookmarks;
      if (!BookmarkFile::load(recent.path, bookBookmarks)) continue;
      for (auto& bookmark : bookBookmarks) {
        bookmarks.push_back(BookmarkItem{recent.path, recent.title, std::move(bookmark)});
      }
    }
    for (const auto& state : BOOK_STATES.getBooks()) {
      if (!FsHelpers::hasEpubExtension(state.path) ||
          std::find_if(bookmarks.begin(), bookmarks.end(), [&](const BookmarkItem& item) {
            return item.bookPath == state.path;
          }) != bookmarks.end())
        continue;
      std::vector<BookmarkEntry> bookBookmarks;
      if (!BookmarkFile::load(state.path, bookBookmarks)) continue;
      for (auto& bookmark : bookBookmarks) {
        bookmarks.push_back(BookmarkItem{state.path, {}, std::move(bookmark)});
      }
    }
  } else {
    if (!epub) {
      epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
      if (!epub->load(false, true)) {
        epub.reset();
        return;
      }
    }
    std::vector<BookmarkEntry> bookBookmarks;
    if (BookmarkFile::load(epubPath, bookBookmarks)) {
      for (auto& bookmark : bookBookmarks) {
        bookmarks.push_back(BookmarkItem{epubPath, {}, std::move(bookmark)});
      }
    }
  }
  LOG_DBG("EPB", "Loaded %d bookmarks (%s)", static_cast<int>(bookmarks.size()), allBooks ? "all books" : epubPath.c_str());

  // Trigger first update
  requestUpdate();
}

void EpubReaderBookmarksActivity::onExit() { Activity::onExit(); }

int EpubReaderBookmarksActivity::getGutterBottom(const GfxRenderer& renderer) {
  const auto orientation = renderer.getOrientation();
  const bool isPortrait = orientation == GfxRenderer::Orientation::Portrait;
  return isPortrait ? 75 : 40;  // Reserve vertical space for button hints at the bottom
}

int EpubReaderBookmarksActivity::getListHeight(const GfxRenderer& renderer) {
  const auto pageHeight = renderer.getScreenHeight();
  return pageHeight - getGutterBottom(renderer) - LINE_HEIGHT;  // Reserve vertical space for title and button hints
}

void EpubReaderBookmarksActivity::loop() {
  renderer.setUiScaleTextEnabled(true);
  auto openBookmark = [this] {
    if (bookmarks.empty()) {
      return;
    }
    const auto& item = bookmarks.at(selectorIndex);
    if (allBooks && (epubPath != item.bookPath || !epub)) {
      epubPath = item.bookPath;
      epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
      if (!epub->load(false, true)) {
        epub.reset();
        return;
      }
    }
    if (!epub) return;
    const auto& bookmark = item.bookmark;
    ProgressChangeResult result{};
    result.xpath = bookmark.xpath;
    result.percentage = bookmark.percentage;
    result.hasSavedProgress = true;
    result.bookPath = item.bookPath;
    if (bookmark.computedChapterPageCount > 0 && bookmark.computedChapterProgress < bookmark.computedChapterPageCount &&
        bookmark.computedSpineIndex < epub->getSpineItemsCount()) {
      result.spineIndex = bookmark.computedSpineIndex;
      result.page = bookmark.computedChapterProgress;
      result.totalPages = bookmark.computedChapterPageCount;
    } else if (!bookmark.xpath.empty()) {
      // Older bookmark files may not have the computed chapter fields. Resolve
      // their saved XPath/percentage while the selected EPUB is already open,
      // so the reader still lands on the actual bookmark instead of page 1.
      const auto mapped = ProgressMapper::toCrossPoint(epub, {bookmark.xpath, bookmark.percentage}, renderer);
      result.spineIndex = mapped.spineIndex;
      result.page = mapped.pageNumber;
      result.totalPages = mapped.totalPages;
    }
    setResult(std::move(result));
    finish();
  };

  // Delete confirmation popup
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (confirmingDelete) {
    // Popup dismissed without a selection (Back button or tap outside): cancel delete
    confirmingDelete = false;
    requestUpdate();
    return;
  }

  if (swallowInitialConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      swallowInitialConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;
  const int listY = contentY + LINE_HEIGHT;
  const int listHeight = getListHeight(renderer);
  int tapped = 0;
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTouchDown(tx, ty) && tx >= contentX && tx < contentX + contentWidth &&
      mappedInput.wasListItemTouchedDown(tapped, static_cast<int>(bookmarks.size()), selectorIndex, listY, listHeight,
                                         true)) {
    if (selectorIndex != tapped) {
      selectorIndex = tapped;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty) && tx >= contentX && tx < contentX + contentWidth &&
      mappedInput.wasListItemTapped(tapped, static_cast<int>(bookmarks.size()), selectorIndex, listY, listHeight,
                                    true)) {
    selectorIndex = tapped;
    openBookmark();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up && !bookmarks.empty()) {
    selectorIndex =
        ButtonNavigator::nextPageIndex(selectorIndex, bookmarks.size(), GUI.getListPageItems(listHeight, true));
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down && !bookmarks.empty()) {
    selectorIndex =
        ButtonNavigator::previousPageIndex(selectorIndex, bookmarks.size(), GUI.getListPageItems(listHeight, true));
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {  // Open
    openBookmark();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    if (bookmarks.empty()) {
      return;
    }
    confirmingDelete = true;
    const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
    confirmPopup.show(tr(STR_CONFIRM_DELETE_BOOKMARK), options, 2, 0, [this](int idx) {
      confirmingDelete = false;
      if (idx == 1) {
        deleteSelectedBookmark();
      }
      requestUpdate();
    });
    requestUpdate();
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, bookmarks.size());
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, bookmarks.size());
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, bookmarks.size(),
                                                   GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, bookmarks.size(),
                                                       GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });
}

void EpubReaderBookmarksActivity::deleteSelectedBookmark() {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(bookmarks.size())) return;
  const std::string sourcePath = bookmarks[selectorIndex].bookPath;
  bookmarks.erase(bookmarks.begin() + selectorIndex);
  if (!allBooks) {
    std::vector<BookmarkEntry> remaining;
    remaining.reserve(bookmarks.size());
    for (const auto& item : bookmarks) remaining.push_back(item.bookmark);
    if (!BookmarkFile::save(epubPath, remaining)) {
      LOG_ERR("EPB", "Failed to save bookmarks after delete");
    }
  } else {
    std::vector<BookmarkEntry> remaining;
    for (const auto& item : bookmarks) {
      if (item.bookPath == sourcePath) remaining.push_back(item.bookmark);
    }
    if (!BookmarkFile::save(sourcePath, remaining)) {
      LOG_ERR("EPB", "Failed to save bookmarks after delete");
    }
  }

  // Move selector up if we deleted the last item
  if (selectorIndex >= bookmarks.size() && selectorIndex > 0) {
    selectorIndex--;
  }

  if (bookmarks.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
}

void EpubReaderBookmarksActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: reserve a horizontal gutter for button hints.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: reserve vertical space for hints at the top.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const bool isPortrait = orientation == GfxRenderer::Orientation::Portrait;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int hintGutterBottom = getGutterBottom(renderer);
  const int contentY = hintGutterHeight;
  const int listY = contentY + LINE_HEIGHT;  // Reserve vertical space for title
  const int listHeight = getListHeight(renderer);
  const int numBookmarks = bookmarks.size();

  // Manual centering to honor content gutters.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_BOOKMARKS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_BOOKMARKS), true, EpdFontFamily::BOLD);

  const auto getBookmarkTitle = [this](int index) {
    return bookmarks.at(confirmingDelete ? selectorIndex : index).bookmark.summary;
  };
  const auto getBookmarkSubtitle = [this](int index) {
    const auto& item = bookmarks.at(confirmingDelete ? selectorIndex : index);
    const auto& bookmark = item.bookmark;
    std::string subtitle = std::to_string((int)(std::clamp(bookmark.percentage, 0.0f, 1.0f) * 100.0f + 0.5f)) + "% - ";
    if (bookmark.computedChapterPageCount > 0) {
      subtitle += std::to_string(bookmark.computedChapterProgress + 1) + "/" +
                  std::to_string(bookmark.computedChapterPageCount) + " - ";
    }
    if (allBooks) return item.bookTitle + " - " + subtitle + tr(STR_BOOKMARKS);
    auto tocIndex = epub->getTocIndexForSpineIndex(bookmark.computedSpineIndex);
    auto tocTitle = (tocIndex >= 0) ? (epub->getTocItem(tocIndex)).title : tr(STR_UNNAMED);
    return subtitle + tocTitle;
  };
  const auto getBookmarkIcon = [isPortrait](int index) {
    // only enabled icon in portrait mode due to limitation with rotating icons for other orientations
    return isPortrait ? UIIcon::Bookmark : UIIcon::None;
  };

  if (numBookmarks > 0) {
    if (confirmingDelete) {
      // Render just the selected item near the top; the confirmation popup occupies the center
      GUI.drawList(renderer, Rect{contentX, listY, contentWidth, LINE_HEIGHT}, 1, 0, getBookmarkTitle,
                   getBookmarkSubtitle, getBookmarkIcon);
    } else {
      GUI.drawList(renderer, Rect{contentX, listY, contentWidth, listHeight}, numBookmarks, selectorIndex,
                   getBookmarkTitle, getBookmarkSubtitle, getBookmarkIcon);

      GUI.drawHelpText(renderer, Rect{contentX, pageHeight - hintGutterBottom, contentWidth, LINE_HEIGHT},
                       tr(STR_HOLD_OPEN_TO_DELETE));
    }
  }

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  const auto confirmLabel = bookmarks.size() > 0 ? tr(STR_SELECT) : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
