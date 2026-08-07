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
#include "activities/home/ReadingStatsActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr char RETRIEVE_ALL_BOOKS_MESSAGE[] = "Retrieve all books details. Please be patient.";
constexpr char RETRIEVE_ALL_BOOKS_DONE_MESSAGE[] = "All book details retrieved.";

bool isLibraryBook(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) ||
         FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

std::string joinLibraryPath(const std::string& basepath, const std::string& name) {
  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += '/';
  path += name;
  if (!path.empty() && path.back() == '/') path.pop_back();
  return path;
}

enum class LibraryBookState : uint8_t { Reading, Unread, OnHold, Finished };

LibraryBookState getLibraryBookState(const std::string& path) {
  const BookState* state = BOOK_STATES.find(path);
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  const auto recentIt = std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) {
    return book.path == path;
  });
  const uint8_t progress = state ? state->progressPercent
                                 : (recentIt == recentBooks.end() ? 0 : recentIt->progressPercent);
  if ((state && state->status == BookStatus::Finished) || progress >= 100) return LibraryBookState::Finished;
  if (state && state->status == BookStatus::OnHold) return LibraryBookState::OnHold;
  if ((state && state->status == BookStatus::Reading) || progress > 0) return LibraryBookState::Reading;
  return LibraryBookState::Unread;
}
}  // namespace

void FolioLibraryActivity::loadFiles() {
  allFiles.clear();
  allFiles.reserve(64);
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
      allFiles.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      const std::string_view name(fileNameBuffer.get());
      if (FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
          FsHelpers::hasMarkdownExtension(name) || FsHelpers::hasBmpExtension(name) ||
          FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name)) {
        allFiles.emplace_back(name);
      }
    }
  }
  FsHelpers::sortFileList(allFiles);
  applyLibraryFilter();
}

bool FolioLibraryActivity::matchesLibraryFilter(const std::string& name) const {
  if (libraryFilter == LibraryFilter::All || (!name.empty() && name.back() == '/')) return true;
  const std::string path = joinLibraryPath(basepath, name);
  if (!isLibraryBook(path)) return false;
  const LibraryBookState state = getLibraryBookState(path);
  switch (libraryFilter) {
    case LibraryFilter::Reading:
      return state == LibraryBookState::Reading;
    case LibraryFilter::Unread:
      return state == LibraryBookState::Unread;
    case LibraryFilter::OnHold:
      return state == LibraryBookState::OnHold;
    case LibraryFilter::Finished:
      return state == LibraryBookState::Finished;
    case LibraryFilter::All:
      return true;
  }
  return true;
}

void FolioLibraryActivity::applyLibraryFilter() {
  files.clear();
  files.reserve(allFiles.size());
  for (const auto& name : allFiles) {
    if (matchesLibraryFilter(name)) files.push_back(name);
  }
  selectorIndex = 0;
  observedSelectorIndex = SIZE_MAX;
  retrievingMetadata = false;
  retrievingMetadataIndex = SIZE_MAX;
  retrievingPopupRendered = false;
  resetPreviews();
}

FolioLibrarySummary FolioLibraryActivity::getLibrarySummary() const {
  FolioLibrarySummary summary;
  for (const auto& name : allFiles) {
    if (!name.empty() && name.back() == '/') continue;
    const std::string path = joinLibraryPath(basepath, name);
    if (!isLibraryBook(path)) continue;
    ++summary.total;
    switch (getLibraryBookState(path)) {
      case LibraryBookState::Reading:
        ++summary.reading;
        break;
      case LibraryBookState::Unread:
        ++summary.unread;
        break;
      case LibraryBookState::OnHold:
        ++summary.onHold;
        break;
      case LibraryBookState::Finished:
        ++summary.finished;
        break;
    }
  }
  return summary;
}

