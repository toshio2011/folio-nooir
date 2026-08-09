#include "SynopsisActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <limits>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HtmlToPlainText.h"

void SynopsisActivity::buildLines() {
  renderer.setUiScaleTextEnabled(true);
  lines.clear();
  const std::string plainText = htmlToPlainText(synopsis);
  const int width = renderer.getScreenWidth() - 24;
  size_t start = 0;

  // Keep block-level HTML structure (paragraphs, headings, list items and
  // <br>) while wrapping each block with the normal e-ink font metrics. The
  // synopsis remains lightweight; this is not a full browser/CSS engine.
  while (start <= plainText.size()) {
    const size_t end = plainText.find('\n', start);
    const std::string paragraph = plainText.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (paragraph.empty()) {
      if (!lines.empty()) lines.emplace_back();
    } else {
      const size_t maxLineCount = std::min(paragraph.size() + 1, static_cast<size_t>(std::numeric_limits<int>::max()));
      const auto wrapped = renderer.wrappedText(SMALL_FONT_ID, paragraph.c_str(), width,
                                                 static_cast<int>(maxLineCount));
      lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }

  if (lines.empty()) lines.emplace_back(tr(STR_NO_SYNOPSIS));
}

void SynopsisActivity::onEnter() {
  Activity::onEnter();
  // Shelf entries intentionally keep a small synopsis cache for boot and
  // scrolling speed. This activity is the explicit full-text view, so always
  // reload the EPUB metadata and replace the preview when a complete OPF
  // description is available. This also handles short previews that were
  // truncated at a paragraph boundary.
  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    if (epub.loadMetadataOnly() && !epub.getDescription().empty()) {
      synopsis = epub.getDescription();
    }
  }
  // The shelf cache is bounded, but this view must not silently truncate a
  // longer description. Build all wrapped lines after converting the HTML
  // fragment while preserving paragraph and list structure.
  buildLines();
  firstLine = 0;
  requestUpdate();
}

void SynopsisActivity::movePage(const int direction) {
  renderer.setUiScaleTextEnabled(true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 24;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const size_t pageLines = static_cast<size_t>(std::max(1, (bottom - top) / renderer.getLineHeight(SMALL_FONT_ID)));
  const size_t maxStart = lines.size() > pageLines ? lines.size() - pageLines : 0;
  if (direction > 0) {
    firstLine = std::min(maxStart, firstLine + pageLines);
  } else if (firstLine > pageLines) {
    firstLine -= pageLines;
  } else {
    firstLine = 0;
  }
  requestUpdate();
}

void SynopsisActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    movePage(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    movePage(-1);
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    movePage(1);
  } else if (swipe == MappedInputManager::SwipeDir::Down) {
    movePage(-1);
  }
}

void SynopsisActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  const int titleY = metrics.topPadding + 18;
  renderer.drawText(UI_12_FONT_ID, side, titleY,
                    renderer.truncatedText(UI_12_FONT_ID, title.c_str(), width).c_str(), true,
                    EpdFontFamily::BOLD);
  if (!author.empty()) {
    renderer.drawText(UI_10_FONT_ID, side, titleY + renderer.getLineHeight(UI_12_FONT_ID),
                      renderer.truncatedText(UI_10_FONT_ID, author.c_str(), width).c_str());
  }

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 24;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const size_t pageLines = static_cast<size_t>(std::max(1, (bottom - top) / lineHeight));
  for (size_t row = 0; row < pageLines && firstLine + row < lines.size(); ++row) {
    renderer.drawText(SMALL_FONT_ID, side, top + static_cast<int>(row) * lineHeight, lines[firstLine + row].c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
