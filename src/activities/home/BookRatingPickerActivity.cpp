#include "BookRatingPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "BookStateStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookRating.h"

namespace {
BookRating::StarBounds pickerStarBounds(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing - 54;
  return {metrics.contentSidePadding, top, renderer.getScreenWidth() - metrics.contentSidePadding * 2,
          std::max(0, bottom - top)};
}

BookRating::StarRow pickerStarRow(const GfxRenderer& renderer) {
  const auto bounds = pickerStarBounds(renderer);
  const int starSize = std::clamp(renderer.getScreenWidth() / 24, 16, 28);
  return BookRating::centeredStarRowInBounds(renderer.getScreenWidth(), renderer.getScreenHeight(), bounds, starSize,
                                              std::max(8, starSize / 3));
}
}  // namespace

BookRatingPickerActivity::BookRatingPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   std::string bookPath, const uint8_t currentRating)
    : Activity("BookRatingPicker", renderer, mappedInput),
      bookPath_(std::move(bookPath)),
      originalRating_(std::min<uint8_t>(currentRating, BookRating::kMaximum)),
      selectedRating_(originalRating_) {}

void BookRatingPickerActivity::onEnter() {
  Activity::onEnter();
  ignoreInitialConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                         mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  renderer.setUiScaleTextEnabled(true);
  requestUpdate();
}

void BookRatingPickerActivity::adjust(const int direction) {
  if (direction > 0) {
    selectedRating_ = std::min<uint8_t>(BookRating::kMaximum, static_cast<uint8_t>(selectedRating_ + 1));
  } else if (selectedRating_ > 0) {
    --selectedRating_;
  }
  requestUpdate();
}

void BookRatingPickerActivity::saveAndClose() {
  if (selectedRating_ != originalRating_) BOOK_STATES.updateRating(bookPath_, selectedRating_);
  finish();
}

void BookRatingPickerActivity::cancelAndClose() {
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BookRatingPickerActivity::loop() {
  if (ignoreInitialConfirm) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ignoreInitialConfirm = false;
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelAndClose();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    adjust(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    adjust(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    saveAndClose();
    return;
  }

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  const BookRating::StarRow row = pickerStarRow(renderer);
  if (row.starSize >= 5 && y >= row.y - 12 && y < row.y + row.starSize + 12 && x >= row.x - row.spacing &&
      x < row.x + row.width + row.spacing) {
    if (x < row.x) {
      selectedRating_ = 0;
    } else {
      const int index =
          std::clamp((x - row.x) / (row.starSize + row.spacing), 0, static_cast<int>(BookRating::kMaximum) - 1);
      selectedRating_ = static_cast<uint8_t>(index + 1);
    }
    requestUpdate();
  }
}

void BookRatingPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_BOOK_RATING));
  const BookRating::StarRow row = pickerStarRow(renderer);
  BookRating::drawStarsInBounds(renderer, pickerStarBounds(renderer), selectedRating_, row.starSize, row.spacing,
                                selectedRating_, true);
  renderer.drawCenteredText(UI_10_FONT_ID, row.y + row.starSize + 28,
                            (std::to_string(selectedRating_) + " / 5").c_str());
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
