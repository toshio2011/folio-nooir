#include "DictionaryDefinitionActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HtmlToPlainText.h"

namespace {

// Longest measurable/drawable span. Wrapped lines stay under the screen width
// (far below this); only pathological unbreakable tokens are split at this cap.
constexpr size_t MAX_LINE_BYTES = 191;
constexpr size_t MAX_ALL_DICTIONARY_RESULTS = 4;
constexpr size_t MAX_COMBINED_DICTIONARY_BYTES = 48 * 1024;
constexpr unsigned long DICTIONARY_MESSAGE_DURATION_MS = 1500;
constexpr const char* ALL_DICTIONARIES_LABEL = "All dictionaries";

// Body text left/right inset, matching the reader's default feel.
constexpr int SIDE_PADDING = 20;

}  // namespace

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  // Normalize StarDict multi-type separators so the wrap loop and the
  // C-string font APIs below both see the whole definition.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  definition = htmlToPlainText(definition);
  dictionaryOptionsReady = false;
  searchAllOptionIndex = -1;
  showingAllResults = false;
  suppressBackRelease = false;
  showDictionaryError = false;
  wrapText();
  requestUpdate();
}

void DictionaryDefinitionActivity::refreshDictionaryOptions() {
  if (dictionaryOptionsReady) return;

  dictionaryOptions.clear();
  searchAllOptionIndex = -1;
  std::vector<DictionaryEntry> entries;
  DictionaryRegistry::discover(entries);
  for (const auto& entry : entries) {
    Dictionary dictionary;
    // Never build an index from this screen. The explicit Settings action is
    // the only place that prepares alternate dictionaries.
    if (dictionary.open(entry.name.c_str()) && !dictionary.needsIndex()) {
      dictionaryOptions.push_back(entry.name);
    }
  }

  // Keep the active source visible even if its sidecar became stale after
  // this definition was opened. A switch back to it will simply report an
  // error rather than performing an unexpected long scan.
  if (!showingAllResults && !dictionaryName.empty() &&
      std::find(dictionaryOptions.begin(), dictionaryOptions.end(), dictionaryName) == dictionaryOptions.end()) {
    dictionaryOptions.insert(dictionaryOptions.begin(), dictionaryName);
  }
  if (dictionaryOptions.size() > 1) {
    searchAllOptionIndex = static_cast<int>(dictionaryOptions.size());
    dictionaryOptions.emplace_back(I18N.get(StrId::STR_DICT_SEARCH_ALL));
  }
  dictionaryOptionsReady = true;
}

void DictionaryDefinitionActivity::openDictionaryMenu() {
  refreshDictionaryOptions();
  if (dictionaryOptions.size() <= 1) return;

  int currentIndex = 0;
  const auto current = std::find(dictionaryOptions.begin(), dictionaryOptions.end(), dictionaryName);
  if (current != dictionaryOptions.end()) currentIndex = static_cast<int>(current - dictionaryOptions.begin());
  dictionaryPopup.show(StrId::STR_DICTIONARY, dictionaryOptions, currentIndex, [this](int index) {
    if (index == searchAllOptionIndex) {
      searchAllPreparedDictionaries();
    } else if (index >= 0 && index < static_cast<int>(dictionaryOptions.size())) {
      loadFromDictionary(dictionaryOptions[index]);
    }
  });
  requestUpdate();
}

void DictionaryDefinitionActivity::showDictionaryMessage(const StrId message) {
  dictionaryMessage = message;
  dictionaryMessageTime = millis();
  showDictionaryError = true;
}

