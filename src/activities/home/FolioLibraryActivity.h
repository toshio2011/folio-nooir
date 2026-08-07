#pragma once

#include <array>
#include <cstdint>
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
  OptionPopup bookActionsPopup;
  std::string basepath = "/";
  std::vector<std::string> allFiles;
  std::vector<std::string> files;
  LibraryFilter libraryFilter = LibraryFilter::All;
  std::unique_ptr<char[]> fileNameBuffer;
  std::array<Preview, PAGE_SIZE> previews;
  size_t selectorIndex = 0;
  size_t previewPageStart = SIZE_MAX;
  size_t nextPreviewSlot = 0;
  size_t nextCoverSlot = 0;
  unsigned long lastCoverGenerationMs = 0;
  size_t observedSelectorIndex = SIZE_MAX;
  unsigned long selectionChangedMs = 0;
  size_t retrievingMetadataIndex = SIZE_MAX;
  bool retrievingMetadata = false;
  volatile bool retrievingPopupRendered = false;
  bool longPressActionShown = false;
  bool swallowConfirmRelease = false;
  bool swallowBackRelease = false;

  void loadFiles();
  void applyLibraryFilter();
  void resetPreviews();
  void loadNextPreview();
  void generateNextMissingCover();
  void loadSelectedMetadata();
  bool matchesLibraryFilter(const std::string& name) const;
  FolioLibrarySummary getLibrarySummary() const;
  std::string fullPath(size_t index) const;
  void activateSelected();
  void showBookActions();

 public:
  explicit FolioLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path = "/")
      : Activity("FolioLibrary", renderer, mappedInput), basepath(path.empty() ? "/" : std::move(path)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
