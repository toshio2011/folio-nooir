#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "BookmarkEntry.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class XtcReaderBookmarksActivity final : public Activity {
  std::string bookPath;
  std::vector<BookmarkEntry> bookmarks;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

 public:
  XtcReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath)
      : Activity("XtcReaderBookmarks", renderer, mappedInput), bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
