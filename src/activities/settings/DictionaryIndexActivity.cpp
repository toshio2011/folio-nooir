#include "DictionaryIndexActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void DictionaryIndexActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  state = State::List;
  statusName.clear();
  progressPercent = 0;
  resuming = false;
  suppressBackRelease = false;
  refreshEntries();
  requestUpdate();
}

void DictionaryIndexActivity::onExit() { Activity::onExit(); }

void DictionaryIndexActivity::refreshEntries() {
  DictionaryRegistry::discover(entries);
  indexed.assign(entries.size(), false);
  resumable.assign(entries.size(), false);
  for (size_t i = 0; i < entries.size(); ++i) {
    Dictionary dictionary;
    if (dictionary.open(entries[i].name.c_str())) {
      indexed[i] = !dictionary.needsIndex();
      resumable[i] = !indexed[i] && dictionary.hasIndexResume();
    }
  }
  if (entries.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::min(selectedIndex, static_cast<int>(entries.size()) - 1);
  }
}

void DictionaryIndexActivity::beginIndex() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) return;

  statusName = entries[selectedIndex].name;
  progressPercent = 0;
  lastProgressRenderMs = 0;
  suppressBackRelease = false;
  state = State::Indexing;

  Dictionary dictionary;
  const bool opened = dictionary.open(statusName.c_str());
  resuming = opened && dictionary.hasIndexResume();
  requestUpdateAndWait();  // Show the status before the SD-card scan starts.

  const bool alreadyReady = opened && !dictionary.needsIndex();
  const bool ok = alreadyReady || (opened && dictionary.buildIndex(&DictionaryIndexActivity::indexYield, this,
                                                                    &DictionaryIndexActivity::indexProgress));
  if (ok) {
    progressPercent = 100;
    state = State::Complete;
  } else if (opened && dictionary.hasIndexResume()) {
    state = State::Cancelled;
    suppressBackRelease = true;
  } else {
    state = State::Failed;
  }
  refreshEntries();
  requestUpdate();
}

void DictionaryIndexActivity::indexYield(void*) { vTaskDelay(1); }

bool DictionaryIndexActivity::indexProgress(void* ctx, const uint32_t processedBytes, const uint32_t totalBytes) {
  auto* self = static_cast<DictionaryIndexActivity*>(ctx);
  if (!self) return true;

  if (self->mappedInput.isPressed(MappedInputManager::Button::Back)) return false;

  const uint8_t percent = totalBytes == 0
                              ? 100
                              : static_cast<uint8_t>(std::min<uint64_t>(
                                    100, (static_cast<uint64_t>(processedBytes) * 100) / totalBytes));
  self->progressPercent = percent;
  const unsigned long now = millis();
  if (now - self->lastProgressRenderMs >= 500 || percent >= 100) {
    self->lastProgressRenderMs = now;
    self->requestUpdateAndWait();
  }
  return true;
}

void DictionaryIndexActivity::finishFromResult() {
  if (state == State::Complete || state == State::Cancelled || state == State::Failed) {
    state = State::List;
    requestUpdate();
  } else {
    finish();
  }
}

void DictionaryIndexActivity::loop() {
  if (state == State::Indexing) return;

  if (suppressBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) suppressBackRelease = false;
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishFromResult();
    return;
  }
  if (state == State::Complete || state == State::Cancelled || state == State::Failed) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) finishFromResult();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    beginIndex();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int itemCount = static_cast<int>(entries.size());
  if (itemCount > 0) {
    switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, true)) {
      case ListTouchResult::Activated:
        beginIndex();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, itemCount] {
    if (itemCount > 0) {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousRelease([this, itemCount] {
    if (itemCount > 0) {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
      requestUpdate();
    }
  });
  buttonNavigator.onNextContinuous([this, itemCount, pageItems] {
    if (itemCount > 0) {
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousContinuous([this, itemCount, pageItems] {
    if (itemCount > 0) {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
      requestUpdate();
    }
  });
}

void DictionaryIndexActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_DICTIONARY_INDEXES));

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerY = (pageHeight - lineHeight) / 2;
  if (state == State::Indexing) {
    const char* prefix = resuming ? tr(STR_DICT_INDEX_RESUMING) : tr(STR_DICT_INDEXING);
    std::string text = std::string(prefix) + " " + statusName + " (" + std::to_string(progressPercent) + "%)";
    const std::string displayText = renderer.truncatedText(UI_10_FONT_ID, text.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, displayText.c_str());
    const int barWidth = std::max(40, pageWidth - 80);
    const int barY = centerY + lineHeight;
    renderer.drawRect((pageWidth - barWidth) / 2, barY, barWidth, 12, 1, true);
    if (progressPercent > 0) {
      renderer.fillRect((pageWidth - barWidth) / 2 + 2, barY + 2, (barWidth - 4) * progressPercent / 100, 8);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  } else if (state == State::Complete || state == State::Cancelled || state == State::Failed) {
    const char* result = state == State::Complete
                             ? tr(STR_DICT_INDEX_READY)
                             : state == State::Cancelled ? tr(STR_DICT_INDEX_CANCELLED) : tr(STR_DICT_ERROR);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, statusName.c_str(), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight, result);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  } else if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_DICT_NO_DICT_SET), true);
    const std::string hint = renderer.truncatedText(UI_10_FONT_ID, tr(STR_DICT_PREPARE_HINT), pageWidth - 24);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight, hint.c_str());
  } else {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()),
                 selectedIndex,
                 [this](int index) { return entries[index].name; },
                 [this](int index) { return entries[index].stem; }, nullptr,
                 [this](int index) {
                   if (indexed[index]) return std::string(tr(STR_DICT_INDEX_READY));
                   if (resumable[index]) return std::string(tr(STR_DICT_INDEX_PAUSED));
                   return std::string(tr(STR_DICT_INDEX_NEEDED));
                 },
                 true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
