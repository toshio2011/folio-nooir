#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Xtc.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "BookStateStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/BookCacheUtils.h"
#include "components/UITheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "fontIds.h"

namespace {
constexpr char FOLIO_HOME_SNAPSHOT[] = "/.crosspoint/folio_home.bin";
constexpr uint32_t FOLIO_HOME_MAGIC = 0x464E484D;  // "FNHM"
constexpr uint16_t FOLIO_HOME_VERSION = 1;

void hashBytes(uint64_t& hash, const void* data, const size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() { recentBooks = RECENT_BOOKS.getBooks(); }

void RecentBooksActivity::rebuildVisibleBooks() {
  visibleBookCount = 0;
  for (size_t i = 0; i < recentBooks.size() && visibleBookCount < sizeof(visibleBookIndexes); ++i) {
    const BookState* state = BOOK_STATES.find(recentBooks[i].path);
    const bool finished = state ? state->status == BookStatus::Finished : recentBooks[i].progressPercent >= 100;
    const bool show = activeTab == 0 || (activeTab == 1 && !finished) || (activeTab == 2 && finished);
    if (show) visibleBookIndexes[visibleBookCount++] = static_cast<uint8_t>(i);
  }
  if (visibleBookCount == 0) {
    selectorIndex = 0;
  } else if (selectorIndex >= visibleBookCount) {
    selectorIndex = visibleBookCount - 1;
  }
}

size_t RecentBooksActivity::selectedRecentIndex() const {
  return visibleBookCount == 0 ? 0 : visibleBookIndexes[selectorIndex];
}

uint64_t RecentBooksActivity::snapshotKey() const {
  uint64_t hash = 1469598103934665603ULL;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  hashBytes(hash, &width, sizeof(width));
  hashBytes(hash, &height, sizeof(height));
  for (const auto& book : recentBooks) hashBytes(hash, book.path.data(), book.path.size());
  return hash;
}

bool RecentBooksActivity::restoreSnapshot() {
  if (SETTINGS.uiTheme != CrossPointSettings::UI_THEME::FOLIO_NOOIR || !renderer.hasFrameBuffer()) return false;
  HalFile file;
  if (!Storage.openFileForRead("SHELF", FOLIO_HOME_SNAPSHOT, file)) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t dataSize = 0;
  uint64_t key = 0;
  if (file.read(&magic, sizeof(magic)) != sizeof(magic) || file.read(&version, sizeof(version)) != sizeof(version) ||
      file.read(&width, sizeof(width)) != sizeof(width) || file.read(&height, sizeof(height)) != sizeof(height) ||
      file.read(&dataSize, sizeof(dataSize)) != sizeof(dataSize) || file.read(&key, sizeof(key)) != sizeof(key)) {
    return false;
  }
  if (magic != FOLIO_HOME_MAGIC || version != FOLIO_HOME_VERSION || width != renderer.getScreenWidth() ||
      height != renderer.getScreenHeight() || dataSize != renderer.getBufferSize() || key != snapshotKey()) {
    return false;
  }
  const bool loaded = file.read(renderer.getFrameBuffer(), dataSize) == static_cast<int>(dataSize);
  if (loaded) LOG_DBG("SHELF", "Restored %u-byte Home snapshot", dataSize);
  return loaded;
}

void RecentBooksActivity::writeSnapshot() {
  if (!renderer.hasFrameBuffer()) return;
  Storage.ensureDirectoryExists("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("SHELF", FOLIO_HOME_SNAPSHOT, file)) return;
  const uint32_t magic = FOLIO_HOME_MAGIC;
  const uint16_t version = FOLIO_HOME_VERSION;
  const uint16_t width = static_cast<uint16_t>(renderer.getScreenWidth());
  const uint16_t height = static_cast<uint16_t>(renderer.getScreenHeight());
  const uint32_t dataSize = renderer.getBufferSize();
  const uint64_t key = snapshotKey();
  const bool ok = file.write(&magic, sizeof(magic)) == sizeof(magic) &&
                  file.write(&version, sizeof(version)) == sizeof(version) &&
                  file.write(&width, sizeof(width)) == sizeof(width) &&
                  file.write(&height, sizeof(height)) == sizeof(height) &&
                  file.write(&dataSize, sizeof(dataSize)) == sizeof(dataSize) &&
                  file.write(&key, sizeof(key)) == sizeof(key) &&
                  file.write(renderer.getFrameBuffer(), dataSize) == dataSize;
  if (!ok) LOG_ERR("SHELF", "Failed to write Home snapshot");
}

void RecentBooksActivity::generateNextCover() {
  if (coverGenerationActive) return;
  coverGenerationActive = true;
  while (nextCoverToGenerate < recentBooks.size()) {
    RecentBook& book = recentBooks[nextCoverToGenerate++];
    if (book.coverBmpPath.empty()) continue;
    const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
    if (Storage.exists(thumbPath.c_str())) continue;

    bool attempted = false;
    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      if (epub.load(false, true)) {
        attempted = true;
        epub.generateThumbBmp(BOOKSHELF_COVER_HEIGHT);
      }
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        attempted = true;
        xtc.generateThumbBmp(BOOKSHELF_COVER_HEIGHT);
      }
    }
    if (attempted) {
      snapshotRestored = false;
      if (Storage.exists(FOLIO_HOME_SNAPSHOT)) Storage.remove(FOLIO_HOME_SNAPSHOT);
      initialRenderPending = true;
      coverGenerationActive = false;
      requestUpdate();
      return;  // At most one expensive extraction per activity cycle.
    }
  }
  coverGenerationActive = false;
}

void RecentBooksActivity::showMenu() {
  static constexpr StrId options[] = {StrId::STR_BROWSE_FILES, StrId::STR_FILE_TRANSFER, StrId::STR_SETTINGS_TITLE};
  menuPopup.show(StrId::STR_MENU, options, 3, 0, [](int index) {
    if (index == 0) activityManager.goToFileBrowser();
    if (index == 1) activityManager.goToFileTransfer();
    if (index == 2) activityManager.goToSettings();
  });
  requestUpdate();
}

void RecentBooksActivity::showBookActions() {
  if (visibleBookCount == 0 || selectorIndex >= visibleBookCount) return;
  static constexpr StrId actions[] = {StrId::STR_OPEN,           StrId::STR_MARK_READING,
                                      StrId::STR_MARK_ON_HOLD,   StrId::STR_FINISHED,
                                      StrId::STR_RESET_PROGRESS, StrId::STR_REFRESH_BOOK_CACHE,
                                      StrId::STR_REMOVE_FROM_LIST};
  bookActionsPopup.show(StrId::STR_BOOK_ACTIONS, actions, 7, 0, [this](const int action) {
    if (visibleBookCount == 0 || selectorIndex >= visibleBookCount) return;
    const RecentBook selected = recentBooks[selectedRecentIndex()];
    if (action == 0) {
      onSelectBook(selected.path);
      return;
    }
    if (action == 1 || action == 2) {
      BOOK_STATES.setStatus(selected.path, action == 1 ? BookStatus::Reading : BookStatus::OnHold);
      if (selected.progressPercent >= 100) RECENT_BOOKS.recordReading(selected.path, 99, 0);
    }
    if (action == 3) {
      BOOK_STATES.setStatus(selected.path, BookStatus::Finished);
      RECENT_BOOKS.recordReading(selected.path, 100, 0);
    }
    if (action == 4) {
      BOOK_STATES.reset(selected.path);
      RECENT_BOOKS.recordReading(selected.path, 0, 0);
    }
    if (action == 5) clearBookCache(selected.path);
    if (action == 6) {
      RECENT_BOOKS.removeByPath(selected.path);
      BOOK_STATES.removeByPath(selected.path);
    }
    loadRecentBooks();
    rebuildVisibleBooks();
    snapshotRestored = false;
    if (Storage.exists(FOLIO_HOME_SNAPSHOT)) Storage.remove(FOLIO_HOME_SNAPSHOT);
    requestUpdate(true);
  });
  requestUpdate();
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadRecentBooks();
  rebuildVisibleBooks();

  selectorIndex = 0;
  nextCoverToGenerate = 0;
  snapshotRestored = restoreSnapshot();
  initialRenderPending = true;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
}

void RecentBooksActivity::loop() {
  constexpr int pageItems = BOOKS_PER_PAGE;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = folioTheme.shelfLayout(renderer, metrics);

  if (snapshotWritePending) {
    snapshotWritePending = false;
    writeSnapshot();
  }

  const bool bookPopupActive = bookActionsPopup.isActive();
  const bool bookPopupConfirm = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool bookPopupBack = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (bookActionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (bookPopupConfirm) swallowBookConfirmRelease = true;
    if (bookPopupBack) swallowBookBackRelease = true;
    return;
  }
  if (swallowBookBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowBookBackRelease = false;
    return;
  }

  if (visibleBookCount > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= 1000 && !longPressActionShown) {
    longPressActionShown = true;
    swallowBookConfirmRelease = true;
    showBookActions();
    return;
  }

  const bool menuWasActive = menuPopup.isActive();
  const bool menuBackPressed =
      menuWasActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (menuPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (menuBackPressed) swallowMenuBackRelease = true;
    return;
  }

  // OptionPopup dismisses on press while this activity opens the menu on
  // release. Consume the matching release so one press cannot close and then
  // immediately reopen the menu.
  if (swallowMenuBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowMenuBackRelease = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    if (swallowBookConfirmRelease) {
      swallowBookConfirmRelease = false;
      return;
    }
    if (visibleBookCount > 0 && selectorIndex < visibleBookCount) {
      const RecentBook& selected = recentBooks[selectedRecentIndex()];
      LOG_DBG("RBA", "Selected recent book: %s", selected.path.c_str());
      onSelectBook(selected.path);
      return;
    }
  }

  const bool previousTab = mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextTab = mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (previousTab || nextTab) {
    if (previousTab && activeTab == 1) {
      activityManager.goToFileBrowser();
      return;
    }
    activeTab = activeTab == 2 ? 1 : 2;
    selectorIndex = 0;
    rebuildVisibleBooks();
    requestUpdate();
    return;
  }

  const int columns = layout.columns;
  const int gap = layout.gridGap;
  const int cardWidth = layout.cardWidth;
  const int gridTop = layout.gridTop;
  const int cardHeight = layout.cardHeight;
  const size_t pageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
      const uint8_t touchedTab =
          static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
      if (touchedTab == 0) {
        activityManager.goToFileBrowser();
        return;
      }
      activeTab = touchedTab;
      selectorIndex = 0;
      rebuildVisibleBooks();
      requestUpdate();
      return;
    }
    for (int slot = 0; slot < BOOKS_PER_PAGE; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= visibleBookCount) break;
      const int cardX = gap + (slot % columns) * (cardWidth + gap);
      const int cardY = gridTop + (slot / columns) * cardHeight;
      if (touchX >= cardX && touchX < cardX + cardWidth && touchY >= cardY && touchY < cardY + cardHeight) {
        if (selectorIndex != index) {
          selectorIndex = index;
          requestUpdate();
        }
        return;
      }
    }
  }
  for (int slot = 0; slot < BOOKS_PER_PAGE; ++slot) {
    const size_t index = pageStart + static_cast<size_t>(slot);
    if (index >= visibleBookCount) break;
    const int cardX = gap + (slot % columns) * (cardWidth + gap);
    const int cardY = gridTop + (slot / columns) * cardHeight;
    if (mappedInput.wasTapInRect(cardX, cardY, cardWidth, cardHeight)) {
      selectorIndex = index;
      onSelectBook(recentBooks[visibleBookIndexes[index]].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR)
      showMenu();
    else
      onGoHome();
  }

  int listSize = visibleBookCount;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  // Cover extraction is paced by FolioLibraryActivity. Running it here makes
  // the freshly restored Home screen unresponsive immediately after reading.
}

