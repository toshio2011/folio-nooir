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
#include "BookMetadataOverridesStore.h"
#include "BookRatingPickerActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookRating.h"
#include "util/BookFormat.h"
#include "util/HtmlToPlainText.h"

namespace {

const char* statusText(const BookStatus status, const uint8_t progress) {
  if (progress >= 100 || status == BookStatus::Finished) return tr(STR_FINISHED);
  if (status == BookStatus::OnHold) return tr(STR_STATS_STATUS_ON_HOLD);
  if (progress > 0 || status == BookStatus::Reading) return tr(STR_STATS_STATUS_READING);
  return tr(STR_STATS_STATUS_NEW);
}

struct InfoRow {
  int top = -1;
  int height = 0;
};

struct InfoRows {
  int panelHeight = 0;
  int textRowHeight = 0;
  InfoRow format;
  InfoRow status;
  InfoRow progressDetails;
  InfoRow progressBar;
  InfoRow rating;
};

InfoRows makeInfoRows(const GfxRenderer& renderer, const bool showFormat) {
  InfoRows rows{};
  rows.textRowHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID)) + 2;
  int cursor = 6;
  if (showFormat) {
    rows.format = {cursor, rows.textRowHeight};
    cursor += rows.textRowHeight;
  }
  rows.status = {cursor, rows.textRowHeight};
  cursor += rows.textRowHeight;
  rows.progressDetails = {cursor, rows.textRowHeight};
  cursor += rows.textRowHeight + 4;
  rows.progressBar = {cursor, 8};
  cursor += rows.progressBar.height + 4;
  rows.rating = {cursor, 22};
  rows.panelHeight = std::max(96, cursor + rows.rating.height + 6);
  return rows;
}

int infoPanelTop(const GfxRenderer& renderer, const int titleY, const bool hasAuthor) {
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorHeight = hasAuthor ? renderer.getLineHeight(UI_10_FONT_ID) : 0;
  return titleY + titleHeight + authorHeight + (hasAuthor ? 8 : 6);
}

}  // namespace

