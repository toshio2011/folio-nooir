#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <HalStorage.h>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "util/ButtonNavigator.h"

class FolioLibraryActivity final : public Activity {
 private:
  enum class LibraryFilter : uint8_t { All = 0, Reading, Unread, OnHold, Finished };

  struct Preview {
    size_t fileIndex = SIZE_MAX;
    bool loaded = false;
    bool metadataAttempted = false;
    bool directory = false;
    uint8_t progressPercent = 0;
    std::string title;
    std::string author;
    std::string synopsis;
    std::string coverBmpPath;
  };

  static constexpr size_t PAGE_SIZE = 8;
  static constexpr size_t NAME_BUFFER_SIZE = 500;

  ButtonNavigator buttonNavigator;
  OptionPopup menuPopup;
  OptionPopup bookActionsPopup;
  std::string basepath = "/";
  // Filename-only filter for the current directory. Normal search never opens
  // books or walks the SD card, so it remains immediate with large libraries.
  std::string searchQuery;
  std::string searchQueryFolded;
  bool recursiveSearchMode = false;
  bool recursiveSearchActive = false;
  std::string recursiveSearchRoot;
  std::vector<std::string> recursiveSearchDirectories;
  std::vector<std::string> recursiveSearchMatches;
  size_t recursiveSearchDirectoryIndex = 0;
  std::string recursiveSearchDirectoryPath;
  HalFile recursiveSearchDirectory;
  bool recursiveSearchDirectoryOpen = false;
  uint32_t recursiveSearchScannedEntries = 0;
  unsigned long recursiveSearchNextUiUpdateMs = 0;
  bool recursiveSearchNoResults = false;
  unsigned long recursiveSearchNoResultsUntilMs = 0;
  std::vector<std::string> allFiles;
  std::vector<std::string> files;
  LibraryFilter libraryFilter = LibraryFilter::All;
  std::unique_ptr<char[]> fileNameBuffer;
  std::array<Preview, PAGE_SIZE> previews;
  std::vector<std::string> retrieveDirectories;
  // Retrieve-all uses a streaming queue on the SD card instead of retaining
  // every book path in RAM.  Large libraries previously exhausted the X3 heap
  // before the first cover could be processed.
  HalFile retrieveQueueFile;
  HalFile retrieveThumbnailQueueFile;
  HalFile retrieveScanDirectory;
  bool retrieveQueueOpen = false;
  bool retrieveQueueReading = false;
  bool retrieveThumbnailQueueWriting = false;
  bool retrieveThumbnailQueueReading = false;
  bool retrieveScanDirectoryOpen = false;
  std::string retrieveScanDirectoryPath;
  enum class RetrieveAllStage : uint8_t { Scanning, Metadata, Thumbnails };
  RetrieveAllStage retrieveAllStage = RetrieveAllStage::Scanning;
  uint32_t retrieveAllTotal = 0;
  uint32_t retrieveAllProcessed = 0;
  uint32_t retrieveAllThumbnailTotal = 0;
  uint32_t retrieveAllThumbnailProcessed = 0;
  std::string retrieveAllSelectedPath;
  bool retrieveAllSelectedNeedsThumbnail = false;
  bool retrieveAllPriorityPending = false;
  bool retrieveAllPriorityDone = false;
  bool retrieveAllCurrentFromPriority = false;
  std::string retrieveAllCurrentPath;
  bool retrieveAllCurrentReady = false;
  unsigned long retrieveAllCurrentReadyAtMs = 0;
  unsigned long retrieveAllNextUiUpdateMs = 0;
  uint32_t retrieveAllLastHalfRefreshProcessed = 0;
  unsigned long retrieveAllCurrentStartedMs = 0;
  std::atomic<bool> retrieveAllProcessingBook{false};
  std::string retrieveAllStatusMessage;
  size_t selectorIndex = 0;
  size_t previewPageStart = SIZE_MAX;
  size_t nextPreviewSlot = 0;
  size_t observedSelectorIndex = SIZE_MAX;
  unsigned long selectionChangedMs = 0;
  size_t retrievingMetadataIndex = SIZE_MAX;
  bool retrievingMetadata = false;
  uint8_t retrievingMetadataProgress = 0;
  bool retrievingMetadataCleanupOnCancel = false;
  volatile bool retrievingPopupRendered = false;
  unsigned long retrievingMetadataStartedMs = 0;
  bool forceMetadataRefresh = false;
  size_t forceMetadataRefreshIndex = SIZE_MAX;
  size_t retrieveDirectoryIndex = 0;
  bool retrievingAllBooks = false;
  volatile bool retrievingAllBooksPopupRendered = false;
  bool retrieveAllComplete = false;
  unsigned long retrieveAllCompleteUntilMs = 0;
  bool swallowMenuBackRelease = false;
  bool longPressActionShown = false;
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;

  void loadFiles();
  void applyLibraryFilter();
  void resetPreviews();
  void loadNextPreview();
  void refreshSelectedPreviewFromCache(const std::string& path);
  void loadSelectedMetadata();
  bool matchesLibraryFilter(const std::string& name) const;
  FolioLibrarySummary getLibrarySummary() const;
  std::string fullPath(size_t index) const;
  void showMenu();
  void launchSearch(bool recursive);
  void startRecursiveSearch();
  void processRecursiveSearch();
  void clearSearch();
  void startRetrieveAllBooks();
  void processRetrieveAllBooks();
  void cancelRetrieveAllBooks();
  void finishRetrieveAllBooks(const char* message, bool showCompletion = true);
  void activateSelected();
  void showBookActions();

 public:
  explicit FolioLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path = "/")
      : Activity("FolioLibrary", renderer, mappedInput), basepath(path.empty() ? "/" : std::move(path)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Retrieval/search is active work, not an idle bookshelf. Keep the C3 at
  // full speed and prevent the global sleep timer from firing while it runs.
  bool skipLoopDelay() override { return retrievingAllBooks || recursiveSearchActive || retrievingMetadata; }
  bool preventAutoSleep() override { return retrievingAllBooks || recursiveSearchActive || retrievingMetadata; }
};
