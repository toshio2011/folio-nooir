#include "ClipSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ClipFile.h"

namespace {

bool isSelectableToken(const char* text) {
  if (!text) return false;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; p++) {
    if (*p < 0x80) {
      if (std::isalnum(*p)) return true;
    } else {
      return true;
    }
  }
  return false;
}

}  // namespace

void ClipSelectionActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  extractWords();
  if (!words.empty()) {
    selected = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
    if (selected < 0) selected = 0;
  }
  requestUpdate();
}

void ClipSelectionActivity::extractWords() {
  words.clear();
  words.reserve(128);
  rowCount = 0;
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;

  if (!page) return;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (!isSelectableToken(text)) continue;
      WordBox word;
      word.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      word.y = static_cast<int16_t>(line->yPos + marginTop);
      word.width = 0;  // measured after the SD font advance table is primed
      word.row = rowCount;
      word.text = text;
      word.style = block->wordStyle(i);
      words.push_back(word);
      rowHasWords = true;
      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(word.style) & 0x03));
    }
    if (rowHasWords) rowCount++;
  }

  if (styleMask == 0) styleMask = 0x01;
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  // Re-measure after the SD font advance table has been primed.
  for (auto& word : words) word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.text, word.style));
}

int ClipSelectionActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (words[i].row != row) continue;
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

int ClipSelectionActivity::wordAt(const int x, const int y) const {
  constexpr int SLOP = 5;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    const auto& word = words[i];
    if (x >= word.x - SLOP && x < word.x + word.width + SLOP && y >= word.y - SLOP &&
        y < word.y + lineHeight + SLOP) {
      return i;
    }
  }
  return -1;
}

void ClipSelectionActivity::moveVertical(const int direction) {
  if (words.empty()) return;
  const auto& current = words[selected];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;
  const int next = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  if (next >= 0 && next != selected) {
    selected = next;
    requestUpdate();
  }
}

std::string ClipSelectionActivity::buildSelectionText() const {
  if (words.empty() || startIndex < 0) return {};
  const int first = std::min(startIndex, selected);
  const int last = std::max(startIndex, selected);
  std::string text;
  text.reserve(256);
  for (int i = first; i <= last; i++) {
    if (i > first) {
      if (words[i].row != words[i - 1].row) {
        text.push_back('\n');
      } else if (words[i].x > words[i - 1].x + words[i - 1].width + 1) {
        text.push_back(' ');
      }
    }
    if (text.size() >= 1024) break;
    const size_t remaining = 1024 - text.size();
    text.append(words[i].text, std::min(remaining, strlen(words[i].text)));
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  return text;
}

bool ClipSelectionActivity::saveSelection() {
  const std::string text = buildSelectionText();
  if (text.empty()) return false;
  ClippingEntry clipping;
  clipping.text = text;
  clipping.percentage = std::clamp(percentage, 0.0f, 1.0f);
  clipping.spineIndex = static_cast<uint16_t>(std::max(0, spineIndex));
  clipping.page = static_cast<uint16_t>(std::max(0, pageNumber));
  return ClipFile::append(bookPath, bookTitle, std::move(clipping));
}

void ClipSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmPressSeen = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && confirmPressSeen) {
    confirmPressSeen = false;
    if (startIndex < 0) {
      startIndex = selected;
      requestUpdate();
    } else if (saveSelection()) {
      finish();
    } else {
      saveFailed = true;
      requestUpdate();
    }
    return;
  }
  if (words.empty()) return;

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    const int hit = wordAt(tx, ty);
    if (hit >= 0 && hit != selected) {
      selected = hit;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = wordAt(tx, ty);
    if (hit >= 0 && hit != selected) {
      selected = hit;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left) && selected > 0) {
    selected--;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right) &&
             selected + 1 < static_cast<int>(words.size())) {
    selected++;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveVertical(1);
  }
}

void ClipSelectionActivity::drawSelection() {
  if (words.empty()) return;
  const int first = startIndex < 0 ? selected : std::min(startIndex, selected);
  const int last = startIndex < 0 ? selected : std::max(startIndex, selected);
  for (int i = first; i <= last; i++) {
    const auto& word = words[i];
    if (renderer.isDarkMode()) {
      // Dark reader pages use a black base with white glyphs.  For a visible
      // selection, temporarily paint a white chip and draw black text on it;
      // the normal dark-mode font path intentionally always paints white ink.
      renderer.fillRect(word.x - 2, word.y - 2, word.width + 4, lineHeight + 4, false);
      renderer.setDarkMode(false);
      renderer.drawText(fontId, word.x, word.y, word.text, true, word.style);
      renderer.setDarkMode(true);
    } else {
      renderer.fillRect(word.x - 2, word.y - 2, word.width + 4, lineHeight + 4, true);
      renderer.drawText(fontId, word.x, word.y, word.text, false, word.style);
    }
  }
}

void ClipSelectionActivity::drawHints() const {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), startIndex < 0 ? tr(STR_SELECT) : "Save", tr(STR_DIR_LEFT),
                                            tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClipSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (page) {
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    page->render(renderer, fontId, marginLeft, marginTop);
    scope.endScanAndPrewarm();
    page->render(renderer, fontId, marginLeft, marginTop);
    drawSelection();
  }
  drawHints();
  if (saveFailed) GUI.drawPopup(renderer, "Could not save clipping");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
