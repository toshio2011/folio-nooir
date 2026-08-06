#include "FolioLibraryActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "BookStateStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "fontIds.h"
#include "activities/home/SynopsisActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "util/BookCacheUtils.h"

void FolioLibraryActivity::loadFiles() {
  files.clear();
  files.reserve(64);
  HalFile root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory() || !fileNameBuffer) return;
  root.rewindDirectory();
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }
    if (entry.isDirectory()) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      const std::string_view name(fileNameBuffer.get());
      if (FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
          FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name) ||
          FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name)) {
        files.emplace_back(name);
      }
    }
  }
  FsHelpers::sortFileList(files);
  selectorIndex = 0;
  resetPreviews();
}

void FolioLibraryActivity::resetPreviews() {
  for (auto& preview : previews) preview = Preview{};
  previewPageStart = (selectorIndex / PAGE_SIZE) * PAGE_SIZE;
  nextPreviewSlot = 0;
  nextCoverSlot = 0;
  lastCoverGenerationMs = millis();
}

std::string FolioLibraryActivity::fullPath(const size_t index) const {
  if (index >= files.size()) return {};
  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += '/';
  path += files[index];
  if (!path.empty() && path.back() == '/') path.pop_back();
  return path;
}

void FolioLibraryActivity::loadNextPreview() {
  while (nextPreviewSlot < PAGE_SIZE) {
    const size_t slot = nextPreviewSlot++;
    const size_t index = previewPageStart + slot;
    if (index >= files.size()) return;
    Preview& preview = previews[slot];
    preview.fileIndex = index;
    preview.directory = !files[index].empty() && files[index].back() == '/';
    preview.title = files[index];
    if (preview.directory && !preview.title.empty()) preview.title.pop_back();
    if (!preview.directory) {
      const std::string path = fullPath(index);
      const auto& recent = RECENT_BOOKS.getBooks();
      const auto recentIt = std::find_if(recent.begin(), recent.end(), [&](const RecentBook& book) {
        return book.path == path;
      });
      if (recentIt != recent.end()) {
        preview.progressPercent = recentIt->progressPercent;
        if (!recentIt->title.empty()) preview.title = recentIt->title;
        preview.author = recentIt->author;
        preview.synopsis = recentIt->synopsis;
        preview.coverBmpPath = recentIt->coverBmpPath;
        const bool cachedThumbAvailable =
            !preview.coverBmpPath.empty() &&
            Storage.exists(UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT).c_str());
        preview.metadataAttempted = !preview.title.empty() && cachedThumbAvailable;
      }
      // Images are viewable files, not books. Do not show the metadata
      // retrieval popup when the highlight lands on a PNG/JPEG/BMP.
      if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
        preview.metadataAttempted = true;
      }
      if (FsHelpers::hasEpubExtension(path) && !preview.metadataAttempted) {
        // Browsing must never build an EPUB cache or generate a thumbnail.
        // Cache misses keep the filename and placeholder until the book is opened.
        Epub epub(path, "/.crosspoint");
        if (epub.load(false, true)) {
          preview.title = epub.getTitle().empty() ? files[index] : epub.getTitle();
          preview.author = epub.getAuthor();
          preview.synopsis = epub.getDescription();
          preview.coverBmpPath = epub.getThumbBmpPath();
          preview.metadataAttempted = true;
        }
      }
    }
    preview.loaded = true;
    // Prepare one item per loop so input remains responsive, but refresh the
    // e-ink panel only once after the visible page is complete.
    if (nextPreviewSlot >= PAGE_SIZE || previewPageStart + nextPreviewSlot >= files.size()) requestUpdate();
    return;  // Exactly one visible item per loop.
  }
}

