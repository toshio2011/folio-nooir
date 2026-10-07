#include "ToDoListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int SUMMARY_HEIGHT = 32;
constexpr int ROW_MIN_HEIGHT = 54;
constexpr int ROW_GAP = 6;
constexpr int MAX_VISIBLE_ROWS = 8;

void drawTaskCheck(GfxRenderer& renderer, const int x, const int y, const bool checked) {
  renderer.drawRoundedRect(x, y, 18, 18, 1, 3, true);
  if (!checked) return;
  renderer.drawLine(x + 4, y + 9, x + 7, y + 12, 2, true);
  renderer.drawLine(x + 7, y + 12, x + 14, y + 5, 2, true);
}
}  // namespace

void ToDoListActivity::reload() {
  items = TODO_STORE.getItems();
  if (items.empty()) selectedIndex = 0;
  else selectedIndex = std::min(selectedIndex, static_cast<int>(items.size()) - 1);
}

void ToDoListActivity::onEnter() {
  Activity::onEnter();
  swallowInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  reload();
  requestUpdate();
}

int ToDoListActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + SUMMARY_HEIGHT;
}

int ToDoListActivity::listHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int footer = metrics.buttonHintsHeight + metrics.verticalSpacing;
  return std::max(1, renderer.getScreenHeight() - listTop() - footer);
}

void ToDoListActivity::addItem() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Add task", "", 256),
      [this](const ActivityResult& result) {
        addLongPressShown = false;
        swallowAddRelease = false;
        if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
          const std::string text = std::get<KeyboardResult>(result.data).text;
          if (!text.empty()) TODO_STORE.add(text);
          reload();
          requestUpdate(true);
        }
      });
}

void ToDoListActivity::editSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size())) return;
  const uint32_t id = items[static_cast<size_t>(selectedIndex)].id;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Edit task", items[static_cast<size_t>(selectedIndex)].text, 256),
      [this, id](const ActivityResult& result) {
        editLongPressShown = false;
        swallowEditRelease = false;
        if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
          const std::string text = std::get<KeyboardResult>(result.data).text;
          const ToDoItem* item = TODO_STORE.find(id);
          if (item && !text.empty()) TODO_STORE.update(id, text, item->completed);
          reload();
          requestUpdate(true);
        }
      });
}

void ToDoListActivity::deleteSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size())) return;
  const uint32_t id = items[static_cast<size_t>(selectedIndex)].id;
  const std::string text = items[static_cast<size_t>(selectedIndex)].text;
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, "Delete task?", text),
      [this, id](const ActivityResult& result) {
        if (!result.isCancelled) TODO_STORE.remove(id);
        reload();
        requestUpdate(true);
      });
}

void ToDoListActivity::showActions() {
  const char* options[] = {"Cancel",       "Add task",       "Edit task", "Delete task",
                           "Toggle priority", "Move up",    "Move down", "Clear completed"};
  actionsPopup.show("Task actions", options, 8, 0, [this](const int action) {
    if (action == 1) addItem();
    else if (action == 2) editSelected();
    else if (action == 3) deleteSelected();
    else if (action == 4 && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
      TODO_STORE.togglePriority(items[static_cast<size_t>(selectedIndex)].id);
      reload();
    } else if (action == 5 && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
      TODO_STORE.move(items[static_cast<size_t>(selectedIndex)].id, -1);
      reload();
      selectedIndex = std::max(0, selectedIndex - 1);
    } else if (action == 6 && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
      TODO_STORE.move(items[static_cast<size_t>(selectedIndex)].id, 1);
      reload();
      selectedIndex = std::min(static_cast<int>(items.size()) - 1, selectedIndex + 1);
    } else if (action == 7) {
      TODO_STORE.clearCompleted();
      reload();
    }
    requestUpdate(true);
  });
}

void ToDoListActivity::loop() {
  renderer.setUiScaleTextEnabled(true);
  if (actionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  // Keep short Up/Down navigation intact, but make Add/Edit immediately
  // reachable without opening the long-press action menu: hold Previous
  // (the Up-labeled button) to add, or hold Next (the Down-labeled button) to
  // edit the highlighted task. The release is swallowed after the keyboard
  // activity returns so it cannot move the selection unexpectedly.
  if (swallowAddRelease && mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    swallowAddRelease = false;
    return;
  }
  if (swallowEditRelease && mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    swallowEditRelease = false;
    return;
  }
  if (swallowInitialConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) swallowInitialConfirmRelease = false;
    return;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= 1000 &&
      !longPressShown) {
    longPressShown = true;
    swallowConfirmRelease = true;
    showActions();
    return;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::NavPrevious) && mappedInput.getHeldTime() >= 900 &&
      !addLongPressShown) {
    addLongPressShown = true;
    swallowAddRelease = true;
    addItem();
    return;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::NavNext) && mappedInput.getHeldTime() >= 900 &&
      !editLongPressShown) {
    editLongPressShown = true;
    swallowEditRelease = true;
    editSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (swallowConfirmRelease) {
      swallowConfirmRelease = false;
      longPressShown = false;
      return;
    }
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
      TODO_STORE.toggle(items[static_cast<size_t>(selectedIndex)].id);
      reload();
      requestUpdate(true);
    } else {
      addItem();
    }
    longPressShown = false;
    return;
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressShown = false;
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::NavPrevious) &&
      !mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    addLongPressShown = false;
  }
  if (!mappedInput.isPressed(MappedInputManager::Button::NavNext) &&
      !mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    editLongPressShown = false;
  }

  buttonNavigator.onNextRelease([this] {
    if (!items.empty()) selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    if (!items.empty()) selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });
}

void ToDoListActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_TODO_LIST));

  const int completedCount = static_cast<int>(std::count_if(items.begin(), items.end(),
                                                            [](const ToDoItem& item) { return item.completed; }));
  const int openCount = static_cast<int>(items.size()) - completedCount;
  const int summaryY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing / 2;
  const int side = metrics.contentSidePadding;
  char countText[12];
  renderer.drawText(SMALL_FONT_ID, side, summaryY, "OPEN", true, EpdFontFamily::BOLD);
  snprintf(countText, sizeof(countText), "%d", openCount);
  const int openX = side + renderer.getTextWidth(SMALL_FONT_ID, "OPEN") + 8;
  renderer.drawText(UI_10_FONT_ID, openX, summaryY - 2, countText, true, EpdFontFamily::BOLD);
  const int doneLabelX = screenWidth / 2 + 8;
  renderer.drawLine(screenWidth / 2, summaryY - 1, screenWidth / 2, summaryY + 18);
  renderer.drawText(SMALL_FONT_ID, doneLabelX, summaryY, "DONE", true, EpdFontFamily::BOLD);
  snprintf(countText, sizeof(countText), "%d", completedCount);
  const int doneX = doneLabelX + renderer.getTextWidth(SMALL_FONT_ID, "DONE") + 8;
  renderer.drawText(UI_10_FONT_ID, doneX, summaryY - 2, countText, true, EpdFontFamily::BOLD);
  renderer.drawLine(side, summaryY + 25, screenWidth - side, summaryY + 25);

  const int top = listTop();
  const int height = listHeight();
  const int rowHeight = std::max(ROW_MIN_HEIGHT, renderer.getLineHeight(UI_12_FONT_ID) + 28);
  const int rowStep = rowHeight + ROW_GAP;
  const int visibleRows = std::max(1, std::min(MAX_VISIBLE_ROWS, height / rowStep));
  const int firstVisible = items.empty() ? 0 : selectedIndex / visibleRows * visibleRows;
  const int visibleCount = std::min(visibleRows, static_cast<int>(items.size()) - firstVisible);
  const int rightInset = items.size() > static_cast<size_t>(visibleRows) ? 12 : 0;
  const int rowX = side;
  const int rowWidth = std::max(1, screenWidth - side * 2 - rightInset);

  if (items.empty()) {
    const int centerY = top + height / 2;
    const int iconX = screenWidth / 2 - 15;
    renderer.drawRoundedRect(iconX, centerY - 62, 30, 30, 2, 7, true);
    renderer.drawLine(iconX + 9, centerY - 47, iconX + 21, centerY - 47, 2, true);
    renderer.drawLine(iconX + 15, centerY - 53, iconX + 15, centerY - 41, 2, true);
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 20, "Nothing on your list", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, centerY + 12, "Press OK to add a task");
  } else {
    constexpr int priorityMarkWidth = 20;
    for (int row = 0; row < visibleCount; ++row) {
      const int index = firstVisible + row;
      const ToDoItem& item = items[static_cast<size_t>(index)];
      const int y = top + row * rowStep;
      const bool selected = index == selectedIndex;
      if (selected) {
        renderer.fillRoundedRect(rowX, y, rowWidth, rowHeight, 7, Color::LightGray);
        renderer.fillRoundedRect(rowX, y + 9, 3, rowHeight - 18, 1, Color::Black);
      } else {
        renderer.drawRoundedRect(rowX, y, rowWidth, rowHeight, 1, 7, true);
      }

      const int checkX = rowX + 14;
      const int checkY = y + (rowHeight - 18) / 2;
      drawTaskCheck(renderer, checkX, checkY, item.completed);

      const int badgeWidth = item.priority ? priorityMarkWidth : 0;
      const int textX = checkX + 18 + 14;
      const int textWidth = std::max(1, rowX + rowWidth - 14 - badgeWidth - textX);
      const EpdFontFamily::Style style = item.completed ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
      const char* text = item.text.c_str();
      std::string clipped;
      if (renderer.getTextWidth(UI_12_FONT_ID, text, style) > textWidth) {
        clipped = renderer.truncatedText(UI_12_FONT_ID, text, textWidth, style);
        text = clipped.c_str();
      }
      const int textY = y + (rowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
      renderer.drawText(UI_12_FONT_ID, textX, textY, text, true, style);

      if (item.priority) {
        const int badgeX = rowX + rowWidth - 10 - priorityMarkWidth;
        const int badgeHeight = 22;
        const int badgeY = y + (rowHeight - badgeHeight) / 2;
        renderer.drawRoundedRect(badgeX, badgeY, priorityMarkWidth, badgeHeight, 1, 5, true);
        const int markWidth = renderer.getTextWidth(SMALL_FONT_ID, "!", EpdFontFamily::BOLD);
        const int markY = badgeY + (badgeHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
        renderer.drawText(SMALL_FONT_ID, badgeX + (priorityMarkWidth - markWidth) / 2, markY, "!", true,
                          EpdFontFamily::BOLD);
      }
    }

    // Keep the theme's previous page-at-a-time selection behavior while
    // making the current position visible on long task lists.
    if (items.size() > static_cast<size_t>(visibleRows) && height > 0) {
      const int trackX = screenWidth - side - 2;
      const int thumbHeight = std::max(16, height * visibleRows / static_cast<int>(items.size()));
      const int pageCount = (static_cast<int>(items.size()) + visibleRows - 1) / visibleRows;
      const int page = selectedIndex / visibleRows;
      const int thumbTravel = std::max(0, height - thumbHeight);
      const int thumbY = top + (pageCount > 1 ? thumbTravel * page / (pageCount - 1) : 0);
      renderer.drawLine(trackX, top, trackX, top + height - 1);
      renderer.fillRect(trackX - 3, thumbY, 4, thumbHeight);
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "Add (hold)", "Edit (hold)");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
