#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "fontIds.h"

// Shared, allocation-free drawing primitives for book covers. Callers pass
// the already-decoded/cached cover bounds; this helper never opens files or
// inspects bitmap pixels.
namespace BookCoverTreatment {

inline void drawFrame(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                      const bool selected = false, const bool featured = false) {
  if (width < 2 || height < 2) return;
  renderer.drawRect(x, y, width, height, selected ? 2 : 1, true);
  // A small binding and page edge use only straight strokes. Keep them away
  // from the artwork on compact 4x2 cards; the larger Featured slot gets a
  // little more depth.
  if (width >= 52 && height >= 78) {
    renderer.drawLine(x + width - 3, y + 3, x + width - 3, y + height - 3, true);
    renderer.drawLine(x + 3, y + height - 3, x + width - 3, y + height - 3, true);
  }
  if (featured && width >= 76 && height >= 112) {
    renderer.drawLine(x + 2, y + 2, x + 2, y + height - 3, true);
  }
}

inline void drawPerspectiveFrame(const GfxRenderer& renderer, const int x, const int y, const int width,
                                const int maxHeight, const int leftHeight, const int rightHeight,
                                const bool selected = false) {
  if (width < 2 || maxHeight < 2 || leftHeight < 2 || rightHeight < 2) return;
  const int leftTop = y + (maxHeight - leftHeight) / 2;
  const int rightTop = y + (maxHeight - rightHeight) / 2;
  const int leftBottom = leftTop + leftHeight - 1;
  const int rightBottom = rightTop + rightHeight - 1;
  const int strokes = selected ? 2 : 1;
  for (int inset = 0; inset < strokes; ++inset) {
    const int leftX = x + inset;
    const int rightX = x + width - 1 - inset;
    const int insetTop = inset == 0 ? 0 : 2;
    const int insetBottom = inset == 0 ? 0 : 2;
    renderer.drawLine(leftX, leftTop + insetTop, rightX, rightTop + insetTop);
    renderer.drawLine(rightX, rightTop + insetTop, rightX, rightBottom - insetBottom);
    renderer.drawLine(rightX, rightBottom - insetBottom, leftX, leftBottom - insetBottom);
    renderer.drawLine(leftX, leftBottom - insetBottom, leftX, leftTop + insetTop);
  }
  if (width >= 90 && std::min(leftHeight, rightHeight) >= 132) {
    const int leftPageX = x + width - 4;
    const int rightPageX = x + width - 4;
    const int pageTop = std::max(leftTop, rightTop) + 4;
    const int pageBottom = std::min(leftBottom, rightBottom) - 4;
    if (pageBottom > pageTop) renderer.drawLine(leftPageX, pageTop, rightPageX, pageBottom);
  }
}

inline void drawProgress(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                         const uint8_t rawPercent, const bool selected = false, const bool featured = false) {
  if (SETTINGS.coverProgressStyle >= CrossPointSettings::COVER_PROGRESS_STYLE_COUNT ||
      SETTINGS.coverProgressStyle == CrossPointSettings::COVER_PROGRESS_NONE || width < 20 || height < 24)
    return;

  const int percent = std::min<int>(100, rawPercent);
  char text[8];
  snprintf(text, sizeof(text), "%d%%", percent);
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, text);
  const bool compact = width < 74 || height < 116;

