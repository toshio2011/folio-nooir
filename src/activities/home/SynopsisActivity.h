#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "BookStateStore.h"

// A lightweight, paged reader for a book's complete synopsis. It deliberately
// lives on the activity stack so Back returns to the exact shelf/browser screen
// that opened it.
class SynopsisActivity final : public Activity {
 public:
  SynopsisActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string author,
                   std::string synopsis, std::string bookPath = {}, std::string coverBmpPath = {},
                   uint8_t progressPercent = 0, uint32_t readingSeconds = 0, uint16_t readingSessions = 0,
                   std::string series = {}, bool xtcMetadataChecked = false)
      : Activity("Synopsis", renderer, mappedInput),
        title(std::move(title)),
        author(std::move(author)),
        series(std::move(series)),
        synopsis(std::move(synopsis)),
        bookPath(std::move(bookPath)),
        coverBmpPath(std::move(coverBmpPath)),
        progressPercent(progressPercent),
        readingSeconds(readingSeconds),
        readingSessions(readingSessions),
        xtcMetadataChecked(xtcMetadataChecked) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void buildLines();
  void movePage(int direction);
  int synopsisTop() const;

  std::string title;
  std::string author;
  std::string series;
  std::string synopsis;
  std::string bookPath;
  std::string coverBmpPath;
  BookStatus status = BookStatus::New;
  uint8_t progressPercent = 0;
  uint32_t readingSeconds = 0;
  uint16_t readingSessions = 0;
  bool xtcMetadataChecked = false;
  std::vector<std::string> lines;
  size_t firstLine = 0;
  bool ratingFocused = false;
  bool ignoreInitialConfirm = false;
};
