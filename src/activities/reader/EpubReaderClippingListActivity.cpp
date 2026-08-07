#include "EpubReaderClippingListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
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
  ClipFile::load(bookPath, clippings);
  if (selectedIndex >= static_cast<int>(clippings.size())) selectedIndex = std::max(0, static_cast<int>(clippings.size()) - 1);
  requestUpdate();
}

int EpubReaderClippingListActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + TITLE_HEIGHT;
}

int EpubReaderClippingListActivity::listHeight() const {
  return std::max(1, renderer.getScreenHeight() - listTop() - FOOTER_HEIGHT);
}

void EpubReaderClippingListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (clippings.empty()) return;

  const int height = listHeight();
  switch (handleListTouch(selectedIndex, static_cast<int>(clippings.size()), listTop(), height, true)) {
    case ListTouchResult::Activated:
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
        [this](int index) { return previewText(clippings[index].text); },
        [this](int index) {
          const auto& clip = clippings[index];
          return std::to_string(static_cast<int>(clip.percentage * 100.0f + 0.5f)) + "%  ·  page " +
                 std::to_string(static_cast<unsigned>(clip.page) + 1);
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