void DictionaryDefinitionActivity::loadFromDictionary(const std::string& name) {
  if (name.empty() || name == dictionaryName) return;

  dictionaryBusy = true;
  showDictionaryError = false;
  requestUpdateAndWait();

  Dictionary dictionary;
  std::string nextDefinition;
  std::string nextHeadword;
  const bool ok = dictionary.open(name.c_str()) && !dictionary.needsIndex() &&
                  dictionary.lookup(lookupWord.c_str(), nextDefinition, nextHeadword);
  if (!ok) {
    dictionaryBusy = false;
    showDictionaryMessage(StrId::STR_DICT_NOT_FOUND);
    requestUpdate();
    return;
  }

  std::replace(nextDefinition.begin(), nextDefinition.end(), '\0', '\n');
  nextDefinition = htmlToPlainText(nextDefinition);
  dictionaryName = name;
  showingAllResults = false;
  headword = std::move(nextHeadword);
  definition = std::move(nextDefinition);
  currentPage = 0;
  wrapText();
  dictionaryBusy = false;
  requestUpdate();
}

void DictionaryDefinitionActivity::searchAllPreparedDictionaries() {
  refreshDictionaryOptions();
  dictionaryBusy = true;
  showDictionaryError = false;
  requestUpdateAndWait();

  // The ready list is bounded by the dictionaries installed on the card, and
  // each lookup uses a qidx binary search. Build one bounded result directly
  // instead of retaining several full definition copies at once.
  std::string combined;
  combined.reserve(MAX_COMBINED_DICTIONARY_BYTES);
  size_t resultCount = 0;
  for (size_t i = 0; i < dictionaryOptions.size(); ++i) {
    if (static_cast<int>(i) == searchAllOptionIndex) continue;
    Dictionary dictionary;
    if (!dictionary.open(dictionaryOptions[i].c_str()) || dictionary.needsIndex()) continue;

    std::string resultDefinition;
    std::string resultHeadword;
    if (!dictionary.lookup(lookupWord.c_str(), resultDefinition, resultHeadword)) continue;
    if (resultCount >= MAX_ALL_DICTIONARY_RESULTS) break;
    if (!combined.empty()) combined += "\n\n";
    combined += "--- ";
    combined += dictionaryOptions[i];
    combined += " ---\n";
    const size_t remaining = MAX_COMBINED_DICTIONARY_BYTES > combined.size()
                                 ? MAX_COMBINED_DICTIONARY_BYTES - combined.size()
                                 : 0;
    if (remaining == 0) break;
    combined.append(resultDefinition, 0, std::min(remaining, resultDefinition.size()));
    resultCount++;
    if (combined.size() >= MAX_COMBINED_DICTIONARY_BYTES) break;
  }

  dictionaryBusy = false;
  if (resultCount == 0) {
    showDictionaryMessage(StrId::STR_DICT_NOT_FOUND);
    requestUpdate();
    return;
  }

  dictionaryName = ALL_DICTIONARIES_LABEL;
  showingAllResults = true;
  headword = lookupWord;
  definition = std::move(combined);
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  definition = htmlToPlainText(definition);
  currentPage = 0;
  searchAllOptionIndex = -1;
  dictionaryOptionsReady = false;
  wrapText();
  requestUpdate();
}

int DictionaryDefinitionActivity::measureSpan(const int fontId, const char* text, size_t len) const {
  char buf[MAX_LINE_BYTES + 1];
  len = std::min(len, MAX_LINE_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, EpdFontFamily::REGULAR);
}

