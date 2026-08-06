#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"

// A lightweight, paged reader for the complete synopsis stored in the book
// cache. It deliberately lives on the activity stack so Back returns to the
// exact shelf/browser screen that opened it.
class SynopsisActivity final : public Activity {
 public:
  SynopsisActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string author,
                   std::string synopsis)
      : Activity("Synopsis", renderer, mappedInput),
        title(std::move(title)),
        author(std::move(author)),
        synopsis(std::move(synopsis)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static std::string stripHtml(const std::string& source);
  void movePage(int direction);

  std::string title;
  std::string author;
  std::string synopsis;
  std::vector<std::string> lines;
  size_t firstLine = 0;
};