void FolioLibraryActivity::showBookActions() {
  if (selectorIndex >= files.size() || (!files[selectorIndex].empty() && files[selectorIndex].back() == '/')) return;
  static constexpr StrId actions[] = {StrId::STR_OPEN,          StrId::STR_MARK_READING,
                                      StrId::STR_MARK_ON_HOLD,  StrId::STR_FINISHED,
                                      StrId::STR_RESET_PROGRESS, StrId::STR_REFRESH_BOOK_CACHE,
                                      StrId::STR_READ_FULL_SYNOPSIS};
  bookActionsPopup.show(StrId::STR_BOOK_ACTIONS, actions, 7, 0, [this](const int action) {
    const std::string path = fullPath(selectorIndex);
    const size_t slot = selectorIndex - previewPageStart;
    if (slot >= PAGE_SIZE) return;
    Preview& preview = previews[slot];
    if (action == 0) {
      onSelectBook(path);
      return;
    }
    RECENT_BOOKS.addBook(path, preview.title, preview.author, preview.coverBmpPath, preview.synopsis);
    if (action == 1) {
      BOOK_STATES.setStatus(path, BookStatus::Reading);
      if (preview.progressPercent >= 100) {
        preview.progressPercent = 99;
        RECENT_BOOKS.recordReading(path, 99, 0);
      }
    } else if (action == 2) {
      BOOK_STATES.setStatus(path, BookStatus::OnHold);
      if (preview.progressPercent >= 100) {
        preview.progressPercent = 99;
        RECENT_BOOKS.recordReading(path, 99, 0);
      }
    } else if (action == 3) {
      BOOK_STATES.setStatus(path, BookStatus::Finished);
      RECENT_BOOKS.recordReading(path, 100, 0);
      preview.progressPercent = 100;
    } else if (action == 4) {
      BOOK_STATES.reset(path);
      RECENT_BOOKS.recordReading(path, 0, 0);
      preview.progressPercent = 0;
    } else if (action == 5) {
      clearBookCache(path);
      preview = Preview{};
      nextPreviewSlot = slot;
      nextCoverSlot = slot;
      observedSelectorIndex = SIZE_MAX;
      retrievingMetadata = false;
      retrievingPopupRendered = false;
    } else if (action == 6) {
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, preview.title, preview.author, preview.synopsis),
          nullptr);
      return;
    }
    requestUpdate(true);
  });
  requestUpdate();
}

void FolioLibraryActivity::generateNextMissingCover() {
  constexpr unsigned long COVER_GENERATION_INTERVAL_MS = 1500;
  const bool previewsReady =
      nextPreviewSlot >= PAGE_SIZE || previewPageStart + nextPreviewSlot >= files.size();
  if (!previewsReady || millis() - lastCoverGenerationMs < COVER_GENERATION_INTERVAL_MS) return;

  while (nextCoverSlot < PAGE_SIZE) {
    Preview& preview = previews[nextCoverSlot++];
    if (!preview.loaded || preview.directory || preview.coverBmpPath.empty()) continue;
    const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
    if (Storage.exists(thumb.c_str())) continue;

    const std::string path = fullPath(preview.fileIndex);
    bool generated = false;
    if (FsHelpers::hasEpubExtension(path)) {
      Epub epub(path, "/.crosspoint");
      if (epub.load(false, true)) generated = epub.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
    } else if (FsHelpers::hasXtcExtension(path)) {
      Xtc xtc(path, "/.crosspoint");
      if (xtc.load()) generated = xtc.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
    }
    lastCoverGenerationMs = millis();
    if (generated) requestUpdate();
    return;  // Never extract more than one cover during an idle interval.
  }
}

void FolioLibraryActivity::loadSelectedMetadata() {
  constexpr unsigned long SELECTION_DEBOUNCE_MS = 1500;
  if (selectorIndex >= files.size() || selectorIndex < previewPageStart ||
      selectorIndex >= previewPageStart + PAGE_SIZE || millis() - selectionChangedMs < SELECTION_DEBOUNCE_MS)
    return;

  Preview& preview = previews[selectorIndex - previewPageStart];
  if (!preview.loaded || preview.directory || preview.metadataAttempted) return;

  const std::string path = fullPath(selectorIndex);
  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
    preview.metadataAttempted = true;
    return;
  }
  // Only EPUB/XTC books have metadata extractors here. Leave other file types
  // (including future PDF/image entries) as filename-only rows without a
  // misleading retrieval popup.
  if (!FsHelpers::hasEpubExtension(path) && !FsHelpers::hasXtcExtension(path)) {
    preview.metadataAttempted = true;
    return;
  }

  if (!retrievingMetadata) {
    retrievingMetadata = true;
    retrievingMetadataIndex = selectorIndex;
    retrievingPopupRendered = false;
    requestUpdate();
    return;
  }
  if (retrievingMetadataIndex != selectorIndex || !retrievingPopupRendered) return;

  retrievingMetadata = false;
  retrievingPopupRendered = false;
  preview.metadataAttempted = true;
  bool changed = false;
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (epub.load(true, true)) {
      preview.title = epub.getTitle().empty() ? files[selectorIndex] : epub.getTitle();
      preview.author = epub.getAuthor();
      preview.synopsis = epub.getDescription();
      preview.coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
      if (!Storage.exists(thumb.c_str())) epub.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
      changed = true;
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      preview.title = xtc.getTitle();
      preview.author = xtc.getAuthor();
      preview.coverBmpPath = xtc.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
      if (!Storage.exists(thumb.c_str())) xtc.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
      changed = true;
    }
  }
  if (changed) requestUpdate();
}