// Greedy word-wrap of `definition` into byte spans. '\n' breaks lines (blank
// lines survive as paragraph spacing; NULs from multi-type StarDict entries
// were normalized to newlines in onEnter); '\r' is dropped by treating it as
// a space at a token edge.
void DictionaryDefinitionActivity::wrapText() {
  lines.clear();
  lines.reserve(definition.size() / 32 + 8);

  const int fontId = SETTINGS.getDictionaryFontId();
  // SD-card fonts: merge every definition codepoint into the persistent
  // advance table up front. Otherwise each unseen codepoint measured below
  // falls back to an on-demand glyph load from SD (8-slot overflow ring).
  renderer.ensureSdCardFontReady(fontId, definition.c_str(), 0x01 /* REGULAR */);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscape = orientation == GfxRenderer::Orientation::LandscapeClockwise ||
                           orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = isLandscape ? metrics.sideButtonHintsWidth : 0;
  const int maxWidth = renderer.getScreenWidth() - hintGutterWidth - 2 * SIDE_PADDING;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);

  const int lineHeight = renderer.getLineHeight(fontId);
  const int topArea = (isInverted ? metrics.buttonHintsHeight : 0) + metrics.topPadding + metrics.headerHeight;
  const int bottomArea = metrics.buttonHintsHeight + metrics.verticalSpacing;
  linesPerPage = std::max(1, (renderer.getScreenHeight() - topArea - bottomArea) / lineHeight);

  const char* text = definition.c_str();
  const uint32_t n = static_cast<uint32_t>(definition.size());
  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;  // one past the last token byte on the current line
  int lineWidth = 0;

  const auto flushLine = [&](uint32_t nextStart) {
    lines.push_back({lineStart, static_cast<uint16_t>(lineEnd - lineStart)});
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
  };

  uint32_t i = 0;
  while (i < n) {
    const char c = text[i];
    if (c == '\n' || c == '\0') {
      flushLine(i + 1);
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }

    // Token: run of non-whitespace bytes, capped at the measure buffer.
    const uint32_t tokenStart = i;
    while (i < n && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '\0' &&
           i - tokenStart < MAX_LINE_BYTES) {
      i++;
    }
    // If the byte cap cut the token mid-UTF-8-sequence, back off to the last
    // complete codepoint so measure/draw never see a partial sequence. A
    // natural stop lands on whitespace or the terminating NUL, never on a
    // continuation byte, so this is a no-op there.
    while (i - tokenStart > 1 && (text[i] & 0xC0) == 0x80) i--;
    const uint32_t tokenLen = i - tokenStart;
    const int tokenWidth = measureSpan(fontId, text + tokenStart, tokenLen);

    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    } else if (lineWidth + spaceWidth + tokenWidth <= maxWidth &&
               tokenStart + tokenLen - lineStart <= UINT16_MAX) {  // span len must fit Line::len
      lineEnd = tokenStart + tokenLen;
      lineWidth += spaceWidth + tokenWidth;
    } else {
      flushLine(tokenStart);
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    }

    // An unbreakable token wider than the screen is now alone on the line
    // (any previous content was flushed above): split it at the widest
    // fitting UTF-8 boundary and carry the remainder forward.
    while (lineWidth > maxWidth && lineEnd - lineStart > 1) {
      const uint32_t len = lineEnd - lineStart;
      uint32_t lastFit = 0;
      for (uint32_t f = 1; f <= len; f++) {
        if (f == len || (text[lineStart + f] & 0xC0) != 0x80) {  // codepoint boundary
          if (measureSpan(fontId, text + lineStart, f) > maxWidth) break;
          lastFit = f;
        }
      }
      if (lastFit == 0) {
        // Even a single over-wide glyph must make progress; consume its whole
        // UTF-8 sequence rather than splitting it into invalid fragments.
        lastFit = 1;
        while (lastFit < len && (text[lineStart + lastFit] & 0xC0) == 0x80) lastFit++;
      }
      const uint32_t rest = lineStart + lastFit;
      lineEnd = rest;
      flushLine(rest);
      lineEnd = rest + (len - lastFit);
      lineWidth = measureSpan(fontId, text + lineStart, lineEnd - lineStart);
    }
  }
  if (lineEnd > lineStart) flushLine(n);

  // Trim trailing blank lines so the last page is not empty padding.
  while (!lines.empty() && lines.back().len == 0) lines.pop_back();

  totalPages = std::max(1, (static_cast<int>(lines.size()) + linesPerPage - 1) / linesPerPage);
  currentPage = 0;
}

