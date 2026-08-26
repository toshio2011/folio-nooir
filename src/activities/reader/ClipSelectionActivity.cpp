#include "ClipSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

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

void drawStyledWord(GfxRenderer& renderer, const int fontId, const int x, const int y, const char* text,
                    const EpdFontFamily::Style style, const uint8_t focusBoundary, const uint16_t focusSuffixX,
                    const bool black) {
  if (!text || *text == '\0') return;
  if (focusBoundary == 0) {
    renderer.drawText(fontId, x, y, text, black, style);
    return;
  }

  static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
  char boldPrefix[MAX_FOCUS_PREFIX_BYTES + 3]{};
  const size_t textLength = strlen(text);
  const size_t prefixLength = std::min<size_t>({static_cast<size_t>(focusBoundary), textLength,
                                                sizeof(boldPrefix) - 1});
  memcpy(boldPrefix, text, prefixLength);
  boldPrefix[prefixLength] = '\0';
  const auto boldStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
  renderer.drawText(fontId, x, y, boldPrefix, black, boldStyle);
  if (prefixLength < textLength) renderer.drawText(fontId, x + focusSuffixX, y, text + prefixLength, black, style);
}

}  // namespace

void ClipSelectionActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  // The page snapshot is allocated after the first render, when font/page
  // allocations have finished. This avoids taking a large contiguous block
  // away from the EPUB renderer during its memory-sensitive prewarm pass.
  pageSnapshotSize = 0;
  pageSnapshot.reset();
  pageSnapshotValid = false;
  extractWords();
  if (!words.empty()) {
    selected = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
    if (selected < 0) selected = 0;
  }
  requestUpdate();
}

void ClipSelectionActivity::onExit() {
  pageSnapshot.reset();
  pageSnapshotSize = 0;
  pageSnapshotValid = false;
  Activity::onExit();
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
      word.focusBoundary = block->focusBoundary(i);
      word.focusSuffixX = block->focusSuffixX(i);
      words.push_back(word);
      rowHasWords = true;
      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(word.style) & 0x03));
      if (word.focusBoundary > 0) styleMask |= static_cast<uint8_t>(1u << 1);
    }
    if (rowHasWords) rowCount++;
  }

  if (styleMask == 0) styleMask = 0x01;
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  // Re-measure after the SD font advance table has been primed.
  for (auto& word : words) {
    const size_t textLength = strlen(word.text);
    const size_t prefixLength = std::min<size_t>(word.focusBoundary, textLength);
    const int fullWidth = prefixLength > 0
                              ? word.focusSuffixX + renderer.getTextAdvanceX(fontId, word.text + prefixLength, word.style)
                              : renderer.getTextAdvanceX(fontId, word.text, word.style);
    word.width = static_cast<int16_t>(std::max(0, fullWidth));
  }
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
  const int first = std::min(startIndex, selected);
  const int last = std::max(startIndex, selected);
  if (first >= 0 && last >= first && last <= static_cast<int>(UINT16_MAX)) {
    clipping.firstWord = static_cast<uint16_t>(first);
    clipping.lastWord = static_cast<uint16_t>(last);
    clipping.hasWordRange = true;
  }
  uint8_t styleMask = 0;
  bool hasBold = false;
  for (int i = first; i <= last && i >= 0 && i < static_cast<int>(words.size()); ++i) {
    const uint8_t sourceStyle = static_cast<uint8_t>(words[i].style);
    const bool runBold = words[i].focusBoundary > 0 || (sourceStyle & static_cast<uint8_t>(EpdFontFamily::BOLD)) != 0;
    styleMask |= static_cast<uint8_t>(1u << (sourceStyle & 0x03));
    hasBold = hasBold || runBold;
    if (words[i].focusBoundary > 0) styleMask |= static_cast<uint8_t>(1u << 1);
    LOG_DBG("CLP", "source_run index=%d style=%u bold=%u", i, static_cast<unsigned>(sourceStyle),
            runBold ? 1u : 0u);
    LOG_DBG("CLP", "selection_run index=%d style=%u bold=%u", i, static_cast<unsigned>(sourceStyle),
            runBold ? 1u : 0u);
  }
  clipping.styleMask = styleMask;
  clipping.bold = hasBold;
  LOG_DBG("CLP", "selection start=%d end=%d", first, last);
  LOG_DBG("CLP", "action=highlight bold=%u", hasBold ? 1u : 0u);
  LOG_DBG("CLP", "save style=%u bold=%u highlighted=1", static_cast<unsigned>(styleMask), hasBold ? 1u : 0u);
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

  using Button = MappedInputManager::Button;
  buttonNavigator.onRelease({Button::Left}, [this] {
    if (selected > 0) {
      selected--;
      requestUpdate();
    }
  });
  buttonNavigator.onContinuous({Button::Left}, [this] {
    if (selected > 0) {
      selected--;
      requestUpdate();
    }
  });
  buttonNavigator.onRelease({Button::Right}, [this] {
    if (selected + 1 < static_cast<int>(words.size())) {
      selected++;
      requestUpdate();
    }
  });
  buttonNavigator.onContinuous({Button::Right}, [this] {
    if (selected + 1 < static_cast<int>(words.size())) {
      selected++;
      requestUpdate();
    }
  });
  buttonNavigator.onRelease({Button::Up}, [this] { moveVertical(-1); });
  buttonNavigator.onContinuous({Button::Up}, [this] { moveVertical(-1); });
  buttonNavigator.onRelease({Button::Down}, [this] { moveVertical(1); });
  buttonNavigator.onContinuous({Button::Down}, [this] { moveVertical(1); });
}

