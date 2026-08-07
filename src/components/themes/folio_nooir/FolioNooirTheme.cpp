#include "FolioNooirTheme.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "fontIds.h"

namespace {
void drawSummaryIcon(const GfxRenderer& renderer, const int x, const int y, const int icon) {
  switch (icon) {
    case 0:
      renderer.drawLine(x + 6, y + 2, x + 1, y + 1, true);
      renderer.drawLine(x + 1, y + 1, x + 1, y + 11, true);
      renderer.drawLine(x + 1, y + 11, x + 6, y + 10, true);
      renderer.drawLine(x + 6, y + 2, x + 6, y + 11, true);
      renderer.drawLine(x + 6, y + 2, x + 11, y + 1, true);
      renderer.drawLine(x + 11, y + 1, x + 11, y + 11, true);
      renderer.drawLine(x + 11, y + 11, x + 6, y + 10, true);
      break;
    case 1:
      renderer.drawRect(x + 1, y + 2, 11, 9, true);
      renderer.drawLine(x + 3, y + 1, x + 10, y + 1, true);
      renderer.drawLine(x + 6, y + 2, x + 6, y + 11, true);
      break;
    case 2:
      renderer.drawLine(x + 3, y + 2, x + 3, y + 11, 2, true);
      renderer.drawLine(x + 9, y + 2, x + 9, y + 11, 2, true);
      break;
    default:
      renderer.drawLine(x + 1, y + 7, x + 5, y + 11, 2, true);
      renderer.drawLine(x + 5, y + 11, x + 12, y + 2, 2, true);
      break;
  }
}
}  // namespace

FolioShelfLayout FolioNooirTheme::shelfLayout(const GfxRenderer& renderer, const ThemeMetrics& metrics) const {
  FolioShelfLayout out{};
  out.headerTop = metrics.topPadding + 8;
  out.headerHeight = 52;
  out.contentTop = out.headerTop + out.headerHeight + 2;
  out.contentHeight = renderer.getScreenHeight() - out.contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  out.detailHeight = std::min(238, std::max(150, out.contentHeight * 38 / 100));
  // Keep the statistics strip close to the button hints so the page label has
  // a dedicated gap below the cover grid instead of sharing its last row.
  out.statsHeight = std::min(52, std::max(40, out.contentHeight / 10 - 10));
  out.statsTop = out.contentTop + out.contentHeight - out.statsHeight;
  out.gridTop = out.contentTop + out.detailHeight;
  out.gridHeight = out.statsTop - out.gridTop;
  out.gridGap = 6;
  out.columns = 4;
  out.cardWidth = (renderer.getScreenWidth() - out.gridGap * (out.columns + 1)) / out.columns;
  out.cardHeight = out.gridHeight / 2;
  return out;
}

void FolioNooirTheme::drawShelfTabs(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                                    const uint8_t activeTab) const {
  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, layout.headerTop - 8, pageWidth, layout.headerHeight + 10, false);
  const char* tabs[] = {tr(STR_LIBRARY), tr(STR_RECENT), tr(STR_FINISHED)};
  for (int i = 0; i < 3; ++i) {
    const int left = pageWidth * i / 3;
    const int right = pageWidth * (i + 1) / 3;
    const int width = renderer.getTextWidth(UI_10_FONT_ID, tabs[i]);
    renderer.drawText(UI_10_FONT_ID, left + (right - left - width) / 2, layout.headerTop + 8, tabs[i], true,
                      i == activeTab ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (i == activeTab) renderer.fillRect(left + 12, layout.headerTop + layout.headerHeight - 3, right - left - 24, 3);
  }
  renderer.drawLine(0, layout.headerTop + layout.headerHeight, pageWidth - 1,
                    layout.headerTop + layout.headerHeight);
}

void FolioNooirTheme::drawShelfStats(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                                     const uint32_t lastMinutes, const uint32_t middleMinutes,
                                     const uint16_t finishedCount, const bool accumulated) const {
  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, layout.statsTop, pageWidth, layout.statsHeight, false);
  renderer.drawLine(0, layout.statsTop, pageWidth - 1, layout.statsTop);
  const char* labels[] = {tr(STR_LAST_READ), accumulated ? tr(STR_ACCUMULATED) : tr(STR_TODAY), tr(STR_FINISHED)};
  char values[3][24];
  snprintf(values[0], sizeof(values[0]), "%lu %s", static_cast<unsigned long>(lastMinutes), tr(STR_MIN_SHORT));
  snprintf(values[1], sizeof(values[1]), "%lu %s", static_cast<unsigned long>(middleMinutes), tr(STR_MIN_SHORT));
  snprintf(values[2], sizeof(values[2]), "%u", finishedCount);
  for (int i = 0; i < 3; ++i) {
    const int left = pageWidth * i / 3;
    const int right = pageWidth * (i + 1) / 3;
    if (i > 0) renderer.drawLine(left, layout.statsTop + 7, left, layout.statsTop + layout.statsHeight - 7);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, values[i]);
    renderer.drawText(SMALL_FONT_ID, left + (right - left - labelWidth) / 2, layout.statsTop + 8, labels[i]);
    renderer.drawText(UI_10_FONT_ID, left + (right - left - valueWidth) / 2, layout.statsTop + 29, values[i], true);
  }
}

