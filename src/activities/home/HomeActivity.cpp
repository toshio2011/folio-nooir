#include "HomeActivity.h"

#include <Bitmap.h>
#include <Cbz.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <memory>
#include <vector>

#include "BookStateStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/home/ClockWeatherActivity.h"
#include "activities/home/ToDoListActivity.h"
#include "RecentBooksStore.h"
#include "activities/home/ReadingStatsActivity.h"
#include "activities/home/SynopsisActivity.h"
#include "activities/reader/EpubReaderBookmarksActivity.h"
#include "activities/reader/EpubReaderClippingListActivity.h"
#include "util/BookCacheUtils.h"
#include "util/CbzDiagnostics.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  while (nextRecentCover < recentBooks.size()) {
    RecentBook& book = recentBooks[nextRecentCover++];
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!isValidBookThumbnail(coverPath)) {
        if (Storage.exists(coverPath.c_str())) Storage.remove(coverPath.c_str());
        bool attempted = false;
        bool success = false;
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          if (epub.load(false, true)) {
            attempted = true;
            success = epub.generateThumbBmp(coverHeight);
          }
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            attempted = true;
            success = xtc.generateThumbBmp(coverHeight);
          }
        } else if (FsHelpers::hasCbzExtension(book.path)) {
          Cbz cbz(book.path, "/.crosspoint");
          if (cbz.loadMetadataOnly()) {
            attempted = true;
            success = cbz.generateThumbBmp(coverHeight);
          }
        }

        if (attempted && !success) {
          // A failed conversion can be transient (low heap, an interrupted
          // write, or a malformed cached BMP). Keep the source cover path so
          // Home can show its placeholder and retry later; do not erase a
          // working cover reference from RecentBooksStore.
          LOG_DBG("HOME", "Keeping cover reference after thumbnail retry failure: %s", book.path.c_str());
        }
        if (attempted) {
          // Yield back to the normal activity/render cycle after one expensive
          // decode. This keeps Home responsive instead of decoding every missing
          // recent cover in one long blocking batch.
          coverRendered = false;
          freeCoverBuffer();
          recentsLoading = false;
          if (nextRecentCover >= recentBooks.size()) recentsLoaded = true;
          requestUpdate();
          return;
        }
      }
    }
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::showMenu() {
  std::vector<std::string> options = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                      tr(STR_SETTINGS_TITLE)};
  if (hasOpdsServers) options.insert(options.begin() + 2, tr(STR_OPDS_BROWSER));

  const int fileTransferIndex = hasOpdsServers ? 3 : 2;
  const int settingsIndex = hasOpdsServers ? 4 : 3;
  const int clockWeatherIndex = settingsIndex + 1;
  const int todoListIndex = settingsIndex + 2;
  const int readingStatsIndex = settingsIndex + 3;
  const int bookmarksIndex = settingsIndex + 4;
  const int clippingsIndex = settingsIndex + 5;
  options.insert(options.end(), {tr(STR_CLOCK_WEATHER), tr(STR_TODO_LIST), tr(STR_READING_STATS),
                                 "Bookmarks (all books)", "Clippings (all books)"});

  menuPopup.show(StrId::STR_MENU, options, 0,
                 [this, fileTransferIndex, settingsIndex, clockWeatherIndex, todoListIndex, readingStatsIndex,
                  bookmarksIndex, clippingsIndex](const int index) {
    menuPopup.dismiss();
    if (index == 0) {
      activityManager.goToFileBrowser();
    } else if (index == 1) {
      activityManager.goToRecentBooks();
    } else if (hasOpdsServers && index == 2) {
      activityManager.goToBrowser();
    } else if (index == fileTransferIndex) {
      activityManager.goToFileTransfer();
    } else if (index == settingsIndex) {
      activityManager.goToSettings();
    } else if (index == clockWeatherIndex) {
      startActivityForResult(std::make_unique<ClockWeatherActivity>(renderer, mappedInput), nullptr);
    } else if (index == todoListIndex) {
      startActivityForResult(std::make_unique<ToDoListActivity>(renderer, mappedInput), nullptr);
    } else if (index == readingStatsIndex) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), nullptr);
    } else if (index == bookmarksIndex) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, std::string{}, true),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
    } else if (index == clippingsIndex) {
      startActivityForResult(std::make_unique<EpubReaderClippingListActivity>(
                                renderer, mappedInput, std::string{}, "All books", true),
                            nullptr);
    }
  });
  requestUpdate();
}