void RecentBooksActivity::promptRemoveBook(const std::string& path, const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("RBA", "Remove from recents cancelled");
      return;
    }
    if (RECENT_BOOKS.removeByPath(path)) {
      LOG_DBG("RBA", "Removed from recents: %s", path.c_str());
      loadRecentBooks();
      rebuildVisibleBooks();
      if (recentBooks.empty()) {
        selectorIndex = 0;
      } else if (selectorIndex >= visibleBookCount) {
        selectorIndex = visibleBookCount > 0 ? visibleBookCount - 1 : 0;
      }
      requestUpdate(true);
    }
  };

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

#if 0
void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOKSHELF));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Four-card bookshelf. Only cached BMP thumbnails are decoded; missing
  // thumbnails are generated one at a time from loop(), never inside render().
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    constexpr int columns = 2;
    constexpr int gap = 10;
    const int cardWidth = (pageWidth - gap * 3) / columns;
    const int cardHeight = contentHeight / 2;
    const size_t pageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    for (int slot = 0; slot < BOOKS_PER_PAGE; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= recentBooks.size()) break;
      const RecentBook& book = recentBooks[index];
      const int col = slot % columns;
      const int row = slot / columns;
      const int cardX = gap + col * (cardWidth + gap);
      const int cardY = contentTop + row * cardHeight;
      if (index == selectorIndex) renderer.drawRect(cardX, cardY, cardWidth, cardHeight - gap);

      const int coverHeight = std::min(BOOKSHELF_COVER_HEIGHT, cardHeight - 67);
      int coverWidth = coverHeight * 2 / 3;
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
      HalFile file;
      bool drewCover = false;
      if (!book.coverBmpPath.empty() && Storage.openFileForRead("SHELF", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          coverWidth = coverHeight * bitmap.getWidth() / bitmap.getHeight();
          coverWidth = std::min(coverWidth, cardWidth - 24);
          renderer.drawBitmap(bitmap, cardX + (cardWidth - coverWidth) / 2, cardY + 5, coverWidth, coverHeight);
          drewCover = true;
        }
      }
      if (!drewCover) {
        renderer.drawRect(cardX + (cardWidth - coverWidth) / 2, cardY + 5, coverWidth, coverHeight);
        const std::string placeholderTitle =
            renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), coverWidth - 12);
        const int placeholderX = cardX + (cardWidth - renderer.getTextWidth(UI_10_FONT_ID, placeholderTitle.c_str())) / 2;
        renderer.drawText(UI_10_FONT_ID, placeholderX, cardY + coverHeight / 2, placeholderTitle.c_str());
      }

      const int textY = cardY + coverHeight + 9;
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), cardWidth - 12);
      renderer.drawText(UI_10_FONT_ID, cardX + 6, textY, title.c_str(), true);
      char progress[24];
      snprintf(progress, sizeof(progress), "%u%% · %s", book.progressPercent,
               book.progressPercent >= 100 ? tr(STR_COMPLETE) : (book.progressPercent > 0 ? tr(STR_ONGOING) : tr(STR_NEW)));
      renderer.drawText(UI_10_FONT_ID, cardX + 6, textY + renderer.getLineHeight(UI_10_FONT_ID), progress);
      const int barY = cardY + cardHeight - gap - 7;
      renderer.drawRect(cardX + 6, barY, cardWidth - 12, 4);
      const int fillWidth = (cardWidth - 14) * book.progressPercent / 100;
      if (fillWidth > 0) renderer.fillRect(cardX + 7, barY + 1, fillWidth, 2);
    }
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
#endif

