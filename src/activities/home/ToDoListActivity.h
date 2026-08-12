#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

#include "ToDoStore.h"

class ToDoListActivity final : public Activity {
 public:
  ToDoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ToDoList", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<ToDoItem> items;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  OptionPopup actionsPopup;
  bool longPressShown = false;
  bool swallowConfirmRelease = false;
  bool swallowInitialConfirmRelease = false;
  bool addLongPressShown = false;
  bool editLongPressShown = false;
  bool swallowAddRelease = false;
  bool swallowEditRelease = false;

  void reload();
  void addItem();
  void editSelected();
  void deleteSelected();
  void showActions();
  int listTop() const;
  int listHeight() const;
};
