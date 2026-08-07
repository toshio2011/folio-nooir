#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../../ClippingEntry.h"
#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Lightweight list of saved clippings for the current EPUB. Selecting a row
// does not reopen or re-layout the book; the full text is shown in the row and
// the same content is available in /My Clippings.txt for export.
class EpubReaderClippingListActivity final : public Activity {
 public:
  EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                  std::string bookTitle)
      : Activity("EpubReaderClippings", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath;
  std::string bookTitle;
  std::vector<ClippingEntry> clippings;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int listTop() const;
  int listHeight() const;
};
