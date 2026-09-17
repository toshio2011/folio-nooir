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
constexpr unsigned long DICTIONARY_MESSAGE_DURATION_MS = 1500;

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
  suppressBackRelease = false;
  showDictionaryError = false;
  if (matchingSources.empty() && !dictionaryName.empty()) {
    matchingSources.add(dictionaryName, headword, dictionaryName == preferredDictionaryName);
  }
  wrapText();
  requestUpdate();
}

void DictionaryDefinitionActivity::onExit() {
  Activity::onExit();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
}

bool DictionaryDefinitionActivity::openDictionary(Dictionary& target, const std::string& name) {
  if (name.empty()) return false;

  // Keep the first detailed diagnostic for a broken folder, but make later
  // validation passes silent for the same folder during this activity.
  const bool logErrors = !dictionaryOpenFailures.contains(name) && dictionaryOpenFailures.canTrack();
  const bool opened = target.open(name.c_str(), logErrors);
  if (!opened) dictionaryOpenFailures.remember(name);
  return opened;
}

void DictionaryDefinitionActivity::ensureDictionaryEntries() {
  if (dictionaryEntriesReady) return;
  DictionaryRegistry::discover(dictionaryEntries);
  dictionaryEntryCursor = 0;
  dictionaryEntriesReady = true;
}

bool DictionaryDefinitionActivity::discoverNextMatchingSource() {
  if (matchingSources.size() >= DictionarySourceMatches::MAX_MATCHES) {
    dictionaryOptionsReady = true;
    return false;
  }

  ensureDictionaryEntries();
  while (dictionaryEntryCursor < dictionaryEntries.size()) {
    const DictionaryEntry& entry = dictionaryEntries[dictionaryEntryCursor++];
    if (matchingSources.contains(entry.name)) continue;

    Dictionary dictionary;
    // Do not build qidx sidecars from the result screen. Dictionary::locate()
    // already provides a bounded indexed path and a correct sequential scan
    // fallback for unindexed dictionaries; the latter is used only on demand.
    if (!openDictionary(dictionary, entry.name)) continue;

    std::string resultHeadword;
    if (!dictionary.hasEntry(lookupWord.c_str(), resultHeadword)) continue;
    if (!matchingSources.add(entry.name, resultHeadword, entry.name == preferredDictionaryName)) continue;
    dictionaryOptionsReady = matchingSources.size() >= DictionarySourceMatches::MAX_MATCHES;
    return true;
  }

  dictionaryOptionsReady = true;
  return false;
}

void DictionaryDefinitionActivity::refreshDictionaryOptions() {
  if (!dictionaryOptionsReady) {
    while (matchingSources.size() < DictionarySourceMatches::MAX_MATCHES && discoverNextMatchingSource()) {
    }
  }

  dictionaryOptions.clear();
  dictionaryOptions.reserve(matchingSources.size());
  for (size_t i = 0; i < matchingSources.size(); i++) dictionaryOptions.push_back(matchingSources[i].name);

  dictionaryOptionsReady = true;
}

void DictionaryDefinitionActivity::openDictionaryMenu() {
  if (!dictionaryOptionsReady) {
    dictionaryBusy = true;
    showDictionaryError = false;
    requestUpdateAndWait();
    refreshDictionaryOptions();
    dictionaryBusy = false;
    requestUpdate();
  }
  refreshDictionaryOptions();
  if (dictionaryOptions.size() <= 1) return;

  int currentIndex = 0;
  const auto current = std::find(dictionaryOptions.begin(), dictionaryOptions.end(), dictionaryName);
  if (current != dictionaryOptions.end()) currentIndex = static_cast<int>(current - dictionaryOptions.begin());
  dictionaryPopup.show(StrId::STR_DICTIONARY, dictionaryOptions, currentIndex, [this](int index) {
    if (index >= 0 && index < static_cast<int>(dictionaryOptions.size())) {
      loadFromDictionary(dictionaryOptions[index]);
    }
  });
  requestUpdate();
}

void DictionaryDefinitionActivity::navigateSource(const int direction) {
  if (dictionaryBusy || matchingSources.empty() || direction == 0) return;

  const size_t current = currentSourceIndex();
  const bool needsMore = (direction > 0 && current + 1 >= matchingSources.size()) ||
                         (direction < 0 && current == 0);
  if (needsMore && !dictionaryOptionsReady) {
    dictionaryBusy = true;
    requestUpdateAndWait();
    if (direction > 0) {
      discoverNextMatchingSource();
    } else {
      // Previous from the first known source wraps to the last matching source,
      // so finish the bounded scan before choosing that endpoint.
      refreshDictionaryOptions();
    }
    dictionaryBusy = false;
  }

  if (matchingSources.size() <= 1) {
    requestUpdate();
    return;
  }

  const int sourceCount = static_cast<int>(matchingSources.size());
  const int currentIndex = static_cast<int>(currentSourceIndex());
  const int nextIndex = direction > 0 ? ButtonNavigator::nextIndex(currentIndex, sourceCount)
                                     : ButtonNavigator::previousIndex(currentIndex, sourceCount);
  loadFromDictionary(matchingSources[static_cast<size_t>(nextIndex)].name);
}

void DictionaryDefinitionActivity::navigateDefinitionPage(const int direction) {
  if (direction > 0 && currentPage + 1 < totalPages) {
    currentPage++;
    requestUpdate();
  } else if (direction < 0 && currentPage > 0) {
    currentPage--;
    requestUpdate();
  }
}

size_t DictionaryDefinitionActivity::currentSourceIndex() const {
  for (size_t i = 0; i < matchingSources.size(); i++) {
    if (matchingSources[i].name == dictionaryName) return i;
  }
  return 0;
}

