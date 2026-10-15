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
        clippings.push_back(ClippingItem{recent.path, recent.title, std::move(clipping), {}, false});
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
        clippings.push_back(ClippingItem{state.path, {}, std::move(clipping), {}, false});
      }
    }
  } else {
    std::vector<ClippingEntry> bookClippings;
    std::vector<std::string> notes;
    ClipFile::load(bookPath, bookClippings, notes);
    for (size_t i = 0; i < bookClippings.size(); ++i) {
      const std::string note = i < notes.size() ? std::move(notes[i]) : std::string{};
      clippings.push_back(ClippingItem{bookPath, bookTitle, std::move(bookClippings[i]), note, true});
    }
  }
  if (selectedIndex >= static_cast<int>(clippings.size())) selectedIndex = std::max(0, static_cast<int>(clippings.size()) - 1);
  requestUpdate();
}

void EpubReaderClippingListActivity::showActions() {
  if (clippings.empty() || selectedIndex >= static_cast<int>(clippings.size())) return;
  if (!clippings[selectedIndex].noteLoaded && !loadNotesForBook(clippings[selectedIndex].bookPath)) return;
  const char* noteAction = clippings[selectedIndex].note.empty() ? tr(STR_ADD_NOTE) : tr(STR_EDIT_NOTE);
  const char* options[] = {tr(STR_CANCEL), tr(STR_READ_CLIPPING), tr(STR_EDIT_CLIP_TEXT), noteAction, tr(STR_DELETE)};
  actionsPopup.show("Clipping actions", options, 5, 0, [this](const int action) {
    if (action == 1) {
      const auto& item = clippings[selectedIndex];
      startActivityForResult(std::make_unique<SynopsisActivity>(renderer, mappedInput, "Clipping", item.bookTitle,
                                                               item.clipping.text),
                             nullptr);
    } else if (action == 2) {
      editSelected();
    } else if (action == 3) {
      editNoteSelected();
    } else if (action == 4) {
      const char* confirm[] = {tr(STR_CANCEL), tr(STR_DELETE)};
      // A saved entry is both the in-book highlight and its reusable/exported
      // text clipping, so deleting it removes both representations.
      confirmPopup.show(tr(STR_DELETE_HIGHLIGHT_AND_CLIPPING), confirm, 2, 0, [this](const int choice) {
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
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EDIT_CLIP_TEXT), clippings[index].clipping.text,
                                              1024),
      [this, index](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        const std::string& text = std::get<KeyboardResult>(result.data).text;
        if (text.empty() || index < 0 || index >= static_cast<int>(clippings.size())) return;
        clippings[index].clipping.text = text;
        const std::string sourcePath = clippings[index].bookPath;
        saveBookClippings(sourcePath);
        requestUpdate(true);
      });
}

void EpubReaderClippingListActivity::editNoteSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(clippings.size())) return;
  const int index = selectedIndex;
  const bool editing = !clippings[index].note.empty();
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                              editing ? tr(STR_EDIT_NOTE) : tr(STR_ADD_NOTE),
                                              clippings[index].note, ClipFile::MAX_NOTE_BYTES),
      [this, index](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        if (index < 0 || index >= static_cast<int>(clippings.size())) return;
        clippings[index].note = std::get<KeyboardResult>(result.data).text;
        const std::string sourcePath = clippings[index].bookPath;
        saveBookClippings(sourcePath);
        requestUpdate(true);
      });
}

void EpubReaderClippingListActivity::deleteSelected() {
  if (selectedIndex >= static_cast<int>(clippings.size())) return;
  const std::string sourcePath = clippings[selectedIndex].bookPath;
  clippings.erase(clippings.begin() + selectedIndex);
  saveBookClippings(sourcePath);
  if (selectedIndex >= static_cast<int>(clippings.size()) && selectedIndex > 0) --selectedIndex;
  requestUpdate(true);
}

bool EpubReaderClippingListActivity::loadNotesForBook(const std::string& sourcePath) {
  std::vector<ClippingEntry> bookClippings;
  std::vector<std::string> notes;
  if (!ClipFile::load(sourcePath, bookClippings, notes)) return false;

  // The all-books list can span many titles. Keep detailed note text cached
  // for only the book whose actions are currently being opened.
  for (auto& item : clippings) {
    if (item.bookPath == sourcePath) continue;
    item.note.clear();
    item.noteLoaded = false;
  }

  std::vector<bool> matched(bookClippings.size(), false);
  for (auto& item : clippings) {
    if (item.bookPath != sourcePath) continue;
    size_t match = bookClippings.size();
    for (size_t i = 0; i < bookClippings.size(); ++i) {
      if (matched[i]) continue;
      const auto& saved = bookClippings[i];
      if (saved.spineIndex == item.clipping.spineIndex && saved.page == item.clipping.page &&
          saved.text == item.clipping.text) {
        match = i;
        break;
      }
    }
    item.note = match < notes.size() ? std::move(notes[match]) : std::string{};
    item.noteLoaded = true;
    if (match < matched.size()) matched[match] = true;
  }
  return true;
}

bool EpubReaderClippingListActivity::saveBookClippings(const std::string& sourcePath) {
  std::vector<ClippingEntry> bookClippings;
  std::vector<std::string> notes;
  for (const auto& item : clippings) {
    if (item.bookPath != sourcePath) continue;
    bookClippings.push_back(item.clipping);
    notes.push_back(item.note);
  }
  return ClipFile::replace(sourcePath, bookClippings, notes);
}

int EpubReaderClippingListActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + TITLE_HEIGHT;
}

int EpubReaderClippingListActivity::listHeight() const {
  return std::max(1, renderer.getScreenHeight() - listTop() - FOOTER_HEIGHT);
}

void EpubReaderClippingListActivity::loop() {
  renderer.setUiScaleTextEnabled(true);
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
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_CLIPPINGS));

  const int top = listTop();
  const int height = listHeight();
  renderer.drawLine(screen.x + 12, top - 10, screen.x + screen.width - 12, top - 10);
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
        },
        nullptr, [this](int index) { return clippings[index].note.empty() ? std::string{} : tr(STR_NOTE); });
  }

  if (confirmPopup.processRender(renderer, mappedInput)) return;
  if (actionsPopup.processRender(renderer, mappedInput)) return;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), clippings.empty() ? "" : tr(STR_SELECT),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
