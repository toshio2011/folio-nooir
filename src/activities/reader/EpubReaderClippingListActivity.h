#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../ClippingEntry.h"
#include "../Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Lightweight list of saved clippings for the current EPUB. Selecting a row
// does not reopen or re-layout the book; the full text is shown in the row and
// the same content is available in /My Clippings.txt for export.
class EpubReaderClippingListActivity final : public Activity {
  struct ClippingItem {
    std::string bookPath;
    std::string bookTitle;
    ClippingEntry clipping;
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
  void deleteSelected();
};
