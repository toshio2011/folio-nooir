#pragma once

#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"

// Select a contiguous span of words on the current EPUB page. Confirm once to
// mark the start, move to the end, then Confirm again to save the clipping.
class ClipSelectionActivity final : public Activity {
 public:
  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        std::string bookPath, std::string bookTitle, int marginLeft, int marginTop, int spineIndex,
                        int pageNumber, float percentage)
      : Activity("ClipSelection", renderer, mappedInput),
        page(std::move(page)),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        spineIndex(spineIndex),
        pageNumber(pageNumber),
        percentage(percentage) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct WordBox {
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    uint16_t row = 0;
    const char* text = nullptr;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };

  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  bool saveSelection();
  std::string buildSelectionText() const;
  void drawSelection();
  void drawHints() const;

  std::unique_ptr<Page> page;
  const std::string bookPath;
  const std::string bookTitle;
  const int marginLeft;
  const int marginTop;
  const int spineIndex;
  const int pageNumber;
  const float percentage;

  int fontId = 0;
  int lineHeight = 0;
  std::vector<WordBox> words;
  uint16_t rowCount = 0;
  int selected = 0;
  int startIndex = -1;
  bool confirmPressSeen = false;
  bool saveFailed = false;
};
