#include "EpubReaderClippingListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
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
constexpr size_t MAX_PREVIEW_BYTES = 96;

std::string previewText(const std::string& text, const size_t maxBytes = MAX_PREVIEW_BYTES) {
  std::string preview;
  const size_t take = std::min(text.size(), maxBytes);
  size_t safeTake = take;
  while (safeTake > 0 && safeTake < text.size() &&
         (static_cast<unsigned char>(text[safeTake]) & 0xC0u) == 0x80u) {
    --safeTake;
  }
  preview.reserve(safeTake + 3);
  for (size_t i = 0; i < safeTake; ++i) {
    const char c = text[i];
    if (c == '\n' || c == '\r' || c == '\t') {
      if (!preview.empty() && preview.back() != ' ') preview.push_back(' ');
    } else {
      preview.push_back(c);
    }
  }
  if (safeTake < text.size()) preview += "...";
  return preview;
}

std::string dateLabel(const uint32_t dateKey) {
  if (dateKey == 0) return "Unknown date";
  char value[16];
  std::snprintf(value, sizeof(value), "%04lu-%02lu-%02lu", static_cast<unsigned long>(dateKey / 10000UL),
                static_cast<unsigned long>((dateKey / 100UL) % 100UL),
                static_cast<unsigned long>(dateKey % 100UL));
  return value;
}

std::string escapeHtmlText(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char value : text) {
    switch (value) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      default: escaped.push_back(value); break;
    }
  }
  return escaped;
}

template <typename Visitor>
bool visitKnownBooks(Visitor&& visitor) {
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (FsHelpers::hasEpubExtension(recent.path) && !visitor(recent.path, recent.title)) return false;
  }
  for (const auto& state : BOOK_STATES.getBooks()) {
    if (!FsHelpers::hasEpubExtension(state.path)) continue;
    const bool alreadyRecent = std::any_of(RECENT_BOOKS.getBooks().begin(), RECENT_BOOKS.getBooks().end(),
                                           [&](const RecentBook& recent) { return recent.path == state.path; });
    if (!alreadyRecent && !visitor(state.path, std::string{})) return false;
  }
  return true;
}
}  // namespace

void EpubReaderClippingListActivity::onEnter() {
  Activity::onEnter();
  swallowInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  if (allBooks) {
    allBooksOffset = 0;
    selectedIndex = 0;
    loadAllBooksPage();
  } else {
    loadCurrentBookClippings();
  }
  if (selectedIndex >= static_cast<int>(clippings.size()))
    selectedIndex = std::max(0, static_cast<int>(clippings.size()) - 1);
  requestUpdate();
}

void EpubReaderClippingListActivity::loadCurrentBookClippings() {
  clippings.clear();
  std::vector<ClippingEntry> saved;
  std::vector<std::string> notes;
  if (!ClipFile::load(bookPath, saved, notes)) return;
  clippings.reserve(saved.size());
  for (size_t i = 0; i < saved.size(); ++i) {
    std::string note = i < notes.size() ? std::move(notes[i]) : std::string{};
    clippings.push_back(ClippingItem{bookPath, bookTitle, std::move(saved[i]), std::move(note), i});
  }
}