void FolioLibraryActivity::activateSelected() {
  if (selectorIndex >= files.size()) return;
  if (!files[selectorIndex].empty() && files[selectorIndex].back() == '/') {
    basepath = fullPath(selectorIndex);
    loadFiles();
    requestUpdate();
    return;
  }
  const std::string path = fullPath(selectorIndex);
  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
    activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
  } else {
    onSelectBook(path);
  }
}

void FolioLibraryActivity::onEnter() {
  Activity::onEnter();
  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FLIB", "OOM: %u-byte filename buffer", static_cast<unsigned>(NAME_BUFFER_SIZE));
    return;
  }
  HalFile start = Storage.open(basepath.c_str());
  if (start && !start.isDirectory()) {
    const std::string original = basepath;
    basepath = FsHelpers::extractFolderPath(original);
    if (basepath.empty()) basepath = "/";
    loadFiles();
    const size_t slash = original.find_last_of('/');
    const std::string filename = original.substr(slash == std::string::npos ? 0 : slash + 1);
    const auto it = std::find(files.begin(), files.end(), filename);
    if (it != files.end()) selectorIndex = static_cast<size_t>(std::distance(files.begin(), it));
    resetPreviews();
  } else {
    if (!start) basepath = "/";
    loadFiles();
  }
  requestUpdate();
}

void FolioLibraryActivity::onExit() {
  previews = {};
  files.clear();
  fileNameBuffer.reset();
  Activity::onExit();
}

void FolioLibraryActivity::loop() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = theme.shelfLayout(renderer, metrics);
  const int listTop = layout.contentTop + layout.detailHeight;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const size_t pageStart = (selectorIndex / PAGE_SIZE) * PAGE_SIZE;
  if (pageStart != previewPageStart) resetPreviews();
  if (selectorIndex != observedSelectorIndex) {
    observedSelectorIndex = selectorIndex;
    selectionChangedMs = millis();
    retrievingMetadata = false;
    retrievingMetadataIndex = SIZE_MAX;
    retrievingPopupRendered = false;
  }

  const bool popupActive = bookActionsPopup.isActive();
  const bool popupConfirmPressed = popupActive && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool popupBackPressed = popupActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (bookActionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (popupConfirmPressed) swallowConfirmRelease = true;
    if (popupBackPressed) swallowBackRelease = true;
    return;
  }
  if (swallowBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowBackRelease = false;
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= 1000 &&
      !longPressActionShown) {
    longPressActionShown = true;
    swallowConfirmRelease = true;
    showBookActions();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      return;
    }
    activateSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (basepath == "/") {
      activityManager.goHome();
    } else {
      basepath = FsHelpers::extractFolderPath(basepath);
      if (basepath.empty()) basepath = "/";
      loadFiles();
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    activityManager.goHome();
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    if (touchY < layout.contentTop) {
      const int tab = std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth()));
      if (tab == 1) activityManager.goHome();
      if (tab == 2) activityManager.goToFolioShelf(2);
      return;
    }
  }
  int touchSelection = static_cast<int>(selectorIndex);
  const auto listTouch = handleListTouch(touchSelection, static_cast<int>(files.size()), listTop, listHeight, false);
  if (listTouch != ListTouchResult::None) {
    selectorIndex = static_cast<size_t>(touchSelection);
    if ((selectorIndex / PAGE_SIZE) * PAGE_SIZE != previewPageStart) resetPreviews();
    if (listTouch == ListTouchResult::Activated) activateSelected();
    else requestUpdate();
    return;
  }
  const int count = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), count);
    if ((selectorIndex / PAGE_SIZE) * PAGE_SIZE != previewPageStart) resetPreviews();
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), count);
    if ((selectorIndex / PAGE_SIZE) * PAGE_SIZE != previewPageStart) resetPreviews();
    requestUpdate();
  });
  loadNextPreview();
  loadSelectedMetadata();
  generateNextMissingCover();
}

void FolioLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = theme.shelfLayout(renderer, metrics);
  theme.drawShelfTabs(renderer, layout, 0);

  const int detailTop = layout.contentTop;
  const int detailHeight = layout.detailHeight;
  const int coverX = 12;
  const int coverY = detailTop + 9;
  const int coverWidth = 112;
  const int coverHeight = detailHeight - 18;
  const size_t slot = selectorIndex >= previewPageStart ? selectorIndex - previewPageStart : PAGE_SIZE;
  const Preview* selected = slot < PAGE_SIZE ? &previews[slot] : nullptr;
  bool drewCover = false;
  if (selected && selected->loaded && !selected->directory && !selected->coverBmpPath.empty()) {
    const std::string thumb = UITheme::getCoverThumbPath(selected->coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
    HalFile file;
    if (Storage.openFileForRead("FLIB", thumb, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        // Use the whole cover slot. A slightly stretched portrait is easier to
        // read than a tall image surrounded by an empty border.
        renderer.drawBitmap(bitmap, coverX, coverY, coverWidth, coverHeight);
        renderer.drawRect(coverX, coverY, coverWidth, coverHeight);
        drewCover = true;
      }
    }
  }
  if (!drewCover) {
    renderer.drawRect(coverX + 8, coverY, coverWidth - 16, coverHeight);
    if (selected && selected->directory) renderer.drawCenteredText(UI_12_FONT_ID, coverY + coverHeight / 2, "/", true);
  }
  const int textX = coverX + coverWidth + 14;
  const int textWidth = renderer.getScreenWidth() - textX - 12;
  const char* titleText = selected && selected->loaded ? selected->title.c_str()
                                                        : (files.empty() ? "" : files[selectorIndex].c_str());
  renderer.drawText(UI_12_FONT_ID, textX, detailTop + 18,
                    renderer.truncatedText(UI_12_FONT_ID, titleText, textWidth).c_str(), true);
  if (selected && !selected->author.empty())
    renderer.drawText(UI_10_FONT_ID, textX, detailTop + 48,
                      renderer.truncatedText(UI_10_FONT_ID, selected->author.c_str(), textWidth).c_str());
  const char* synopsis = selected && !selected->synopsis.empty() ? selected->synopsis.c_str() : tr(STR_NO_SYNOPSIS);
  const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsis, textWidth, 3);
  int synopsisY = detailTop + 72;
  for (const auto& line : synopsisLines) {
    renderer.drawText(SMALL_FONT_ID, textX, synopsisY, line.c_str());
    synopsisY += renderer.getLineHeight(SMALL_FONT_ID);
  }
  if (selected && !selected->directory) {
    char progress[20];
    snprintf(progress, sizeof(progress), "%u%%", selected->progressPercent);
    renderer.drawText(UI_10_FONT_ID, textX, detailTop + detailHeight - 43, progress, true);
    theme.drawCoverProgress(renderer, textX, detailTop + detailHeight - 21, textWidth, selected->progressPercent);
  }
  renderer.drawLine(0, detailTop + detailHeight - 1, renderer.getScreenWidth() - 1, detailTop + detailHeight - 1);

  const int listTop = detailTop + detailHeight;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, listTop, renderer.getScreenWidth(), listHeight}, files.size(), selectorIndex,
               [this](int index) {
                 std::string name = files[index];
                 if (!name.empty() && name.back() == '/') name.pop_back();
                 return name;
               },
               nullptr, [this](int index) { return UITheme::getFileIcon(files[index]); }, nullptr, false);

  if (bookActionsPopup.processRender(renderer, mappedInput)) return;
  if (retrievingMetadata && retrievingMetadataIndex == selectorIndex) {
    GUI.drawPopup(renderer, tr(STR_RETRIEVING_BOOK_DETAILS));
    retrievingPopupRendered = true;
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
