#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"

// A lightweight, paged reader for a book's complete synopsis. It deliberately
// lives on the activity stack so Back returns to the exact shelf/browser screen
// that opened it.
class SynopsisActivity final : public Activity {
 public:
  SynopsisActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string author,
                   std::string synopsis, std::string bookPath = {})
      : Activity("Synopsis", renderer, mappedInput),
        title(std::move(title)),
        author(std::move(author)),
        synopsis(std::move(synopsis)),
        bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static std::string stripHtml(const std::string& source);
  void movePage(int direction);

  std::string title;
  std::string author;
  std::string synopsis;
  std::string bookPath;
  std::vector<std::string> lines;
  size_t firstLine = 0;
};