void HomeActivity::showBookActions() {
  if (selectorIndex >= recentBooks.size()) return;
  const RecentBook selectedBook = recentBooks[selectorIndex];
  std::vector<std::string> actions = {tr(STR_OPEN), tr(STR_MARK_READING), tr(STR_MARK_ON_HOLD), tr(STR_FINISHED),
                                      tr(STR_RESET_PROGRESS), tr(STR_REFRESH_BOOK_CACHE),
                                      tr(STR_DELETE_CACHE), tr(STR_REMOVE_FROM_LIST), tr(STR_READ_FULL_SYNOPSIS),
                                      tr(STR_BOOK_STATISTICS)};
  if (FsHelpers::hasEpubExtension(selectedBook.path)) {
    actions.emplace_back(tr(STR_BOOKMARKS));
    actions.emplace_back(tr(STR_CLIPPINGS));
  }

  bookActionsPopup.show(StrId::STR_BOOK_ACTIONS, actions, 0, [this](const int action) {
    if (selectorIndex >= recentBooks.size()) return;
    const RecentBook selected = recentBooks[selectorIndex];
    if (action == 0) {
      logCbzPath("home-book-selection", selected.path);
      onSelectBook(selected.path);
      return;
    }
    if (action == 1 || action == 2) {
      BOOK_STATES.setStatus(selected.path, action == 1 ? BookStatus::Reading : BookStatus::OnHold);
      if (selected.progressPercent >= 100) {
        recentBooks[selectorIndex].progressPercent = 99;
        RECENT_BOOKS.recordReading(selected.path, 99, 0);
      }
    } else if (action == 3) {
      BOOK_STATES.setStatus(selected.path, BookStatus::Finished);
      recentBooks[selectorIndex].progressPercent = 100;
      RECENT_BOOKS.recordReading(selected.path, 100, 0);
    } else if (action == 4) {
      BOOK_STATES.reset(selected.path);
      resetBookProgress(selected.path);
      recentBooks[selectorIndex].progressPercent = 0;
      RECENT_BOOKS.recordReading(selected.path, 0, 0);
    } else if (action == 5) {
      const int coverHeight = UITheme::getInstance().getMetrics().homeCoverHeight;
      if (!selected.coverBmpPath.empty()) {
        Storage.remove(UITheme::getCoverThumbPath(selected.coverBmpPath, coverHeight).c_str());
      }
      freeCoverBuffer();
      coverRendered = false;
      nextRecentCover = selectorIndex;
      recentsLoaded = false;
      recentsLoading = false;
    } else if (action == 6) {
      // Keep progress, but discard CBZ reader pages so the next open rebuilds
      // them with the current decoder/cache behavior.
      clearBookCache(selected.path);
    } else if (action == 7) {
      RECENT_BOOKS.removeByPath(selected.path);
      BOOK_STATES.removeByPath(selected.path);
      loadRecentBooks(UITheme::getInstance().getMetrics().homeRecentBooksCount);
      selectorIndex = 0;
      nextRecentCover = 0;
      recentsLoaded = false;
      recentsLoading = false;
      coverRendered = false;
      freeCoverBuffer();
    } else if (action == 8) {
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, selected.title, selected.author, selected.synopsis,
                                             selected.path),
          nullptr);
      return;
    } else if (action == 9) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, selected.path), nullptr);
      return;
    } else if (action == 10 && FsHelpers::hasEpubExtension(selected.path)) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, selected.path),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
      return;
    } else if (action == 11 && FsHelpers::hasEpubExtension(selected.path)) {
      startActivityForResult(
          std::make_unique<EpubReaderClippingListActivity>(renderer, mappedInput, selected.path, selected.title),
          nullptr);
      return;
    }
    requestUpdate(true);
  });
  requestUpdate();
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const bool menuActive = menuPopup.isActive();
  const bool menuBackPressed = menuActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (menuPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (menuBackPressed) swallowMenuBackRelease = true;
    return;
  }
  if (swallowMenuBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowMenuBackRelease = false;
    return;
  }

  const bool bookPopupActive = bookActionsPopup.isActive();
  const bool bookPopupConfirmPressed = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool bookPopupBackPressed = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (bookActionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (bookPopupConfirmPressed) swallowBookConfirmRelease = true;
    if (bookPopupBackPressed) swallowBookBackRelease = true;
    return;
  }
  if (swallowBookBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowBookBackRelease = false;
    return;
  }

  if (!bookActionsPopup.isActive() && !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    swallowBookConfirmRelease = false;
  }

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;

  // The first front-button hint used to say Resume and open the most recent
  // book directly. Match Folio Nooir's home behavior: the button now opens
  // the menu, while Confirm still opens the selected book normally.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen && !recentBooks.empty()) {
    backPressSeen = false;
    showMenu();
    return;
  }

  if (selectorIndex < recentBooks.size() && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= 1000 && !longPressActionShown) {
    longPressActionShown = true;
    swallowBookConfirmRelease = true;
    showBookActions();
    return;
  }

  int tx = 0;
  int ty = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTouchDown(tx, ty) && tx >= 0 && tx < renderer.getScreenWidth() &&
      ty >= metrics.homeTopPadding && ty < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    if (selectorIndex != 0) {
      selectorIndex = 0;
      requestUpdate();
    }
    return;
  }

  if (!recentBooks.empty() &&
      mappedInput.wasTapInRect(0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight)) {
    selectorIndex = 0;
    activateSelection();
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing,
                                              renderedMenuCount, 0, INT32_MAX, metrics.menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    if (swallowBookConfirmRelease) {
      swallowBookConfirmRelease = false;
      return;
    }
    activateSelection();
    return;
  }

  if (firstRenderDone && !recentsLoaded && !recentsLoading) loadRecentCovers(metrics.homeCoverHeight);
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_MENU), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (menuPopup.processRender(renderer, mappedInput) || bookActionsPopup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
}

void HomeActivity::onSelectBook(const std::string& path) {
  logCbzPath("home-book-selection", path);
  activityManager.goToReader(path);
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