void FolioLibraryActivity::resetPreviews() {
  for (auto& preview : previews) preview = Preview{};
  previewPageStart = (selectorIndex / PAGE_SIZE) * PAGE_SIZE;
  nextPreviewSlot = 0;
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
            isValidBookThumbnail(UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT));
        preview.metadataAttempted = !preview.title.empty() && cachedThumbAvailable;
      }
      // Images are viewable files, not books. Do not show the metadata
      // retrieval popup when the highlight lands on a PNG/JPEG/BMP.
      if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
        preview.metadataAttempted = true;
      }
    }
    preview.loaded = true;
    // Prepare one item per loop so input remains responsive, but refresh the
    // e-ink panel only once after the visible page is complete.
    if (nextPreviewSlot >= PAGE_SIZE || previewPageStart + nextPreviewSlot >= files.size()) requestUpdate();
    return;  // Exactly one visible item per loop.
  }
}

void FolioLibraryActivity::startRetrieveAllBooks() {
  retrieveDirectories.clear();
  retrieveBooks.clear();
  retrieveDirectories.reserve(16);
  retrieveBooks.reserve(32);
  retrieveDirectories.emplace_back("/");
  retrieveDirectoryIndex = 0;
  retrieveBookIndex = 0;
  retrievingAllBooks = true;
  retrievingAllBooksPopupRendered = false;
  retrieveAllComplete = false;
  retrieveAllCompleteUntilMs = 0;
  requestUpdate(true);
}

void FolioLibraryActivity::processRetrieveAllBooks() {
  if (!retrievingAllBooks || !retrievingAllBooksPopupRendered) return;

  // Traverse one directory per loop.  The expensive EPUB/XTC work is kept
  // out of the directory scan and is handled one book per later loop, so the
  // popup remains visible even on slower X3 hardware.
  if (retrieveDirectoryIndex < retrieveDirectories.size()) {
    const std::string directoryPath = retrieveDirectories[retrieveDirectoryIndex++];
    HalFile directory = Storage.open(directoryPath.c_str());
    if (!directory || !directory.isDirectory() || !fileNameBuffer) return;
    directory.rewindDirectory();
    for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      const std::string name(fileNameBuffer.get());
      if (name.empty() || name == "." || name == ".." || name == "System Volume Information" ||
          (!SETTINGS.showHiddenFiles && name.front() == '.')) {
        continue;
      }
      const std::string path = joinLibraryPath(directoryPath, name);
      if (entry.isDirectory()) {
        // The cache and sleep folders are implementation data, not user
        // books.  Skipping them also prevents a needless scan of our own
        // generated thumbnails.
        if (name == ".crosspoint" || name == ".sleep") continue;
        retrieveDirectories.push_back(path);
      } else if (FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name)) {
        retrieveBooks.push_back(path);
      }
    }
    return;
  }

  if (retrieveBookIndex < retrieveBooks.size()) {
    const std::string path = retrieveBooks[retrieveBookIndex++];
    if (FsHelpers::hasEpubExtension(path)) {
      Epub epub(path, "/.crosspoint");
      if (epub.loadMetadataOnly()) {
        const std::string thumb = epub.getThumbBmpPath(FolioNooirTheme::COVER_HEIGHT);
        if (!isValidBookThumbnail(thumb)) {
          Storage.remove(thumb.c_str());
          epub.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
        }
      }
    } else if (FsHelpers::hasXtcExtension(path)) {
      Xtc xtc(path, "/.crosspoint");
      if (xtc.load()) {
        const std::string thumb = xtc.getThumbBmpPath(FolioNooirTheme::COVER_HEIGHT);
        if (!isValidBookThumbnail(thumb)) {
          Storage.remove(thumb.c_str());
          xtc.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT);
        }
      }
    }
    return;
  }

  retrievingAllBooks = false;
  retrievingAllBooksPopupRendered = false;
  retrieveAllComplete = true;
  retrieveAllCompleteUntilMs = millis() + 1800;
  retrieveDirectories.clear();
  retrieveBooks.clear();
  resetPreviews();
  requestUpdate(true);
}