void ClipSelectionActivity::drawSelection() {
  if (words.empty()) return;
  const int first = startIndex < 0 ? selected : std::min(startIndex, selected);
  const int last = startIndex < 0 ? selected : std::max(startIndex, selected);
  const bool darkMode = renderer.isDarkMode();

  // Paint one continuous span per line, rather than a separate chip around
  // every word. This makes a multi-word selection visibly read as one range,
  // including the spaces between words, while preserving line breaks.
  int lineStart = first;
  for (int i = first; i <= last; ++i) {
    if (i != last && words[i + 1].row == words[i].row) continue;
    const auto& startWord = words[lineStart];
    const auto& endWord = words[i];
    const int spanX = startWord.x - 2;
    const int spanRight = endWord.x + endWord.width + 2;
    renderer.fillRect(spanX, startWord.y - 2, std::max(1, spanRight - spanX), lineHeight + 4, !darkMode);
    lineStart = i + 1;
  }

  if (darkMode) {
    renderer.setDarkMode(false);
    for (int i = first; i <= last; ++i) {
      const auto& word = words[i];
      drawStyledWord(renderer, fontId, word.x, word.y, word.text, word.style, word.focusBoundary, word.focusSuffixX, true);
    }
    renderer.setDarkMode(true);
  } else {
    for (int i = first; i <= last; ++i) {
      const auto& word = words[i];
      drawStyledWord(renderer, fontId, word.x, word.y, word.text, word.style, word.focusBoundary, word.focusSuffixX, false);
    }
  }
}

void ClipSelectionActivity::drawHints() const {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), startIndex < 0 ? tr(STR_SELECT) : "Save", tr(STR_DIR_LEFT),
                                            tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClipSelectionActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  const bool canRestoreSnapshot = pageSnapshotValid && pageSnapshot && pageSnapshotSize == renderer.getBufferSize();
  if (canRestoreSnapshot) {
    memcpy(renderer.getFrameBuffer(), pageSnapshot.get(), pageSnapshotSize);
  } else if (page) {
    renderer.clearScreen();
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    page->render(renderer, fontId, marginLeft, marginTop);
    scope.endScanAndPrewarm();
    page->render(renderer, fontId, marginLeft, marginTop);
    if (!pageSnapshot && pageSnapshotSize == 0) {
      pageSnapshotSize = renderer.getBufferSize();
      pageSnapshot = makeUniqueNoThrow<uint8_t[]>(pageSnapshotSize);
    }
    if (pageSnapshot && pageSnapshotSize == renderer.getBufferSize()) {
      memcpy(pageSnapshot.get(), renderer.getFrameBuffer(), pageSnapshotSize);
      pageSnapshotValid = true;
    }
    drawSelection();
  } else {
    renderer.clearScreen();
  }
  if (canRestoreSnapshot) drawSelection();
  drawHints();
  if (saveFailed) GUI.drawPopup(renderer, "Could not save clipping");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