void EpubReaderClippingListActivity::loadAllBooksPage() {
  clippings.clear();
  clippings.reserve(ALL_BOOKS_PAGE_SIZE);
  allBooksHasMore = false;
  size_t remaining = allBooksOffset;

  visitKnownBooks([&](const std::string& path, const std::string& title) {
    size_t bookCount = 0;
    if (!ClipFile::count(path, bookCount) || bookCount == 0) return true;
    if (clippings.size() >= ALL_BOOKS_PAGE_SIZE) {
      allBooksHasMore = true;
      return false;
    }
    if (remaining >= bookCount) {
      remaining -= bookCount;
      return true;
    }

    const size_t start = remaining;
    remaining = 0;
    const size_t requested = std::min(ALL_BOOKS_PAGE_SIZE - clippings.size(), bookCount - start);
    std::vector<ClippingEntry> page;
    std::vector<std::string> notes;
    size_t actualCount = 0;
    if (!ClipFile::loadPage(path, start, requested, page, notes, actualCount)) return true;
    for (size_t i = 0; i < page.size(); ++i) {
      std::string note = i < notes.size() ? std::move(notes[i]) : std::string{};
      clippings.push_back(ClippingItem{path, title, std::move(page[i]), std::move(note), start + i});
    }
    if (start + page.size() < actualCount) {
      allBooksHasMore = true;
      return false;
    }
    return true;
  });
  if (selectedIndex >= static_cast<int>(clippings.size()))
    selectedIndex = std::max(0, static_cast<int>(clippings.size()) - 1);
}

void EpubReaderClippingListActivity::showActions() {
  if (clippings.empty() || selectedIndex >= static_cast<int>(clippings.size())) return;
  const char* noteAction = clippings[selectedIndex].note.empty() ? tr(STR_ADD_NOTE) : tr(STR_EDIT_NOTE);
  const char* options[] = {tr(STR_CANCEL), tr(STR_READ_CLIPPING), tr(STR_EDIT_CLIP_TEXT), noteAction, tr(STR_DELETE)};
  actionsPopup.show("Clipping actions", options, 5, 0, [this](const int action) {
    if (action == 1) {
      const ClippingItem item = clippings[selectedIndex];
      std::string detail = "Highlight:\n\n" + escapeHtmlText(item.clipping.text);
      if (!item.note.empty()) detail += "\n\nNote:\n\n" + escapeHtmlText(item.note);
      detail += "\n\nBook: " + escapeHtmlText(item.bookTitle.empty() ? item.bookPath : item.bookTitle);
      detail += "\nPage: " + std::to_string(static_cast<unsigned>(item.clipping.page) + 1);
      detail += "\nSpine: " + std::to_string(static_cast<unsigned>(item.clipping.spineIndex) + 1);
      detail += "\nProgress: " + std::to_string(static_cast<unsigned>(item.clipping.percentage * 100.0f + 0.5f)) + "%";
      detail += "\nDate: " + dateLabel(item.clipping.dateKey);
      if (item.clipping.hasWordRange) {
        detail += "\nWord range: " + std::to_string(item.clipping.firstWord) + "–" +
                  std::to_string(item.clipping.lastWord);
      }
      detail += "\nStyle mask: " + std::to_string(static_cast<unsigned>(item.clipping.styleMask));
      if (item.clipping.bold) detail += " (bold)";
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, "Clipping details", item.bookTitle, detail),
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
        const ClippingItem original = clippings[index];
        if (!changeSavedClipping(original, &text, nullptr, false)) return;
        if (allBooks) {
          loadAllBooksPage();
        } else {
          loadCurrentBookClippings();
        }
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
        const ClippingItem original = clippings[index];
        const std::string& note = std::get<KeyboardResult>(result.data).text;
        if (!changeSavedClipping(original, nullptr, &note, false)) return;
        if (allBooks) {
          loadAllBooksPage();
        } else {
          loadCurrentBookClippings();
        }
        requestUpdate(true);
      });
}

void EpubReaderClippingListActivity::deleteSelected() {
  if (selectedIndex >= static_cast<int>(clippings.size())) return;
  const ClippingItem original = clippings[selectedIndex];
  if (!changeSavedClipping(original, nullptr, nullptr, true)) return;
  if (allBooks) {
    loadAllBooksPage();
    if (clippings.empty() && allBooksOffset > 0) {
      allBooksOffset -= std::min(allBooksOffset, ALL_BOOKS_PAGE_SIZE);
      loadAllBooksPage();
    }
  } else {
    loadCurrentBookClippings();
    if (selectedIndex >= static_cast<int>(clippings.size()) && selectedIndex > 0) --selectedIndex;
  }
  requestUpdate(true);
}