void RecentBooksActivity::render(RenderLock&&) {
  const bool renderedFromSnapshot = snapshotRestored;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (!snapshotRestored) renderer.clearScreen();

  // The cover bookshelf is exclusive to the Folio Nooir UI theme. Other
  // themes retain CrossPoint's compact Recent Books list.
  if (SETTINGS.uiTheme != CrossPointSettings::UI_THEME::FOLIO_NOOIR) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   tr(STR_MENU_RECENT_BOOKS));
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
    if (recentBooks.empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, top + 20, tr(STR_NO_RECENT_BOOKS));
    } else {
      GUI.drawList(renderer, Rect{0, top, pageWidth, height}, recentBooks.size(), selectorIndex,
                   [this](int index) { return recentBooks[index].title; },
                   [this](int index) { return recentBooks[index].author; },
                   [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); });
    }
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = folioTheme.shelfLayout(renderer, metrics);
  folioTheme.drawShelfTabs(renderer, layout, activeTab);

  const int contentTop = layout.contentTop;
  const int contentHeight = layout.contentHeight;
  const int detailHeight = layout.detailHeight;
  auto drawStats = [&] {
    const bool accumulated = !halClock.isAvailable();
    const uint32_t today = halClock.getDateKey();
    uint32_t middleSeconds = 0;
    uint16_t finishedCount = 0;
    for (const auto& book : recentBooks) {
      const uint32_t seconds = accumulated ? book.readingSeconds
                                           : (today != 0 && book.dailyReadingDateKey == today
                                                  ? book.dailyReadingSeconds
                                                  : 0);
      middleSeconds += std::min(seconds, UINT32_MAX - middleSeconds);
      if (book.progressPercent >= 100) ++finishedCount;
    }
    const uint32_t lastMinutes = recentBooks.empty() ? 0 : (recentBooks.front().lastSessionSeconds + 30) / 60;
    folioTheme.drawShelfStats(renderer, layout, lastMinutes, (middleSeconds + 30) / 60, finishedCount, accumulated);
  };
  if (visibleBookCount == 0) {
    renderer.fillRect(0, contentTop, pageWidth, contentHeight, false);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    auto drawCover = [this](const RecentBook& book, int x, int y, int maxWidth, int maxHeight, bool drawBitmap) {
      if (!drawBitmap) return;
      int width = std::min(maxWidth, maxHeight * 2 / 3);
      const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
      HalFile file;
      if (drawBitmap && !book.coverBmpPath.empty() && Storage.openFileForRead("SHELF", path, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          width = std::min(maxWidth, maxHeight * bitmap.getWidth() / bitmap.getHeight());
          renderer.drawBitmap(bitmap, x + (maxWidth - width) / 2, y, width, maxHeight);
          renderer.drawRect(x + (maxWidth - width) / 2, y, width, maxHeight);
          return;
        }
      }
      const int coverX = x + (maxWidth - width) / 2;
      renderer.drawRect(coverX, y, width, maxHeight);
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), width - 10);
      renderer.drawText(UI_10_FONT_ID, coverX + 5, y + maxHeight / 2, title.c_str());
    };

    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    constexpr int detailPadding = 12;
    constexpr int detailCoverWidth = 126;
    const int detailCoverHeight = detailHeight - 20;
    renderer.fillRect(detailPadding * 2 + detailCoverWidth, contentTop + 1,
                      pageWidth - detailPadding * 3 - detailCoverWidth, detailHeight - 2, false);
    drawCover(selected, detailPadding, contentTop + 7, detailCoverWidth, detailCoverHeight, !snapshotRestored);
    const int detailX = detailPadding * 2 + detailCoverWidth;
    const int detailWidth = pageWidth - detailX - detailPadding;
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, selected.title.c_str(), detailWidth);
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, selected.author.c_str(), detailWidth);
    renderer.drawText(UI_12_FONT_ID, detailX, contentTop + 20, title.c_str(), true);
    renderer.drawText(UI_10_FONT_ID, detailX, contentTop + 55, author.c_str());
    const char* synopsisText = selected.synopsis.empty() ? tr(STR_NO_SYNOPSIS) : selected.synopsis.c_str();
    const int synopsisMaxLines = std::max(3, (detailHeight - 130) / renderer.getLineHeight(SMALL_FONT_ID));
    const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsisText, detailWidth, synopsisMaxLines);
    int synopsisY = contentTop + 79;
    for (const auto& line : synopsisLines) {
      renderer.drawText(SMALL_FONT_ID, detailX, synopsisY, line.c_str());
      synopsisY += renderer.getLineHeight(SMALL_FONT_ID);
    }
    char state[32];
    snprintf(state, sizeof(state), "%s - %u%%",
             selected.progressPercent >= 100 ? tr(STR_COMPLETE)
                                             : (selected.progressPercent > 0 ? tr(STR_ONGOING) : tr(STR_NEW)),
             selected.progressPercent);
    renderer.drawText(UI_10_FONT_ID, detailX, contentTop + detailHeight - 54, state);
    const int progressY = contentTop + detailHeight - 28;
    renderer.drawRect(detailX, progressY, detailWidth, 12);
    const int fill = (detailWidth - 2) * selected.progressPercent / 100;
    if (fill > 0) renderer.fillRect(detailX + 1, progressY + 1, fill, 10);
    renderer.drawLine(0, contentTop + detailHeight - 1, pageWidth - 1, contentTop + detailHeight - 1);

    const int columns = layout.columns;
    const int gap = layout.gridGap;
    const int gridTop = layout.gridTop;
    const int cardWidth = layout.cardWidth;
    const int cardHeight = layout.cardHeight;
    const size_t pageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    for (int slot = 0; slot < BOOKS_PER_PAGE; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= visibleBookCount) break;
      const RecentBook& book = recentBooks[visibleBookIndexes[index]];
      const int x = gap + (slot % columns) * (cardWidth + gap);
      const int y = gridTop + (slot / columns) * cardHeight;
      const int coverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
      renderer.fillRect(x, y + coverHeight, cardWidth, std::max(1, cardHeight - coverHeight), false);
      if (index == selectorIndex) renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6);
      drawCover(book, x, y, cardWidth, coverHeight, !snapshotRestored);
      folioTheme.drawCoverProgress(renderer, x, y + coverHeight + 5, cardWidth, book.progressPercent);
    }
    const size_t pageCount = (visibleBookCount + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
    folioTheme.drawPageIndicator(renderer, layout, selectorIndex / BOOKS_PER_PAGE + 1, pageCount);

  }
  drawStats();

  if (menuPopup.processRender(renderer, mappedInput)) return;
  if (bookActionsPopup.processRender(renderer, mappedInput)) return;
  const auto labels = mappedInput.mapLabels(tr(STR_MENU), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  if (initialRenderPending) {
    initialRenderPending = false;
    snapshotWritePending = true;
  }
  snapshotRestored = false;
  // A restored framebuffer makes Home appear quickly, but its cached image may
  // predate newly generated covers. Schedule one normal render automatically
  // instead of waiting for the user to move the selection.
  if (renderedFromSnapshot) requestUpdate();
}