void FolioNooirTheme::drawCoverProgress(const GfxRenderer& renderer, const int x, const int y, const int width,
                                        const uint8_t percent) const {
  char progress[16];
  snprintf(progress, sizeof(progress), "%u%%", percent);
  const int progressWidth = renderer.getTextWidth(SMALL_FONT_ID, progress);
  renderer.drawText(SMALL_FONT_ID, x + (width - progressWidth) / 2, y, progress);
}

void FolioNooirTheme::drawLibrarySummary(const GfxRenderer& renderer, const int x, const int y, const int width,
                                         const FolioLibrarySummary& summary) const {
  // Two compact rows occupy 30 px inside the existing featured-book region;
  // the grid and statistics strip retain their original positions.
  drawSummaryIcon(renderer, x + 1, y + 2, 1);
  const char* title = tr(STR_MY_LIBRARY);
  int cursor = x + 18;
  renderer.drawText(SMALL_FONT_ID, cursor, y + 3, title);
  cursor += renderer.getTextWidth(SMALL_FONT_ID, title) + 5;
  renderer.drawText(SMALL_FONT_ID, cursor, y + 3, "·");
  cursor += renderer.getTextWidth(SMALL_FONT_ID, "·") + 5;
  char total[16];
  snprintf(total, sizeof(total), "%lu", static_cast<unsigned long>(summary.total));
  renderer.drawText(UI_10_FONT_ID, cursor, y + 2, total, true, EpdFontFamily::BOLD);

  const uint32_t values[] = {summary.reading, summary.unread, summary.onHold, summary.finished};
  const int rowY = y + 16;
  for (int i = 0; i < 4; ++i) {
    const int left = x + width * i / 4;
    drawSummaryIcon(renderer, left + 2, rowY, i);
    char value[16];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(values[i]));
    renderer.drawText(UI_10_FONT_ID, left + 19, rowY + 1, value, true, EpdFontFamily::BOLD);
  }
  renderer.drawLine(x, y + 29, x + width - 1, y + 29);
}

void FolioNooirTheme::drawCoverProgressBadge(const GfxRenderer& renderer, const int x, const int y, const int width,
                                             const int height, const uint8_t percent) const {
  char progress[8];
  snprintf(progress, sizeof(progress), "%u%%", percent);
  constexpr int ribbonHeight = 22;
  constexpr int ribbonLift = 7;
  const int ribbonWidth = std::min(42, std::max(28, renderer.getTextWidth(SMALL_FONT_ID, progress) + 12));
  const int ribbonX = x + 4;
  const int ribbonY = y + height - ribbonHeight - ribbonLift;
  renderer.fillRect(ribbonX, ribbonY, ribbonWidth, ribbonHeight, true);

  // Cut a small white V from the bottom so the marker reads as a physical
  // bookmark ribbon instead of a floating rectangular badge.
  const int notchCenter = ribbonX + ribbonWidth / 2;
  const int notchDepth = 6;
  const int notchHalfWidth = 5;
  const int notchX[] = {notchCenter - notchHalfWidth, notchCenter + notchHalfWidth, notchCenter};
  const int notchY[] = {ribbonY + ribbonHeight - notchDepth, ribbonY + ribbonHeight - notchDepth,
                        ribbonY + ribbonHeight};
  renderer.fillPolygon(notchX, notchY, 3, false);
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, progress);
  renderer.drawText(SMALL_FONT_ID, ribbonX + (ribbonWidth - textWidth) / 2, ribbonY + 4, progress, false);
}

void FolioNooirTheme::drawPageIndicator(const GfxRenderer& renderer, const FolioShelfLayout& layout,
                                        const size_t page, const size_t pages) const {
  if (pages <= 1) return;
  char text[32];
  snprintf(text, sizeof(text), "< %s %u/%u >", tr(STR_PAGE), static_cast<unsigned>(page),
           static_cast<unsigned>(pages));
  const int width = renderer.getTextWidth(SMALL_FONT_ID, text) + 12;
  const int x = (renderer.getScreenWidth() - width) / 2;
  renderer.fillRect(x, layout.statsTop - 21, width, 18, false);
  renderer.drawText(SMALL_FONT_ID, x + 6, layout.statsTop - 19, text);
}
