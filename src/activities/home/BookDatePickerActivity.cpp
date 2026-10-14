#include "BookDatePickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>

#include "BookStateStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StatisticsDate.h"

namespace {
constexpr StrId kDateLabels[] = {StrId::STR_STATS_STARTED, StrId::STR_STATS_FINISHED_DATE, StrId::STR_STATS_LAST_READ};

std::string dateText(const uint32_t dateKey) {
  int year = 0;
  int month = 0;
  int day = 0;
  if (!StatisticsDate::split(dateKey, year, month, day)) return "-";
  char text[16];
  snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, day);
  return text;
}
}  // namespace

BookDatePickerActivity::BookDatePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string bookPath, const uint32_t startDate,
                                               const uint32_t finishDate, const uint32_t lastReadDate,
                                               const uint32_t todayDate)
    : Activity("BookDatePicker", renderer, mappedInput),
      bookPath_(std::move(bookPath)),
      originalDates_{startDate, finishDate, lastReadDate},
      selectedDates_{startDate, finishDate, lastReadDate},
      todayDate_(StatisticsDate::isValid(todayDate) ? todayDate : 0) {}

void BookDatePickerActivity::onEnter() {
  Activity::onEnter();
  renderer.setUiScaleTextEnabled(true);
  requestUpdate();
}

void BookDatePickerActivity::selectField(const int direction) {
  const int count = static_cast<int>(std::size(kDateLabels));
  selectedField_ = static_cast<uint8_t>((static_cast<int>(selectedField_) + direction + count) % count);
  requestUpdate();
}

void BookDatePickerActivity::adjustDate(const int direction) {
  uint32_t base = selectedDates_[selectedField_];
  if (!StatisticsDate::isValid(base)) {
    if (todayDate_ == 0) return;
    base = direction < 0 ? StatisticsDate::addDays(todayDate_, -1) : todayDate_;
  } else {
    base = StatisticsDate::addDays(base, direction);
  }
  if (base == 0) return;
  selectedDates_[selectedField_] = base;
  requestUpdate();
}

void BookDatePickerActivity::saveAndClose() {
  if (selectedDates_[0] != originalDates_[0] || selectedDates_[1] != originalDates_[1] ||
      selectedDates_[2] != originalDates_[2]) {
    BOOK_STATES.updateDates(bookPath_, selectedDates_[0], selectedDates_[1], selectedDates_[2]);
  }
  finish();
}

void BookDatePickerActivity::cancelAndClose() {
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BookDatePickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelAndClose();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectField(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectField(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    adjustDate(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    adjustDate(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    saveAndClose();
    return;
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 24;
  const int hintHeight = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int rowHeight = std::max(48, (renderer.getScreenHeight() - rowTop - metrics.buttonHintsHeight - hintHeight - 12) /
                                          static_cast<int>(std::size(kDateLabels)));
  if (y >= rowTop && y < rowTop + rowHeight * static_cast<int>(std::size(kDateLabels))) {
    selectedField_ = static_cast<uint8_t>((y - rowTop) / rowHeight);
    requestUpdate();
  }
}

void BookDatePickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_BOOK_DATES));
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int rowTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 24;
  const int hintHeight = renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int rowHeight = std::max(48, (renderer.getScreenHeight() - rowTop - metrics.buttonHintsHeight - hintHeight - 12) /
                                          static_cast<int>(std::size(kDateLabels)));
  for (size_t index = 0; index < std::size(kDateLabels); ++index) {
    const int rowY = rowTop + static_cast<int>(index) * rowHeight;
    if (index == selectedField_) renderer.drawRect(side, rowY, width, rowHeight - 3, 2, true);
    const char* label = I18N.get(kDateLabels[index]);
    const std::string value = dateText(selectedDates_[index]);
    renderer.drawText(UI_10_FONT_ID, side + 12, rowY + 8, label, true,
                      index == selectedField_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_12_FONT_ID, side + 12, rowY + 28, value.c_str());
  }
  const int helpY = rowTop + rowHeight * static_cast<int>(std::size(kDateLabels)) + 2;
  renderer.drawCenteredText(SMALL_FONT_ID, helpY, tr(STR_BOOK_DATE_HELP));
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