bool EpubReaderClippingListActivity::changeSavedClipping(const ClippingItem& item, const std::string* newText,
                                                         const std::string* newNote, const bool erase) {
  std::vector<ClippingEntry> bookClippings;
  std::vector<std::string> notes;
  if (!ClipFile::load(item.bookPath, bookClippings, notes) || item.bookClipIndex >= bookClippings.size()) return false;
  const ClippingEntry& saved = bookClippings[item.bookClipIndex];
  const std::string savedNote = item.bookClipIndex < notes.size() ? notes[item.bookClipIndex] : std::string{};
  if (saved.text != item.clipping.text || saved.page != item.clipping.page || saved.spineIndex != item.clipping.spineIndex ||
      saved.dateKey != item.clipping.dateKey || saved.percentage != item.clipping.percentage ||
      saved.styleMask != item.clipping.styleMask || saved.bold != item.clipping.bold ||
      saved.hasWordRange != item.clipping.hasWordRange || saved.firstWord != item.clipping.firstWord ||
      saved.lastWord != item.clipping.lastWord || savedNote != item.note) {
    LOG_DBG("CLP", "Skipped stale clipping edit for %s", item.bookPath.c_str());
    return false;
  }
  if (notes.size() < bookClippings.size()) notes.resize(bookClippings.size());
  if (erase) {
    bookClippings.erase(bookClippings.begin() + item.bookClipIndex);
    notes.erase(notes.begin() + item.bookClipIndex);
  } else {
    if (newText) bookClippings[item.bookClipIndex].text = *newText;
    if (newNote) notes[item.bookClipIndex] = *newNote;
  }
  return ClipFile::replace(item.bookPath, bookClippings, notes);
}

int EpubReaderClippingListActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + TITLE_HEIGHT;
}

int EpubReaderClippingListActivity::listHeight() const {
  return std::max(1, renderer.getScreenHeight() - listTop() - FOOTER_HEIGHT);
}

void EpubReaderClippingListActivity::changeAllBooksPage(const int direction) {
  if (!allBooks || direction == 0) return;
  if (direction > 0) {
    if (!allBooksHasMore) return;
    allBooksOffset += ALL_BOOKS_PAGE_SIZE;
  } else {
    if (allBooksOffset == 0) return;
    allBooksOffset -= std::min(allBooksOffset, ALL_BOOKS_PAGE_SIZE);
  }
  selectedIndex = 0;
  loadAllBooksPage();
  requestUpdate();
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
  if (allBooks && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    changeAllBooksPage(-1);
    return;
  }
  if (allBooks && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    changeAllBooksPage(1);
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
  std::string headerSubtitle;
  if (allBooks) {
    headerSubtitle = "All books · Page " + std::to_string(allBooksOffset / ALL_BOOKS_PAGE_SIZE + 1);
  } else if (!bookTitle.empty()) {
    headerSubtitle = bookTitle;
  }
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_CLIPPINGS), headerSubtitle.empty() ? nullptr : headerSubtitle.c_str());

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
          std::string subtitle;
          if (allBooks && !item.bookTitle.empty()) subtitle = previewText(item.bookTitle, 32) + " · ";
          if (!item.note.empty()) {
            subtitle += "Note: " + previewText(item.note, 56);
          } else {
            subtitle += "Page " + std::to_string(static_cast<unsigned>(clip.page) + 1) + " · " +
                        std::to_string(static_cast<int>(clip.percentage * 100.0f + 0.5f)) + "%";
          }
          return subtitle;
        },
        nullptr, [this](int index) {
          const auto& clipping = clippings[index].clipping;
          return "p." + std::to_string(static_cast<unsigned>(clipping.page) + 1);
        });
  }

  if (confirmPopup.processRender(renderer, mappedInput)) return;
  if (actionsPopup.processRender(renderer, mappedInput)) return;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), clippings.empty() ? "" : tr(STR_SELECT),
                                            allBooks ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                                            allBooks ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