bool DictionaryDefinitionActivity::currentSourceIsPreferred() const {
  for (size_t i = 0; i < matchingSources.size(); i++) {
    if (matchingSources[i].name == dictionaryName) return matchingSources[i].preferred;
  }
  return dictionaryName == preferredDictionaryName;
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
  if (!openDictionary(dictionary, name)) {
    dictionaryBusy = false;
    showDictionaryMessage(StrId::STR_DICT_NOT_FOUND);
    requestUpdate();
    return;
  }

  // Release the previous body before reading the selected source. Alternate
  // source records contain metadata only, so this keeps one full definition
  // body resident even when switching repeatedly on a tight heap.
  std::string().swap(definition);
  lines.clear();
  lines.shrink_to_fit();
  std::string nextHeadword;
  if (!dictionary.lookup(lookupWord.c_str(), definition, nextHeadword)) {
    dictionaryBusy = false;
    showDictionaryMessage(StrId::STR_DICT_NOT_FOUND);
    requestUpdate();
    return;
  }

  std::replace(definition.begin(), definition.end(), '\0', '\n');
  definition = htmlToPlainText(definition);
  dictionaryName = dictionary.folderName().empty() ? name : dictionary.folderName();
  headword = std::move(nextHeadword);
  matchingSources.add(dictionaryName, headword, dictionaryName == preferredDictionaryName);
  currentPage = 0;
  wrapText();
  dictionaryBusy = false;
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

  const bool sourceNavigationVisible = matchingSources.size() > 1 || !dictionaryOptionsReady;
  const bool directSourceNavigation = sourceNavigationVisible &&
                                      SETTINGS.sideButtonLayout != CrossPointSettings::SIDE_BUTTONS_DISABLED;
  if (directSourceNavigation && mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    navigateSource(mappedInput.isNavDirectionSwapped() ? 1 : -1);
    return;
  }
  if (directSourceNavigation && mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    navigateSource(mappedInput.isNavDirectionSwapped() ? -1 : 1);
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

  if (directSourceNavigation) {
    // Front left/right are reserved for matching-source navigation. Definition
    // pages keep their physical navigation on the dedicated side buttons and
    // their existing touch zones, so the two concepts cannot be confused.
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::PageForward}, [this] {
      navigateDefinitionPage(1);
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::PageBack}, [this] {
      navigateDefinitionPage(-1);
    });
  } else {
    // If side buttons are disabled, retain the original front-button page
    // controls. Source switching remains available through the Sources picker.
    buttonNavigator.onNext([this] {
      navigateDefinitionPage(1);
    });
    buttonNavigator.onPrevious([this] {
      navigateDefinitionPage(-1);
    });
  }
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

  // Header: matched headword and source dictionary left, page/source counters
  // right. Only source metadata is retained for alternate results; the body
  // below always belongs to the currently selected source.
  const int headerY = contentY + metrics.topPadding;
  renderer.drawText(UI_12_FONT_ID, contentX + SIDE_PADDING, headerY, headword.c_str(), true, EpdFontFamily::BOLD);
  if (!dictionaryName.empty()) {
    const int headwordHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int sourceFontHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int sourceY = headerY + headwordHeight + 1;
    const int statusY = sourceY + sourceFontHeight;
    const int dictionaryMaxWidth = std::max(0, contentWidth - 2 * SIDE_PADDING);
    const std::string displayName = renderer.truncatedText(UI_10_FONT_ID, dictionaryName.c_str(), dictionaryMaxWidth);
    renderer.drawText(UI_10_FONT_ID, contentX + SIDE_PADDING, sourceY, displayName.c_str());

    std::string sourceStatus = currentSourceIsPreferred() ? "Preferred" : "Fallback";
    if (matchingSources.size() > 1) {
      sourceStatus += " · ";
      sourceStatus += std::to_string(currentSourceIndex() + 1);
      sourceStatus += " of ";
      sourceStatus += std::to_string(matchingSources.size());
    }
    renderer.drawText(UI_10_FONT_ID, contentX + SIDE_PADDING, statusY, sourceStatus.c_str());
  }
  if (totalPages > 1) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", currentPage + 1, totalPages);
    const int counterWidth = renderer.getTextWidth(UI_10_FONT_ID, counter);
    renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - SIDE_PADDING - counterWidth, headerY, counter);
  }
  const int bodyStartY = contentY + metrics.topPadding + metrics.headerHeight;
  renderer.drawLine(contentX + SIDE_PADDING, bodyStartY - 4, contentX + contentWidth - SIDE_PADDING - 1, bodyStartY - 4);

  // Body: two-pass draw inside a prewarm scope (same pattern as the reader's
  // renderContents) so SD-card font glyphs load from SD in one batch instead
  // of one on-demand overflow read per character on every page turn.
  const int fontId = SETTINGS.getDictionaryFontId();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  drawBody(fontId, contentX + SIDE_PADDING, bodyStartY);  // scan pass: records codepoints only
  scope.endScanAndPrewarm();
  drawBody(fontId, contentX + SIDE_PADDING, bodyStartY);

  const bool sourceNavigationVisible = matchingSources.size() > 1 || !dictionaryOptionsReady;
  const bool directSourceNavigation = sourceNavigationVisible &&
                                      SETTINGS.sideButtonLayout != CrossPointSettings::SIDE_BUTTONS_DISABLED;
  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK), sourceNavigationVisible ? "Sources" : "",
      directSourceNavigation ? "< Dict" : (currentPage > 0 ? "<" : ""),
      directSourceNavigation ? "Dict >" : (currentPage + 1 < totalPages ? ">" : ""));
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