void FolioLibraryActivity::showMenu() {
  const char* options[] = {tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE), "Reading Statistics",
                           "Reading Calendar", "Retrieve All Book Details"};
  menuPopup.show(tr(STR_MENU), options, 5, 0, [this](const int index) {
    if (index == 0) {
      menuPopup.dismiss();
      activityManager.goToFileTransfer();
    } else if (index == 1) {
      menuPopup.dismiss();
      activityManager.goToSettings();
    } else if (index == 2) {
      menuPopup.dismiss();
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), nullptr);
    } else if (index == 3) {
      menuPopup.dismiss();
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, "", true), nullptr);
    } else if (index == 4) {
      menuPopup.dismiss();
      startRetrieveAllBooks();
    }
  });
  requestUpdate();
}

void FolioLibraryActivity::showBookActions() {
  if (selectorIndex >= files.size() || (!files[selectorIndex].empty() && files[selectorIndex].back() == '/')) return;
  const char* actions[] = {tr(STR_OPEN),           tr(STR_MARK_READING),
                           tr(STR_MARK_ON_HOLD),   tr(STR_FINISHED),
                           tr(STR_RESET_PROGRESS), tr(STR_REFRESH_BOOK_CACHE),
                           tr(STR_READ_FULL_SYNOPSIS), "Book Statistics"};
  bookActionsPopup.show(tr(STR_BOOK_ACTIONS), actions, 8, 0, [this](const int action) {
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
      observedSelectorIndex = SIZE_MAX;
      retrievingMetadata = false;
      retrievingPopupRendered = false;
    } else if (action == 6) {
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, preview.title, preview.author, preview.synopsis),
          nullptr);
      return;
    } else if (action == 7) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, path), nullptr);
      return;
    }
    requestUpdate(true);
  });
  requestUpdate();
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

  // A complete cached header is safe to read after the debounce and does not
  // need a retrieval popup. Only a cache miss (or a missing thumbnail that
  // needs extraction) enters the visible retrieval path.
  if (!retrievingMetadata && FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (epub.loadCachedMetadataOnly()) {
      preview.title = epub.getTitle().empty() ? files[selectorIndex] : epub.getTitle();
      preview.author = epub.getAuthor();
      preview.synopsis = epub.getDescription();
      preview.coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
      if (preview.coverBmpPath.empty() || isValidBookThumbnail(thumb)) {
        preview.metadataAttempted = true;
        requestUpdate();
        return;
      }
    }
  }

  if (!retrievingMetadata) {
    retrievingMetadata = true;
    retrievingMetadataIndex = selectorIndex;
    retrievingPopupRendered = false;
    // Do not let the next loop begin EPUB/XTC extraction until the busy
    // message has actually reached the panel.  On the slower X3 this small
    // synchronization prevents the device from appearing frozen while the
    // first uncached book is being opened.
    requestUpdateAndWait();
    return;
  }
  if (retrievingMetadataIndex != selectorIndex || !retrievingPopupRendered) return;

  retrievingMetadata = false;
  retrievingPopupRendered = false;
  preview.metadataAttempted = true;
  bool changed = false;
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (epub.loadMetadataOnly()) {
      preview.title = epub.getTitle().empty() ? files[selectorIndex] : epub.getTitle();
      preview.author = epub.getAuthor();
      preview.synopsis = epub.getDescription();
      preview.coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(preview.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
      if (!isValidBookThumbnail(thumb)) {
        Storage.remove(thumb.c_str());
        if (!epub.generateThumbBmp(FolioNooirTheme::COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
          preview.coverBmpPath.clear();
        }
      }
      // Metadata can be shown immediately.  Thumbnail conversion is deferred
      // only for already-cached entries; the metadata-only path above keeps
      // this selected featured book self-contained.
      changed = true;
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      preview.title = xtc.getTitle();
      preview.author = xtc.getAuthor();
      preview.coverBmpPath = xtc.getThumbBmpPath();
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
  allFiles.clear();
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

  if (retrieveAllComplete) {
    if (millis() >= retrieveAllCompleteUntilMs) {
      retrieveAllComplete = false;
      requestUpdate(true);
    }
    return;
  }
  if (retrievingAllBooks) {
    processRetrieveAllBooks();
    return;
  }

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
      showMenu();
    } else {
      basepath = FsHelpers::extractFolderPath(basepath);
      if (basepath.empty()) basepath = "/";
      loadFiles();
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    activityManager.goToFolioShelf(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    activityManager.goToFolioShelf(2);
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    if (touchY < layout.contentTop) {
      const int tab = std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth()));
      if (tab == 0 && libraryFilter != LibraryFilter::All) {
        libraryFilter = LibraryFilter::All;
        applyLibraryFilter();
        requestUpdate();
        return;
      }
      if (tab == 1) activityManager.goToFolioShelf(1);
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
  // Metadata and cover extraction are deliberately selected-item work. Do not
  // scan/generate a background cover merely because the highlight moved: the
  // selected item must remain still for the debounce in loadSelectedMetadata().
  loadSelectedMetadata();
}

void FolioLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (retrieveAllComplete) {
    GUI.drawPopup(renderer, RETRIEVE_ALL_BOOKS_DONE_MESSAGE);
    return;
  }
  if (retrievingAllBooks) {
    // Render the message before any Library list or cover work.  This makes
    // the operation visibly start even on the slower X3 display path.
    GUI.drawPopup(renderer, RETRIEVE_ALL_BOOKS_MESSAGE);
    retrievingAllBooksPopupRendered = true;
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = theme.shelfLayout(renderer, metrics);
  theme.drawShelfTabs(renderer, layout, 0);

  const int detailTop = layout.contentTop;
  const int detailHeight = layout.detailHeight;
  const int featuredTop = detailTop;
  const int featuredHeight = detailHeight;

  constexpr int detailPadding = 12;
  constexpr int detailCoverWidth = 126;
  const int coverX = detailPadding;
  const int coverY = featuredTop + 7;
  const int coverWidth = detailCoverWidth;
  const int coverHeight = featuredHeight - 20;
  const size_t slot = selectorIndex >= previewPageStart ? selectorIndex - previewPageStart : PAGE_SIZE;
  const Preview* selected = slot < PAGE_SIZE ? &previews[slot] : nullptr;
  bool drewCover = false;
  if (selected && selected->loaded && !selected->directory && !selected->coverBmpPath.empty()) {
    const std::string thumb = UITheme::getCoverThumbPath(selected->coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
    HalFile file;
    if (Storage.openFileForRead("FLIB", thumb, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        // Fit the cover inside its featured slot without stretching it, then
        // center it to keep the largest possible visible cover area.
        const int sourceWidth = bitmap.getWidth();
        const int sourceHeight = bitmap.getHeight();
        int drawWidth = sourceWidth;
        int drawHeight = sourceHeight;
        if (sourceWidth > coverWidth || sourceHeight > coverHeight) {
          if (static_cast<long long>(sourceWidth) * coverHeight >
              static_cast<long long>(sourceHeight) * coverWidth) {
            drawWidth = coverWidth;
            drawHeight = std::max(1, sourceHeight * coverWidth / sourceWidth);
          } else {
            drawHeight = coverHeight;
            drawWidth = std::max(1, sourceWidth * coverHeight / sourceHeight);
          }
        }
        const int drawX = coverX + (coverWidth - drawWidth) / 2;
        const int drawY = coverY + (coverHeight - drawHeight) / 2;
        renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
        renderer.drawRect(coverX, coverY, coverWidth, coverHeight);
        drewCover = true;
      }
    }
  }
  if (!drewCover) {
    renderer.drawRect(coverX + 8, coverY, coverWidth - 16, coverHeight);
    if (selected && selected->directory) renderer.drawCenteredText(UI_12_FONT_ID, coverY + coverHeight / 2, "/", true);
  }
  const int textX = detailPadding * 2 + detailCoverWidth;
  const int textWidth = renderer.getScreenWidth() - textX - detailPadding;
  const char* titleText = selected && selected->loaded ? selected->title.c_str()
                                                        : (files.empty() ? "" : files[selectorIndex].c_str());
  renderer.drawText(UI_12_FONT_ID, textX, featuredTop + 20,
                    renderer.truncatedText(UI_12_FONT_ID, titleText, textWidth).c_str(), true);
  if (selected && !selected->author.empty())
    renderer.drawText(UI_10_FONT_ID, textX, featuredTop + 55,
                      renderer.truncatedText(UI_10_FONT_ID, selected->author.c_str(), textWidth).c_str());
  const char* synopsis = selected && !selected->synopsis.empty() ? selected->synopsis.c_str() : tr(STR_NO_SYNOPSIS);
  const int synopsisY = featuredTop + 79;
  const int progressTextY = featuredTop + featuredHeight - 54;
  constexpr int SYNOPSIS_PROGRESS_GAP_PX = 2;
  const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  const int synopsisMaxLines = std::clamp(
      (progressTextY - synopsisY - SYNOPSIS_PROGRESS_GAP_PX) / synopsisLineHeight, 1, 5);
  const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsis, textWidth, synopsisMaxLines);
  int synopsisDrawY = synopsisY;
  for (const auto& line : synopsisLines) {
    renderer.drawText(SMALL_FONT_ID, textX, synopsisDrawY, line.c_str());
    synopsisDrawY += synopsisLineHeight;
  }
  if (selected && !selected->directory) {
    const std::string selectedPath = fullPath(selectorIndex);
    const BookState* trackedState = BOOK_STATES.find(selectedPath);
    const RecentBook* trackedRecent = nullptr;
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    const auto recentIt = std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) {
      return book.path == selectedPath;
    });
    if (recentIt != recentBooks.end()) trackedRecent = &*recentIt;
    const uint8_t progress = std::max<uint8_t>(selected->progressPercent,
                                                trackedState ? trackedState->progressPercent : 0);
    const uint32_t seconds = std::max<uint32_t>(trackedState ? trackedState->readingSeconds : 0,
                                                trackedRecent ? trackedRecent->readingSeconds : 0);
    const uint16_t sessions = std::max<uint16_t>(trackedState ? trackedState->readingSessions : 0,
                                                 trackedRecent ? trackedRecent->readingSessions : 0);
    char progressLine[96];
    snprintf(progressLine, sizeof(progressLine), "%s - %u%% - %lu min - %u sessions",
             progress >= 100 ? tr(STR_COMPLETE) : (progress > 0 ? tr(STR_ONGOING) : tr(STR_NEW)), progress,
             static_cast<unsigned long>((seconds + 30) / 60), sessions);
    const std::string progressText = renderer.truncatedText(SMALL_FONT_ID, progressLine, textWidth);
    renderer.drawText(SMALL_FONT_ID, textX, progressTextY, progressText.c_str());
    const int progressY = featuredTop + featuredHeight - 28;
    renderer.drawRect(textX, progressY, textWidth, 12);
    const int fill = (textWidth - 2) * progress / 100;
    if (fill > 0) renderer.fillRect(textX + 1, progressY + 1, fill, 10);
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

  if (menuPopup.processRender(renderer, mappedInput)) return;
  if (bookActionsPopup.processRender(renderer, mappedInput)) return;
  if (retrievingMetadata && retrievingMetadataIndex == selectorIndex) {
    GUI.drawPopup(renderer, tr(STR_RETRIEVING_BOOK_DETAILS));
    retrievingPopupRendered = true;
    return;
  }
  const auto labels = mappedInput.mapLabels(basepath == "/" ? tr(STR_MENU) : tr(STR_DIR_UP), tr(STR_OPEN),
                                            tr(STR_RECENT), tr(STR_FINISHED));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
