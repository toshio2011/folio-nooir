#pragma once
#include <Epub.h>

#include <memory>
#include <utility>

#include "../../BookmarkEntry.h"
#include "../Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class EpubReaderBookmarksActivity final : public Activity {
  struct BookmarkItem {
    std::string bookPath;
    std::string bookTitle;
    BookmarkEntry bookmark;
  };
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<BookmarkItem> bookmarks;
  bool allBooks = false;
  // Consume the menu's Confirm release when this list is pushed by hardware.
  bool swallowInitialConfirmRelease = false;
  bool confirmingDelete = false;
  OptionPopup confirmPopup;

 public:
  explicit EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::shared_ptr<Epub>& epub, const std::string& epubPath)
      : Activity("EpubReaderBookmarks", renderer, mappedInput), epub(epub), epubPath(epubPath) {}
  // Home-shelf entry point. The full EPUB cache is loaded only when the user
  // explicitly opens bookmark management, keeping shelf navigation light.
  EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string epubPath,
                              const bool allBooks = false)
      : Activity("EpubReaderBookmarks", renderer, mappedInput), epubPath(std::move(epubPath)), allBooks(allBooks) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Calculate the vertical space to reserve for button hints based on orientation
  int getGutterBottom(const GfxRenderer& renderer);

  // Calculate the height available for the bookmark list based on orientation
  int getListHeight(const GfxRenderer& renderer);

  // Delete the currently selected bookmark and persist the list
  void deleteSelectedBookmark();
};
