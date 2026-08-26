#include "ToDoListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_HEIGHT = 42;
constexpr int FOOTER_HEIGHT = 64;

std::string rowText(const ToDoItem& item) {
  return std::string(item.priority ? "! " : "  ") + (item.completed ? "[x] " : "[ ] ") + item.text;
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
  return metrics.topPadding + metrics.headerHeight + TITLE_HEIGHT;
}

int ToDoListActivity::listHeight() const {
  return std::max(1, renderer.getScreenHeight() - listTop() - FOOTER_HEIGHT);
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
  actionsPopup.show("To-Do List", options, 8, 0, [this](const int action) {
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
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, "To-Do List");
  const int top = listTop();
  const int height = listHeight();
  const int completedCount = static_cast<int>(std::count_if(items.begin(), items.end(),
                                                            [](const ToDoItem& item) { return item.completed; }));
  const int openCount = static_cast<int>(items.size()) - completedCount;
  const std::string summary = std::to_string(openCount) + " open  ·  " + std::to_string(completedCount) + " done";
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, top - 24, summary.c_str(), true,
                    EpdFontFamily::REGULAR);
  if (items.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2 - 15, "No tasks yet", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() / 2 + 18, "Press OK to add a task");
  } else {
    GUI.drawList(renderer, Rect{metrics.contentSidePadding, top,
                                renderer.getScreenWidth() - metrics.contentSidePadding * 2, height},
                 items.size(), selectedIndex, [this](const int index) { return rowText(items[index]); },
                 [this](const int index) {
                   if (items[index].completed) return std::string("Completed");
                   return items[index].priority ? std::string("Important") : std::string("Open");
                 });
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "Add (hold)", "Edit (hold)");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
