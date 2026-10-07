#pragma once
#include <I18n.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    selectedIndex = clampIndex(currentIndex, optionCount);
    firstVisibleIndex = 0;
    onSelectCallback = std::move(onSelect);
    layoutValid = false;
    active = true;
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = options[i];
    }
    selectedIndex = clampIndex(currentIndex, optionCount);
    firstVisibleIndex = 0;
    onSelectCallback = std::move(onSelect);
    layoutValid = false;
    active = true;
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings = options;
    selectedIndex = clampIndex(currentIndex, static_cast<int>(options.size()));
    firstVisibleIndex = 0;
    onSelectCallback = std::move(onSelect);
    layoutValid = false;
    active = true;
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const int count = static_cast<int>(ownedStrings.size());
    if (count == 0) {
      active = false;
      return true;
    }
    int tx = 0;
    int ty = 0;
    if (input.wasScreenTouchDown(tx, ty)) {
      const auto& hitLayout = getLayout(input.getRenderer());
      for (int i = 0; i < static_cast<int>(hitLayout.options.size()); i++) {
        if (contains(hitLayout.options[i], tx, ty)) {
          const int optionIndex = hitLayout.firstVisibleIndex + i;
          if (selectedIndex != optionIndex) {
            selectedIndex = optionIndex;
            layoutValid = false;
            requestUpdate();
          }
          break;
        }
      }
      return true;
    }
    if (input.wasScreenTapped(tx, ty)) {
      const auto& hitLayout = getLayout(input.getRenderer());
      if (hitLayout.canScroll && contains(hitLayout.scrollUp, tx, ty)) {
        pageSelection(-1, input.getRenderer(), requestUpdate);
        return true;
      }
      if (hitLayout.canScroll && contains(hitLayout.scrollDown, tx, ty)) {
        pageSelection(1, input.getRenderer(), requestUpdate);
        return true;
      }
      for (int i = 0; i < static_cast<int>(hitLayout.options.size()); i++) {
        if (contains(hitLayout.options[i], tx, ty)) {
          selectedIndex = hitLayout.firstVisibleIndex + i;
          active = false;
          if (onSelectCallback) onSelectCallback(selectedIndex);
          requestUpdate();
          return true;
        }
      }
      // Taps on the dialog chrome (title, padding) keep the popup open; taps outside dismiss it
      if (contains(hitLayout.dialog, tx, ty)) return true;
      active = false;
      requestUpdate();
      return true;
    }

    const auto swipe = input.wasSwipe();
    if (swipe != MappedInputManager::SwipeDir::None && !getLayout(input.getRenderer()).canScroll) {
      // Preserve the pre-scroll behavior for short lists: a swipe is consumed
      // by the popup but must not change selection when every row is visible.
      return true;
    }
    if (swipe == MappedInputManager::SwipeDir::Up) {
      pageSelection(1, input.getRenderer(), requestUpdate);
      return true;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      pageSelection(-1, input.getRenderer(), requestUpdate);
      return true;
    }

    if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Left)) {
      selectedIndex = selectedIndex == 0 ? count - 1 : selectedIndex - 1;
      layoutValid = false;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Down) ||
               input.wasPressed(MappedInputManager::Button::Right)) {
      selectedIndex = (selectedIndex + 1) % count;
      layoutValid = false;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Back)) {
      active = false;
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    const auto& popupLayout = getLayout(renderer);
    GUI.drawOptionPopup(renderer, title.c_str(), ownedStrings, selectedIndex, popupLayout.firstVisibleIndex,
                        popupLayout.visibleCount);
  }

  bool isActive() const { return active; }

  // Explicitly close the popup before an activity transition.  Selection
  // normally deactivates the popup inside handleInput(), but callers that
  // launch another activity from the selection callback should be able to
  // make the transition unambiguous.
  void dismiss() {
    active = false;
  }

 private:
  struct Layout {
    Rect dialog{0, 0, 0, 0};
    std::vector<Rect> options;
    Rect scrollUp{0, 0, 0, 0};
    Rect scrollDown{0, 0, 0, 0};
    int firstVisibleIndex = 0;
    int visibleCount = 0;
    bool canScroll = false;
  };

  static int clampIndex(const int index, const int count) {
    if (count <= 0) return 0;
    return std::clamp(index, 0, count - 1);
  }

  void pageSelection(const int direction, const GfxRenderer& renderer, const std::function<void()>& requestUpdate) {
    const auto& currentLayout = getLayout(renderer);
    const int count = static_cast<int>(ownedStrings.size());
    const int step = std::max(1, currentLayout.visibleCount);
    selectedIndex = std::clamp(selectedIndex + direction * step, 0, count - 1);
    layoutValid = false;
    requestUpdate();
  }

  // Text measurement is expensive and wasScreenTouchDown() is level-triggered, so the
  // layout is computed once per show() and cached rather than rebuilt every loop().
  const Layout& getLayout(const GfxRenderer& renderer) const {
    if (layoutValid) return layout;

    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
    const EpdFontFamily::Style optionStyle =
        metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

    const int itemSpacing = metrics.optionPopupItemSpacing;
    const int innerPadding = metrics.optionPopupInnerPadding;
    const int selectionHPadding = metrics.optionPopupSelectionHPadding;
    const int selectionVPadding = metrics.optionPopupSelectionVPadding;

    const int optionLineHeight = renderer.getLineHeight(optionFontId);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int rowHeight = optionLineHeight + selectionVPadding * 2;

    int maxTextWidth = renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
    for (const auto& opt : ownedStrings) {
      const int width = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
      if (width > maxTextWidth) maxTextWidth = width;
    }

    const int optionCount = static_cast<int>(ownedStrings.size());
    if (optionCount == 0) {
      layout = Layout{};
      layoutValid = true;
      return layout;
    }
    const int verticalMargin = std::max(8, metrics.optionPopupDialogSideMargin);
    const int maxDialogHeight = std::max(1, pageHeight - verticalMargin * 2);
    const int fixedHeight = titleLineHeight + metrics.optionPopupTitleGap + innerPadding * 2;
    const int maxListHeight = std::max(rowHeight, maxDialogHeight - fixedHeight);
    const int maxVisible = std::max(1, (maxListHeight + itemSpacing) / (rowHeight + itemSpacing));
    const int visibleCount = std::min(optionCount, maxVisible);
    const int maxFirst = std::max(0, optionCount - visibleCount);
    firstVisibleIndex = std::clamp(firstVisibleIndex, 0, maxFirst);
    if (selectedIndex < firstVisibleIndex) firstVisibleIndex = selectedIndex;
    if (selectedIndex >= firstVisibleIndex + visibleCount) firstVisibleIndex = selectedIndex - visibleCount + 1;
    firstVisibleIndex = std::clamp(firstVisibleIndex, 0, maxFirst);
    const int listHeight = rowHeight * visibleCount + itemSpacing * (visibleCount - 1);
    constexpr int selectionCheckWidth = 14;
    const int dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2 + selectionCheckWidth) * 12 / 10,
                                 pageWidth - metrics.optionPopupDialogSideMargin * 2);
    const int contentHeight = titleLineHeight + metrics.optionPopupTitleGap + listHeight;
    const int dialogH = contentHeight + innerPadding * 2;
    const int dialogX = (pageWidth - dialogW) / 2;
    const int dialogY = (pageHeight - dialogH) / 2;
    const int itemRectX = dialogX + innerPadding;
    const int itemRectW = dialogW - innerPadding * 2;
    const int firstItemY = dialogY + innerPadding + titleLineHeight + metrics.optionPopupTitleGap;

    layout.dialog = Rect{dialogX, dialogY, dialogW, dialogH};
    layout.firstVisibleIndex = firstVisibleIndex;
    layout.visibleCount = visibleCount;
    layout.canScroll = visibleCount < optionCount;
    const int arrowWidth = 28;
    layout.scrollUp = Rect{dialogX + dialogW - innerPadding - arrowWidth, dialogY + innerPadding, arrowWidth,
                           titleLineHeight};
    layout.scrollDown = Rect{dialogX + dialogW - innerPadding - arrowWidth,
                             dialogY + dialogH - innerPadding - titleLineHeight, arrowWidth, titleLineHeight};
    layout.options.clear();
    layout.options.reserve(visibleCount);
    for (int i = 0; i < visibleCount; i++) {
      layout.options.push_back(Rect{itemRectX, firstItemY + i * (rowHeight + itemSpacing), itemRectW, rowHeight});
    }
    layoutValid = true;
    return layout;
  }

  static bool contains(const Rect& rect, const int x, const int y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  }

  bool active = false;
  std::string title;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  mutable int firstVisibleIndex = 0;
  std::function<void(int)> onSelectCallback;
  mutable Layout layout;
  mutable bool layoutValid = false;
};
