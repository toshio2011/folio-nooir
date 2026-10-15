#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "../../ClippingEntry.h"
#include "../Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Lightweight paged list of saved clippings. Details are loaded from the same
// per-book store; the append-only /My Clippings.txt export remains unchanged.
class EpubReaderClippingListActivity final : public Activity {
  struct ClippingItem {
    std::string bookPath;
    std::string bookTitle;
    ClippingEntry clipping;
    std::string note;
    size_t bookClipIndex = 0;
  };
 public:
  EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                  std::string bookTitle, const bool allBooks = false)
      : Activity("EpubReaderClippings", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)),
        allBooks(allBooks) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath;
  std::string bookTitle;
  std::vector<ClippingItem> clippings;
  bool allBooks = false;
  int selectedIndex = 0;
  size_t allBooksOffset = 0;
  bool allBooksHasMore = false;
  static constexpr size_t ALL_BOOKS_PAGE_SIZE = 12;
  ButtonNavigator buttonNavigator;
  OptionPopup actionsPopup;
  OptionPopup confirmPopup;
  // The menu's Confirm press can still be held when this activity is pushed.
  // Consume that release so it cannot immediately open clipping actions.
  bool swallowInitialConfirmRelease = true;

  int listTop() const;
  int listHeight() const;
  void showActions();
  void editSelected();
  void editNoteSelected();
  void deleteSelected();
  void loadCurrentBookClippings();
  void loadAllBooksPage();
  bool changeSavedClipping(const ClippingItem& item, const std::string* newText, const std::string* newNote,
                           bool erase);
  void changeAllBooksPage(int direction);
};