  switch (SETTINGS.coverProgressStyle) {
    case CrossPointSettings::COVER_PROGRESS_PERCENTAGE: {
      const int chipHeight = std::max(14, renderer.getLineHeight(SMALL_FONT_ID) + 4);
      const int chipWidth = std::min(width - 4, std::max(30, textWidth + 8));
      const int chipX = x + (width - chipWidth) / 2;
      const int chipY = y + height - chipHeight - (featured ? 8 : 3);
      renderer.fillRect(chipX, chipY, chipWidth, chipHeight, false);
      renderer.drawRect(chipX, chipY, chipWidth, chipHeight, true);
      renderer.drawText(SMALL_FONT_ID, chipX + (chipWidth - textWidth) / 2, chipY + 2, text, true,
                        EpdFontFamily::BOLD);
      break;
    }
    case CrossPointSettings::COVER_PROGRESS_BOOKMARK: {
      const int ribbonHeight = compact ? 20 : (featured ? 38 : 32);
      const int ribbonLift = compact ? 4 : 7;
      const int ribbonWidth = std::min(width - 5, std::max(compact ? 25 : 30, textWidth + (compact ? 7 : 12)));
      const int ribbonX = x + 3;
      const int ribbonY = y + height - ribbonHeight - ribbonLift;
      const int notch = compact ? 5 : 8;
      const int center = ribbonX + ribbonWidth / 2;
      const int xPoints[] = {ribbonX, ribbonX + ribbonWidth, ribbonX + ribbonWidth, center, ribbonX};
      const int yPoints[] = {ribbonY, ribbonY, ribbonY + ribbonHeight, ribbonY + ribbonHeight - notch,
                             ribbonY + ribbonHeight};
      renderer.fillPolygon(xPoints, yPoints, 5, true);
      const int textHeight = renderer.getLineHeight(SMALL_FONT_ID);
      renderer.drawText(SMALL_FONT_ID, ribbonX + (ribbonWidth - textWidth) / 2,
                        ribbonY + std::max(1, (ribbonHeight - textHeight) / 2), text, false,
                        EpdFontFamily::BOLD);
      break;
    }
    case CrossPointSettings::COVER_PROGRESS_LIBRARY_LABEL: {
      const char* label = compact ? "LIB" : "LIBRARY";
      const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
      const int tagWidth = std::min(width - 6, std::max(labelWidth, textWidth) + 8);
      const int tagHeight = std::max(15, renderer.getLineHeight(SMALL_FONT_ID) + 4);
      const int tagX = x + 3;
      const int tagY = y + 3;
      renderer.fillRect(tagX, tagY, tagWidth, tagHeight, false);
      renderer.drawRect(tagX, tagY, tagWidth, tagHeight, true);
      const char* shown = tagWidth >= labelWidth + textWidth + 5 ? label : text;
      const int shownWidth = renderer.getTextWidth(SMALL_FONT_ID, shown);
      renderer.drawText(SMALL_FONT_ID, tagX + (tagWidth - shownWidth) / 2, tagY + 2, shown, true,
                        EpdFontFamily::BOLD);
      break;
    }
    case CrossPointSettings::COVER_PROGRESS_READING_STRIP: {
      const int stripHeight = compact ? 3 : (featured ? 7 : 5);
      const int stripX = x + (compact ? 2 : 4);
      const int stripWidth = std::max(1, width - (compact ? 4 : 8));
      const int stripY = y + height - stripHeight - (featured ? 7 : 3);
      renderer.drawRect(stripX, stripY, stripWidth, stripHeight, true);
      const int fill = (stripWidth - 2) * percent / 100;
      if (fill > 0 && stripHeight > 2) renderer.fillRect(stripX + 1, stripY + 1, fill, stripHeight - 2, true);
      break;
    }
    case CrossPointSettings::COVER_PROGRESS_CORNER_BADGE: {
      const int badgeWidth = std::min(width - 6, std::max(compact ? 28 : 34, textWidth + 10));
      const int badgeHeight = std::max(16, renderer.getLineHeight(SMALL_FONT_ID) + 5);
      const int badgeX = x + width - badgeWidth - 3;
      const int badgeY = y + 3;
      renderer.fillRect(badgeX, badgeY, badgeWidth, badgeHeight, false);
      renderer.drawRect(badgeX, badgeY, badgeWidth, badgeHeight, true);
      renderer.drawText(SMALL_FONT_ID, badgeX + (badgeWidth - textWidth) / 2, badgeY + 2, text, true,
                        EpdFontFamily::BOLD);
      if (selected && !compact && badgeWidth > 4) renderer.drawLine(badgeX + 2, badgeY + badgeHeight - 2,
                                                                     badgeX + badgeWidth - 3,
                                                                     badgeY + badgeHeight - 2, true);
      break;
    }
    case CrossPointSettings::COVER_PROGRESS_NONE:
    case CrossPointSettings::COVER_PROGRESS_STYLE_COUNT:
    default:
      break;
  }
}

inline void drawPerspectiveProgress(const GfxRenderer& renderer, const int x, const int y, const int width,
                                   const int maxHeight, const int leftHeight, const int rightHeight,
                                   const uint8_t percent, const bool selected = false) {
  const int interiorHeight = std::min(leftHeight, rightHeight);
  if (width < 20 || maxHeight < 24 || interiorHeight < 24) return;
  const int safeHeight = std::max(24, interiorHeight / 3);
  const int safeY = y + (maxHeight - interiorHeight) / 2 + (interiorHeight - safeHeight) / 2;
  drawProgress(renderer, x + 2, safeY, width - 4, safeHeight, percent, selected, selected);
}

}  // namespace BookCoverTreatment
