#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"

// A small, read-only statistics screen designed for the X4's e-ink display.
// With a book path it shows per-book totals; without one it shows the overall
// reading summary and the latest persisted day buckets.
class ReadingStatsActivity final : public Activity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath = {})
      : Activity("ReadingStats", renderer, mappedInput), bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath;
  std::string heading;
  std::vector<std::string> lines;
  size_t firstLine = 0;

  void buildLines();
  void movePage(int direction);
};
