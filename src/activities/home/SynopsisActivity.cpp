#include "SynopsisActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <limits>

#include "BookStateStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookFormat.h"
#include "util/HtmlToPlainText.h"

namespace {

const char* statusText(const BookStatus status, const uint8_t progress) {
  if (progress >= 100 || status == BookStatus::Finished) return tr(STR_FINISHED);
  if (status == BookStatus::OnHold) return tr(STR_STATS_STATUS_ON_HOLD);
  if (progress > 0 || status == BookStatus::Reading) return tr(STR_STATS_STATUS_READING);
  return tr(STR_STATS_STATUS_NEW);
}

}  // namespace

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
  if (!bookPath.empty()) {
    // Callers provide the already-loaded Recent/Library metadata.  Book Info
    // deliberately does not scan the full Recent history just to hydrate one
    // selected book.
    if (const BookState* state = BOOK_STATES.find(bookPath)) {
      status = state->status;
      progressPercent = state->progressPercent;
      readingSeconds = state->readingSeconds;
      readingSessions = state->readingSessions;
    }
    if (progressPercent >= 100) {
      status = BookStatus::Finished;
      progressPercent = 100;
    } else if (status == BookStatus::New && progressPercent > 0) {
      status = BookStatus::Reading;
    }
  }
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

int SynopsisActivity::synopsisTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int oldTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 24;
  if (bookPath.empty()) return oldTop;

  const int titleY = metrics.topPadding + 18;
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int infoTop = titleY + titleHeight + authorHeight + 10;
  const int infoHeight = std::min(132, std::max(96, renderer.getScreenHeight() / 7));
  return infoTop + infoHeight + 18;
}

void SynopsisActivity::movePage(const int direction) {
  renderer.setUiScaleTextEnabled(true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = synopsisTop();
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

  if (!bookPath.empty()) {
    const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int infoTop = titleY + titleHeight + renderer.getLineHeight(UI_10_FONT_ID) + 10;
    const int infoHeight = std::min(132, std::max(96, renderer.getScreenHeight() / 7));
    const int coverWidth = std::min(104, std::max(76, width / 3));
    const int detailX = side + coverWidth + 14;
    const int detailWidth = std::max(1, width - coverWidth - 14);
    const std::string thumb = UITheme::getCoverThumbPath(coverBmpPath, 220);
    bool drewCover = false;
    HalFile file;
    if (!coverBmpPath.empty() && Storage.openFileForRead("SYNOPSIS", thumb, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        int drawWidth = bitmap.getWidth();
        int drawHeight = bitmap.getHeight();
        if (drawWidth > coverWidth || drawHeight > infoHeight) {
          if (static_cast<long long>(drawWidth) * infoHeight > static_cast<long long>(drawHeight) * coverWidth) {
            drawWidth = coverWidth;
            drawHeight = std::max(1, bitmap.getHeight() * coverWidth / bitmap.getWidth());
          } else {
            drawHeight = infoHeight;
            drawWidth = std::max(1, bitmap.getWidth() * infoHeight / bitmap.getHeight());
          }
        }
        renderer.drawBitmap(bitmap, side + (coverWidth - drawWidth) / 2,
                            infoTop + (infoHeight - drawHeight) / 2, drawWidth, drawHeight);
        drewCover = true;
      }
    }
    if (!drewCover) {
      renderer.drawRect(side, infoTop, coverWidth, infoHeight);
      const std::string fallback = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), coverWidth - 12);
      const int fallbackWidth = renderer.getTextWidth(UI_10_FONT_ID, fallback.c_str());
      renderer.drawText(UI_10_FONT_ID, side + std::max(4, (coverWidth - fallbackWidth) / 2),
                        infoTop + infoHeight / 2, fallback.c_str(), true);
    } else {
      renderer.drawRect(side, infoTop, coverWidth, infoHeight);
    }

    const char* format = BookFormat::labelForPath(bookPath.c_str());
    if (format != nullptr) renderer.drawText(SMALL_FONT_ID, detailX, infoTop + 12, format, true);
    renderer.drawText(SMALL_FONT_ID, detailX, infoTop + 12 + renderer.getLineHeight(SMALL_FONT_ID),
                      statusText(status, progressPercent));
    char progress[32];
    snprintf(progress, sizeof(progress), "%u%%", progressPercent);
    renderer.drawText(SMALL_FONT_ID, detailX, infoTop + 12 + renderer.getLineHeight(SMALL_FONT_ID) * 2, progress);
    char reading[64];
    snprintf(reading, sizeof(reading), "%lu min - %u sessions",
             static_cast<unsigned long>((readingSeconds + 30) / 60), readingSessions);
    const std::string readingText = renderer.truncatedText(SMALL_FONT_ID, reading, detailWidth);
    renderer.drawText(SMALL_FONT_ID, detailX, infoTop + 12 + renderer.getLineHeight(SMALL_FONT_ID) * 3,
                      readingText.c_str());
    const int progressY = infoTop + infoHeight - 14;
    renderer.drawRect(detailX, progressY, detailWidth, 10);
    const int fill = (detailWidth - 2) * progressPercent / 100;
    if (fill > 0) renderer.fillRect(detailX + 1, progressY + 1, fill, 8);
    renderer.drawLine(side, infoTop + infoHeight + 8, side + width, infoTop + infoHeight + 8);
  }

  const int top = synopsisTop();
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
