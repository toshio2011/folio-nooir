#include "EpubReaderClippingListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "MappedInputManager.h"
#include <FsHelpers.h>
#include "BookStateStore.h"
#include "RecentBooksStore.h"
#include "activities/home/SynopsisActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ClipFile.h"

namespace {
constexpr int TITLE_HEIGHT = 60;
constexpr int FOOTER_HEIGHT = 70;
constexpr int MAX_PREVIEW_BYTES = 180;

std::string previewText(const std::string& text) {
  std::string preview;
  preview.reserve(std::min<size_t>(text.size(), MAX_PREVIEW_BYTES));
  for (char c : text) {
    if (c == '\n' || c == '\r' || c == '\t') {
      if (!preview.empty() && preview.back() != ' ') preview.push_back(' ');
    } else {
      preview.push_back(c);
    }
    if (preview.size() >= MAX_PREVIEW_BYTES) break;
  }
  if (text.size() > preview.size()) preview += "...";
  return preview;
}
}  // namespace

void EpubReaderClippingListActivity::onEnter() {
  Activity::onEnter();
  swallowInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  clippings.clear();
  if (allBooks) {
    for (const auto& recent : RECENT_BOOKS.getBooks()) {
      if (!FsHelpers::hasEpubExtension(recent.path)) continue;
      std::vector<ClippingEntry> bookClippings;
      if (!ClipFile::load(recent.path, bookClippings)) continue;
      for (auto& clipping : bookClippings) {
        clippings.push_back(ClippingItem{recent.path, recent.title, std::move(clipping)});
      }
    }
    for (const auto& state : BOOK_STATES.getBooks()) {
      if (!FsHelpers::hasEpubExtension(state.path) ||
          std::find_if(clippings.begin(), clippings.end(), [&](const ClippingItem& item) {
            return item.bookPath == state.path;
          }) != clippings.end())
        continue;
      std::vector<ClippingEntry> bookClippings;
      if (!ClipFile::load(state.path, bookClippings)) continue;
      for (auto& clipping : bookClippings) {
        clippings.push_back(ClippingItem{state.path, {}, std::move(clipping)});
      }
    }
  } else {
    std::vector<ClippingEntry> bookClippings;
    ClipFile::load(bookPath, bookClippings);
    for (auto& clipping : bookClippings) {
      clippings.push_back(ClippingItem{bookPath, bookTitle, std::move(clipping)});
    }
  }
  if (selectedIndex >= static_cast<int>(clippings.size())) selectedIndex = std::max(0, static_cast<int>(clippings.size()) - 1);
  requestUpdate();
}

void EpubReaderClippingListActivity::showActions() {
  if (clippings.empty() || selectedIndex >= static_cast<int>(clippings.size())) return;
  const char* options[] = {"Cancel", "Read full", "Edit clipping", tr(STR_DELETE)};
  actionsPopup.show("Clipping actions", options, 4, 0, [this](const int action) {
    if (action == 1) {
      const auto& item = clippings[selectedIndex];
      startActivityForResult(std::make_unique<SynopsisActivity>(renderer, mappedInput, "Clipping", item.bookTitle,
                                                               item.clipping.text),
                             nullptr);
    } else if (action == 2) {
      editSelected();
    } else if (action == 3) {
      const char* confirm[] = {tr(STR_CANCEL), tr(STR_DELETE)};
      confirmPopup.show("Delete clipping?", confirm, 2, 0, [this](const int choice) {
        if (choice == 1) deleteSelected();
        requestUpdate();
      });
    }
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderClippingListActivity::editSelected() {
  if (selectedIndex >= static_cast<int>(clippings.size())) return;
  const int index = selectedIndex;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Edit clipping", clippings[index].clipping.text,
                                              1024),
      [this, index](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        const std::string& text = std::get<KeyboardResult>(result.data).text;
        if (text.empty() || index < 0 || index >= static_cast<int>(clippings.size())) return;
        clippings[index].clipping.text = text;
        const std::string sourcePath = clippings[index].bookPath;
        std::vector<ClippingEntry> remaining;
        for (const auto& item : clippings) {
          if (item.bookPath == sourcePath) remaining.push_back(item.clipping);
        }
        ClipFile::replace(sourcePath, remaining);
        requestUpdate(true);
      });
}

void EpubReaderClippingListActivity::deleteSelected() {
  if (selectedIndex >= static_cast<int>(clippings.size())) return;
  const std::string sourcePath = clippings[selectedIndex].bookPath;
  clippings.erase(clippings.begin() + selectedIndex);
  std::vector<ClippingEntry> remaining;
  for (const auto& item : clippings) {
    if (item.bookPath == sourcePath) remaining.push_back(item.clipping);
  }
  ClipFile::replace(sourcePath, remaining);
  if (selectedIndex >= static_cast<int>(clippings.size()) && selectedIndex > 0) --selectedIndex;
  requestUpdate(true);
}

int EpubReaderClippingListActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + TITLE_HEIGHT;
}

int EpubReaderClippingListActivity::listHeight() const {
  return std::max(1, renderer.getScreenHeight() - listTop() - FOOTER_HEIGHT);
}

void EpubReaderClippingListActivity::loop() {
  if (actionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (swallowInitialConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      swallowInitialConfirmRelease = false;
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (clippings.empty()) return;

  const int height = listHeight();
  switch (handleListTouch(selectedIndex, static_cast<int>(clippings.size()), listTop(), height, true)) {
    case ListTouchResult::Activated:
      showActions();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, clippings.size(), GUI.getListPageItems(height, true));
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, clippings.size(), GUI.getListPageItems(height, true));
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showActions();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, clippings.size());
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, clippings.size());
    requestUpdate();
  });
}

void EpubReaderClippingListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_CLIPPINGS));

  const int top = listTop();
  const int height = listHeight();
  if (clippings.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, "No clippings", true,
                              EpdFontFamily::REGULAR);
  } else {
    GUI.drawList(
        renderer, Rect{screen.x, top, screen.width, height}, clippings.size(), selectedIndex,
        [this](int index) { return previewText(clippings[index].clipping.text); },
        [this](int index) {
          const auto& item = clippings[index];
          const auto& clip = item.clipping;
          const std::string prefix = item.bookTitle.empty() ? std::string{} : item.bookTitle + "  -  ";
          return prefix + std::to_string(static_cast<int>(clip.percentage * 100.0f + 0.5f)) + "%  ·  page " +
                 std::to_string(static_cast<unsigned>(clip.page) + 1);
        });
  }

  if (confirmPopup.processRender(renderer, mappedInput)) return;
  if (actionsPopup.processRender(renderer, mappedInput)) return;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), clippings.empty() ? "" : tr(STR_SELECT),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