void DictionaryDefinitionActivity::loop() {
  if (dictionaryBusy) return;

  // OptionPopup closes on the Back press edge. Consume the matching release
  // so it cannot immediately bubble into this activity and exit the
  // definition page as well.
  if (suppressBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) suppressBackRelease = false;
    return;
  }

  if (showDictionaryError && millis() - dictionaryMessageTime >= DICTIONARY_MESSAGE_DURATION_MS) {
    showDictionaryError = false;
    requestUpdate();
  }

  if (dictionaryPopup.isActive()) {
    const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
    dictionaryPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    if (backPressed && !dictionaryPopup.isActive()) suppressBackRelease = true;
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openDictionaryMenu();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Same tap zones as the reader page turns: left third = previous page,
  // the rest = next. Back is the usual left-edge swipe.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (tx < renderer.getScreenWidth() / 3) {
      if (currentPage > 0) {
        currentPage--;
        requestUpdate();
      }
    } else if (currentPage + 1 < totalPages) {
      currentPage++;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNext([this] {
    if (currentPage + 1 < totalPages) {
      currentPage++;
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (currentPage > 0) {
      currentPage--;
      requestUpdate();
    }
  });
}

// Draws the current page's line spans (copied into a stack buffer for NUL
// termination). Called twice per render: once in font-cache scan mode, once
// for the real paint.
void DictionaryDefinitionActivity::drawBody(const int fontId, const int x, const int startY) const {
  const int lineHeight = renderer.getLineHeight(fontId);
  char buf[MAX_LINE_BYTES + 1];
  const int firstLine = currentPage * linesPerPage;
  const int lastLine = std::min(firstLine + linesPerPage, static_cast<int>(lines.size()));
  for (int i = firstLine; i < lastLine; i++) {
    if (lines[i].len == 0) continue;
    const size_t len = std::min(static_cast<size_t>(lines[i].len), MAX_LINE_BYTES);
    memcpy(buf, definition.c_str() + lines[i].start, len);
    buf[len] = '\0';
    renderer.drawText(fontId, x, startY + (i - firstLine) * lineHeight, buf);
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = renderer.getScreenWidth() - hintGutterWidth;
  const int contentY = isInverted ? metrics.buttonHintsHeight : 0;

  // Header: matched headword and source dictionary left, page counter right.
  const int headerY = contentY + metrics.topPadding + 10;
  renderer.drawText(UI_12_FONT_ID, contentX + SIDE_PADDING, headerY, headword.c_str(), true, EpdFontFamily::BOLD);
  if (!dictionaryName.empty()) {
    const int dictionaryMaxWidth = std::max(0, contentWidth - 2 * SIDE_PADDING - 70);
    const std::string displayName = renderer.truncatedText(UI_10_FONT_ID, dictionaryName.c_str(), dictionaryMaxWidth);
    renderer.drawText(UI_10_FONT_ID, contentX + SIDE_PADDING, headerY + renderer.getLineHeight(UI_10_FONT_ID),
                      displayName.c_str());
  }
  if (totalPages > 1) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", currentPage + 1, totalPages);
    const int counterWidth = renderer.getTextWidth(UI_10_FONT_ID, counter);
    renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - SIDE_PADDING - counterWidth, headerY, counter);
  }

  // Body: two-pass draw inside a prewarm scope (same pattern as the reader's
  // renderContents) so SD-card font glyphs load from SD in one batch instead
  // of one on-demand overflow read per character on every page turn.
  const int fontId = SETTINGS.getDictionaryFontId();
  const int bodyStartY = contentY + metrics.topPadding + metrics.headerHeight;
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawBody(fontId, contentX + SIDE_PADDING, bodyStartY);  // scan pass: records codepoints only
  scope.endScanAndPrewarm();
  drawBody(fontId, contentX + SIDE_PADDING, bodyStartY);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DICTIONARY),
                                            (currentPage > 0 ? "<" : ""),
                                            (currentPage + 1 < totalPages ? ">" : ""));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (dictionaryPopup.isActive()) {
    dictionaryPopup.processRender(renderer, mappedInput);
    return;
  }
  if (dictionaryBusy) {
    GUI.drawPopup(renderer, tr(STR_DICT_LOOKING_UP));
    return;
  }
  if (showDictionaryError) {
    GUI.drawPopup(renderer, I18N.get(dictionaryMessage));
    return;
  }
  renderer.displayBuffer();
}
