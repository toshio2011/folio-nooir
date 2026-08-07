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

std::string SynopsisActivity::stripHtml(const std::string& source) {
  std::string result;
  result.reserve(source.size());
  bool inTag = false;
  for (size_t i = 0; i < source.size(); ++i) {
    const char c = source[i];
    if (c == '<') {
      inTag = true;
      continue;
    }
    if (inTag) {
      if (c == '>') {
        inTag = false;
        if (!result.empty() && result.back() != ' ') result.push_back(' ');
      }
      continue;
    }
    if (c == '\r' || c == '\n' || c == '\t') {
      if (!result.empty() && result.back() != ' ') result.push_back(' ');
    } else {
      result.push_back(c);
    }
  }
  // Keep the page compact when HTML contains indentation or repeated spaces.
  std::string compact;
  compact.reserve(result.size());
  bool previousSpace = false;
  for (const char c : result) {
    const bool space = c == ' ';
    if (space && previousSpace) continue;
    compact.push_back(c);
    previousSpace = space;
  }
  while (!compact.empty() && compact.front() == ' ') compact.erase(compact.begin());
  while (!compact.empty() && compact.back() == ' ') compact.pop_back();
  return compact;
}

void SynopsisActivity::onEnter() {
  Activity::onEnter();
  // Shelf entries intentionally keep a small synopsis cache for boot and
  // scrolling speed. If it looks like that cache was truncated, retrieve the
  // original EPUB description only when the user explicitly asks to read it.
  if (FsHelpers::hasEpubExtension(bookPath) && (synopsis.empty() || synopsis.size() >= 384)) {
    Epub epub(bookPath, "/.crosspoint");
    if (epub.loadMetadataOnly() && epub.getDescription().size() > synopsis.size()) {
      synopsis = epub.getDescription();
    }
  }
  synopsis = stripHtml(synopsis);
  if (synopsis.empty()) synopsis = tr(STR_NO_SYNOPSIS);
  // The shelf cache is bounded, but this view must not silently truncate a
  // longer description. wrappedText only stores the lines that actually fit.
  const size_t maxLineCount = std::min(synopsis.size() + 1, static_cast<size_t>(std::numeric_limits<int>::max()));
  lines = renderer.wrappedText(SMALL_FONT_ID, synopsis.c_str(), renderer.getScreenWidth() - 24,
                               static_cast<int>(maxLineCount));
  firstLine = 0;
  requestUpdate();
}

void SynopsisActivity::movePage(const int direction) {
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