void SynopsisActivity::buildLines() {
  renderer.setUiScaleTextEnabled(true);
  lines.clear();
  const std::string plainText = htmlToPlainText(synopsis);
  const int side = UITheme::getInstance().getMetrics().contentSidePadding;
  // Leave a small ink margin beyond the measured advance width. Some fallback
  // glyphs have a wider bitmap than their advance and can otherwise write past
  // the logical right edge on the 480-pixel portrait layout.
  const int width = std::max(1, renderer.getScreenWidth() - side * 2 - 12);
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
  ignoreInitialConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                         mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  ratingFocused = false;
  bool hasMetadataOverride = false;
  if (!bookPath.empty()) {
    // Callers provide the already-loaded Recent/Library metadata.  Book Info
    // deliberately does not scan the full Recent history just to hydrate one
    // selected book.
    if (const BookState* state = BOOK_STATES.find(bookPath)) {
      status = state->status;
      progressPercent = std::max(progressPercent, state->progressPercent);
      readingSeconds = std::max(readingSeconds, state->readingSeconds);
      readingSessions = std::max(readingSessions, state->readingSessions);
    }
    if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(bookPath)) {
      if (!overrideData->title.empty()) title = overrideData->title;
      author = overrideData->author;
      synopsis = overrideData->synopsis;
      hasMetadataOverride = true;
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
  if (!hasMetadataOverride && FsHelpers::hasEpubExtension(bookPath)) {
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
  const bool showFormat = BookFormat::labelForPath(bookPath.c_str()) != nullptr;
  const InfoRows rows = makeInfoRows(renderer, showFormat);
  const int infoTop = infoPanelTop(renderer, titleY, !author.empty());
  return infoTop + rows.panelHeight + 12;
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
  if (ignoreInitialConfirm) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ignoreInitialConfirm = false;
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!bookPath.empty() && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ratingFocused = true;
    requestUpdate();
    return;
  }
  if (!bookPath.empty() && mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ratingFocused = false;
    movePage(1);
    return;
  }
  if (!bookPath.empty() && ratingFocused && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const BookState* state = BOOK_STATES.find(bookPath);
    const uint8_t rating = state == nullptr ? 0 : state->rating;
    startActivityForResult(std::make_unique<BookRatingPickerActivity>(renderer, mappedInput, bookPath, rating),
                           [this](const ActivityResult&) { requestUpdate(); });
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
  // Match the width measurement to the bold draw style and retain ink margin
  // so fallback glyphs cannot cross the logical framebuffer edge.
  const int safeTitleWidth = std::max(1, width - 12);
  renderer.drawText(UI_12_FONT_ID, side, titleY,
                    renderer.truncatedText(UI_12_FONT_ID, title.c_str(), safeTitleWidth, EpdFontFamily::BOLD).c_str(),
                    true, EpdFontFamily::BOLD);
  if (!author.empty()) {
    renderer.drawText(UI_10_FONT_ID, side, titleY + renderer.getLineHeight(UI_12_FONT_ID),
                      renderer.truncatedText(UI_10_FONT_ID, author.c_str(), safeTitleWidth).c_str());
  }

  if (!bookPath.empty()) {
    const char* format = BookFormat::labelForPath(bookPath.c_str());
    const InfoRows rows = makeInfoRows(renderer, format != nullptr);
    const int infoTop = infoPanelTop(renderer, titleY, !author.empty());
    const int infoHeight = rows.panelHeight;
    const int coverWidth = std::min(104, std::max(54, width / 4));
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

    const auto drawRowText = [this, detailX, detailWidth, infoTop](const InfoRow& row, const char* text,
                                                                   const bool bold) {
      if (row.top < 0 || row.height <= 0 || !text || !text[0]) return;
      const int baseline = infoTop + row.top + (row.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
      const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, text, detailWidth,
                                                        bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, detailX, baseline, fitted.c_str(), bold);
    };
    if (format) drawRowText(rows.format, format, true);
    drawRowText(rows.status, statusText(status, progressPercent), false);
    char progressDetails[64]{};
    size_t progressDetailsLength = 0;
    const auto appendProgressDetail = [&progressDetails, &progressDetailsLength](const char* value) {
      if (!value || !value[0] || progressDetailsLength >= sizeof(progressDetails) - 1) return;
      const char* separator = progressDetailsLength == 0 ? "" : " · ";
      const size_t remaining = sizeof(progressDetails) - progressDetailsLength;
      const int written = snprintf(progressDetails + progressDetailsLength, remaining, "%s%s", separator, value);
      if (written > 0) {
        progressDetailsLength = std::min(static_cast<size_t>(written), remaining - 1) + progressDetailsLength;
      }
    };
    char progress[8];
    snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(std::min<uint8_t>(progressPercent, 100)));
    appendProgressDetail(progress);
    if (readingSeconds > 0) {
      char minutes[24];
      snprintf(minutes, sizeof(minutes), "%lu min",
               static_cast<unsigned long>((static_cast<uint64_t>(readingSeconds) + 30) / 60));
      appendProgressDetail(minutes);
    }
    if (readingSessions > 0) {
      char sessions[24];
      snprintf(sessions, sizeof(sessions), "%u sessions", static_cast<unsigned>(readingSessions));
      appendProgressDetail(sessions);
    }
    drawRowText(rows.progressDetails, progressDetails, false);
    const int progressY = infoTop + rows.progressBar.top;
    renderer.drawRect(detailX, progressY, detailWidth, rows.progressBar.height);
    const int fill = std::max(0, detailWidth - 2) * std::min<uint8_t>(progressPercent, 100) / 100;
    if (fill > 0) renderer.fillRect(detailX + 1, progressY + 1, fill, rows.progressBar.height - 2);
    const int ratingTop = infoTop + rows.rating.top;
    renderer.drawRect(detailX, ratingTop, detailWidth, rows.rating.height, ratingFocused ? 2 : 1, true);
    const int labelY = ratingTop + (rows.rating.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    const int starSize = std::clamp(rows.rating.height - 6, 5, 10);
    const int minimumStarWidth = BookRating::kMaximum * 5 + (BookRating::kMaximum - 1);
    const int labelMaxWidth = std::max(1, detailWidth - minimumStarWidth - 18);
    const std::string ratingLabel = renderer.truncatedText(SMALL_FONT_ID, tr(STR_BOOK_RATING), labelMaxWidth);
    renderer.drawText(SMALL_FONT_ID, detailX + 5, labelY, ratingLabel.c_str());
    const BookState* state = BOOK_STATES.find(bookPath);
    const uint8_t rating = state == nullptr ? 0 : state->rating;
    const int starsX = detailX + 5 + renderer.getTextWidth(SMALL_FONT_ID, ratingLabel.c_str()) + 6;
    const int starsRight = detailX + detailWidth - 4;
    BookRating::drawStarsInBounds(renderer, {starsX, ratingTop + 3, std::max(0, starsRight - starsX),
                                             rows.rating.height - 6},
                                  rating, starSize, std::max(1, starSize / 5));
    renderer.drawLine(side, infoTop + infoHeight + 6, side + width - 1, infoTop + infoHeight + 6);
  }

  const int top = synopsisTop();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const size_t pageLines = static_cast<size_t>(std::max(1, (bottom - top) / lineHeight));
  const int safeTextWidth = std::max(1, renderer.getScreenWidth() - side * 2 - 12);
  for (size_t row = 0; row < pageLines && firstLine + row < lines.size(); ++row) {
    const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, lines[firstLine + row].c_str(), safeTextWidth);
    renderer.drawText(SMALL_FONT_ID, side, top + static_cast<int>(row) * lineHeight, fitted.c_str());
  }

  const char* confirmLabel = !bookPath.empty() && ratingFocused ? tr(STR_SELECT) : "";
  const char* previousLabel = bookPath.empty() ? tr(STR_DIR_LEFT) : tr(STR_BOOK_RATING);
  const char* nextLabel = bookPath.empty() ? tr(STR_DIR_RIGHT) : tr(STR_NEXT_PAGE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, previousLabel, nextLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
