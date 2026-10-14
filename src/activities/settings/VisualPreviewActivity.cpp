#include "VisualPreviewActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "components/themes/BookCoverTreatment.h"
#include "components/UITheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "fontIds.h"

namespace {
constexpr StrId kFeaturedFieldLabels[] = {
    StrId::STR_FEATURED_SHOW_COVER,       StrId::STR_FEATURED_SHOW_FILE_TYPE,
    StrId::STR_FEATURED_SHOW_AUTHOR,
    StrId::STR_FEATURED_SHOW_SYNOPSIS,    StrId::STR_FEATURED_SHOW_STATUS,
    StrId::STR_FEATURED_SHOW_PROGRESS,    StrId::STR_FEATURED_SHOW_STARTED,
    StrId::STR_FEATURED_SHOW_FINISHED,    StrId::STR_FEATURED_SHOW_READING_TIME,
    StrId::STR_FEATURED_SHOW_SESSIONS,
};
constexpr uint8_t CrossPointSettings::*kFeaturedFieldSettings[] = {
    &CrossPointSettings::featuredShowCover,       &CrossPointSettings::featuredShowFileType,
    &CrossPointSettings::featuredShowAuthor,
    &CrossPointSettings::featuredShowSynopsis,    &CrossPointSettings::featuredShowStatus,
    &CrossPointSettings::featuredShowProgress,    &CrossPointSettings::featuredShowStartedDate,
    &CrossPointSettings::featuredShowFinishedDate, &CrossPointSettings::featuredShowReadingTime,
    &CrossPointSettings::featuredShowSessions,
};
constexpr StrId kCoverProgressOptions[] = {
    StrId::STR_NONE_OPT, StrId::STR_PLAIN_PERCENTAGE, StrId::STR_BOOKMARK_RIBBON,
    StrId::STR_LIBRARY_LABEL, StrId::STR_READING_STRIP, StrId::STR_CORNER_BADGE,
};
constexpr StrId kSpineDecorationOptions[] = {
    StrId::STR_SPINE_BALANCED, StrId::STR_SPINE_SUBTLE, StrId::STR_SPINE_EXPRESSIVE,
};
constexpr StrId kSpineAppearanceOptions[] = {
    StrId::STR_SPINE_ALL_SPINES, StrId::STR_SPINE_SPINES_AND_COVERS, StrId::STR_SPINE_COVERS_ONLY,
};
constexpr StrId kPreviewTabs[] = {StrId::STR_FEATURED, StrId::STR_COVER, StrId::STR_SPINE};

uint32_t sampleHash(const uint32_t seed, const uint32_t index) {
  uint32_t value = seed ^ (index * 0x9E3779B9u);
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  return value ^ (value >> 16);
}
}  // namespace

VisualPreviewActivity::VisualPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("VisualPreview", renderer, mappedInput) {
  tabs_.reserve(3);
  tabKinds_.reserve(3);
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR) {
    tabs_.push_back({I18N.get(kPreviewTabs[0]), false});
    tabKinds_.push_back(Tab::Featured);
  } else {
    tab_ = Tab::Cover;
  }
  tabs_.push_back({I18N.get(kPreviewTabs[1]), false});
  tabKinds_.push_back(Tab::Cover);
  tabs_.push_back({I18N.get(kPreviewTabs[2]), false});
  tabKinds_.push_back(Tab::Spine);
}

void VisualPreviewActivity::onEnter() {
  Activity::onEnter();
  selectedRow_ = 0;
  requestUpdate();
}

int VisualPreviewActivity::rowCount() const {
  switch (tab_) {
    case Tab::Featured: return static_cast<int>(std::size(kFeaturedFieldLabels));
    case Tab::Cover: return 1;
    case Tab::Spine: return 5;
  }
  return 0;
}

int VisualPreviewActivity::previewPanelHeight(const int availableHeight) const {
  const int controlReserve = tab_ == Tab::Featured ? 150 : (tab_ == Tab::Cover ? 54 : 166);
  const int maxPreviewHeight = tab_ == Tab::Cover ? 250 : 190;
  return std::max(80, std::min(maxPreviewHeight, availableHeight - controlReserve - 8));
}

void VisualPreviewActivity::selectTab(const int index) {
  if (tabKinds_.empty()) return;
  const int clamped = std::clamp(index, 0, static_cast<int>(tabKinds_.size()) - 1);
  const Tab selectedTab = tabKinds_[static_cast<size_t>(clamped)];
  if (tab_ == selectedTab) return;
  tab_ = selectedTab;
  selectedRow_ = 0;
  requestUpdate();
}

void VisualPreviewActivity::showEnumPicker(const StrId title, const StrId* values, const size_t count,
                                           const uint8_t current, uint8_t CrossPointSettings::*setting) {
  const int initial = current < count ? current : 0;
  optionPopup_.show(title, values, static_cast<int>(count), initial, [this, setting](const int index) {
    if (SETTINGS.*setting == index) return;
    SETTINGS.*setting = static_cast<uint8_t>(index);
    SETTINGS.saveToFile();
    requestUpdate();
  });
  requestUpdate();
}

void VisualPreviewActivity::activateRow() {
  if (tab_ == Tab::Featured) {
    if (selectedRow_ >= std::size(kFeaturedFieldSettings)) return;
    const auto setting = kFeaturedFieldSettings[selectedRow_];
    SETTINGS.*setting = SETTINGS.*setting ? 0 : 1;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (tab_ == Tab::Cover) {
    showEnumPicker(StrId::STR_COVER_PROGRESS_STYLE, kCoverProgressOptions, std::size(kCoverProgressOptions),
                   SETTINGS.coverProgressStyle, &CrossPointSettings::coverProgressStyle);
    return;
  }

  switch (selectedRow_) {
    case 0:
      spineTarget_ = spineTarget_ == 0 ? 1 : 0;
      requestUpdate();
      break;
    case 1: {
      uint8_t& layout = spineTarget_ == 0 ? SETTINGS.recentBookLayout : SETTINGS.finishedBookLayout;
      const uint8_t next = layout == CrossPointSettings::FOLIO_LAYOUT_SPINE
                               ? CrossPointSettings::FOLIO_LAYOUT_TWO_ROW_SPINE
                               : CrossPointSettings::FOLIO_LAYOUT_SPINE;
      if (layout != next) {
        layout = next;
        SETTINGS.saveToFile();
      }
      requestUpdate();
      break;
    }
    case 2:
      showEnumPicker(StrId::STR_SPINE_APPEARANCE, kSpineAppearanceOptions,
                     std::size(kSpineAppearanceOptions), SETTINGS.spineAppearanceMode,
                     &CrossPointSettings::spineAppearanceMode);
      break;
    case 3:
      showEnumPicker(StrId::STR_SPINE_DECORATION, kSpineDecorationOptions,
                     std::size(kSpineDecorationOptions), SETTINGS.spineDecorationStyle,
                     &CrossPointSettings::spineDecorationStyle);
      break;
    case 4:
      // Preview-only randomness. The real shelf uses a stable book-derived
      // seed, and no generated shelf state is ever written to settings.
      rerollSeed_ += 0x9E3779B9u;
      requestUpdate();
      break;
    default:
      break;
  }
}

bool VisualPreviewActivity::handleTouch(const int listTop, const int listHeight, const int controlColumns,
                                        const int rowHeight) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tabTop = metrics.topPadding + metrics.headerHeight;
  int x = 0;
  int y = 0;
  const auto tabIndex = [this, tabTop, &metrics](const int tx, const int ty) {
    int index = -1;
    GUI.tabIndexFromPoint(renderer, Rect{0, tabTop, renderer.getScreenWidth(), metrics.tabBarHeight}, tabs_, tx, ty,
                          index);
    return index;
  };

  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int index = tabIndex(x, y);
    if (index >= 0) {
      selectTab(index);
      return true;
    }
    if (y >= listTop && y < listTop + listHeight && rowHeight > 0) {
      const int colWidth = renderer.getScreenWidth() / std::max(1, controlColumns);
      const int row = (y - listTop) / rowHeight;
      const int col = std::clamp(x / std::max(1, colWidth), 0, controlColumns - 1);
      const int index = row * controlColumns + col;
      if (index >= 0 && index < rowCount() && selectedRow_ != index) {
        selectedRow_ = static_cast<uint8_t>(index);
        requestUpdate();
      }
      return true;
    }
  }

  if (mappedInput.wasScreenTapped(x, y)) {
    const int index = tabIndex(x, y);
    if (index >= 0) {
      selectTab(index);
      return true;
    }
    if (y >= listTop && y < listTop + listHeight && rowHeight > 0) {
      const int colWidth = renderer.getScreenWidth() / std::max(1, controlColumns);
      const int row = (y - listTop) / rowHeight;
      const int col = std::clamp(x / std::max(1, colWidth), 0, controlColumns - 1);
      const int index = row * controlColumns + col;
      if (index >= 0 && index < rowCount()) {
        selectedRow_ = static_cast<uint8_t>(index);
        activateRow();
      }
      return true;
    }
  }
  return false;
}

void VisualPreviewActivity::loop() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const auto currentTab = std::find(tabKinds_.begin(), tabKinds_.end(), tab_);
  const int tabCount = static_cast<int>(tabKinds_.size());
  const int currentTabIndex = currentTab == tabKinds_.end()
                                  ? 0
                                  : static_cast<int>(std::distance(tabKinds_.begin(), currentTab));
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectTab((currentTabIndex + tabCount - 1) % tabCount);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectTab((currentTabIndex + 1) % tabCount);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int previewHeight = previewPanelHeight(bottom - top);
  const int controlTop = top + previewHeight + metrics.verticalSpacing;
  const int controlHeight = std::max(0, bottom - controlTop);
  const int columns = tab_ == Tab::Featured ? 2 : 1;
  const int rows = columns == 2 ? (rowCount() + 1) / 2 : rowCount();
  const int rowHeight = tab_ == Tab::Cover ? std::min(controlHeight, 54)
                                           : (rows > 0 ? controlHeight / rows : 0);
  if (handleTouch(controlTop, controlHeight, columns, rowHeight)) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selectedRow_ = selectedRow_ == 0 ? static_cast<uint8_t>(rowCount() - 1) : selectedRow_ - 1;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectedRow_ = static_cast<uint8_t>((selectedRow_ + 1) % std::max(1, rowCount()));
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
  }
}

void VisualPreviewActivity::renderFeatured(const int x, const int y, const int width, const int height) {
  renderer.fillRect(x, y, width, height, false);
  renderer.drawRect(x, y, width, height, true);
  const int inset = 10;
  const int coverWidth = SETTINGS.featuredShowCover ? std::min(112, (width - inset * 3) / 3) : 0;
  const int coverHeight = std::min(height - inset * 2, 176);
  if (coverWidth > 0) {
    const int coverX = x + inset;
    const int coverY = y + (height - coverHeight) / 2;
    renderer.fillRectDither(coverX, coverY, coverWidth, coverHeight, Color::LightGray);
    renderer.drawRect(coverX + 6, coverY + 7, coverWidth - 12, coverHeight - 14, true);
    renderer.drawCenteredText(SMALL_FONT_ID, coverY + coverHeight / 2, "SAMPLE", true);
    BookCoverTreatment::drawFrame(renderer, coverX, coverY, coverWidth, coverHeight, false, true);
    BookCoverTreatment::drawProgress(renderer, coverX, coverY, coverWidth, coverHeight, 62, false, true);
  }

  const int textX = coverWidth > 0 ? x + inset * 2 + coverWidth : x + inset;
  const int textWidth = std::max(1, x + width - inset - textX);
  const int badgeWidth = SETTINGS.featuredShowFileType
                             ? FolioNooirTheme::featuredFormatBadgeLabelWidth(renderer, "EPUB")
                             : 0;
  const int titleWidth = std::max(1, textWidth - badgeWidth - (badgeWidth > 0 ? 8 : 0));
  const int titleLineHeight = std::max(1, renderer.getLineHeight(UI_12_FONT_ID));
  const int smallLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  const int metadataLineHeight = smallLineHeight + 1;
  const int textTop = y + inset + 2;
  const int panelBottom = y + height - 2;
  const int usableTextHeight = std::max(0, panelBottom - textTop);
  const bool showAuthor = SETTINGS.featuredShowAuthor != 0;
  const bool showSynopsis = SETTINGS.featuredShowSynopsis != 0;
  const bool showCompactMetadata = SETTINGS.featuredShowStatus || SETTINGS.featuredShowProgress ||
                                   SETTINGS.featuredShowReadingTime || SETTINGS.featuredShowSessions;
  const bool showDates = SETTINGS.featuredShowStartedDate || SETTINGS.featuredShowFinishedDate;
  const int authorHeight = showAuthor ? renderer.getLineHeight(UI_10_FONT_ID) + 3 : 0;
  const int metadataHeight = showCompactMetadata
                                 ? metadataLineHeight + (SETTINGS.featuredShowProgress ? 10 : 0)
                                 : 0;
  const int datesHeight = showDates ? metadataLineHeight : 0;
  const bool titleOnly = !showAuthor && !showSynopsis && !showCompactMetadata && !showDates;
  const int titleAvailableHeight =
      std::max(0, usableTextHeight - authorHeight - metadataHeight - datesHeight -
                       (showSynopsis ? smallLineHeight : 0));
  const int titleAvailableLines = titleAvailableHeight / titleLineHeight;
  const int titleMaxLines = titleOnly ? std::max(1, usableTextHeight / titleLineHeight)
                             : showSynopsis ? std::max(1, std::min(2, titleAvailableLines))
                                            : std::max(1, titleAvailableLines);
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, "The Lantern Shore and the Map of Quiet Places",
                                               titleWidth, titleMaxLines);
  int cursorY = titleOnly
                    ? textTop + (usableTextHeight - static_cast<int>(titleLines.size()) * titleLineHeight) / 2
                    : textTop;
  const int titleY = cursorY;
  for (const auto& line : titleLines) {
    if (cursorY + titleLineHeight > panelBottom) break;
    renderer.drawText(UI_12_FONT_ID, textX, cursorY, line.c_str(), true, EpdFontFamily::BOLD);
    cursorY += titleLineHeight;
  }
  if (badgeWidth > 0) {
    FolioNooirTheme::drawFeaturedFormatBadgeLabel(renderer, "EPUB", textX + textWidth, titleY - 3);
  }
  if (showAuthor) {
    renderer.drawText(UI_10_FONT_ID, textX, cursorY, "M. N. Faris");
    cursorY += authorHeight;
  }
  if (showSynopsis) {
    const int synopsisAvailableHeight = std::max(0, panelBottom - cursorY - metadataHeight - datesHeight);
    const int synopsisMaxLines = synopsisAvailableHeight >= smallLineHeight
                                     ? 1 + (synopsisAvailableHeight - smallLineHeight) / metadataLineHeight
                                     : 0;
    if (synopsisMaxLines > 0) {
      const auto synopsisLines = renderer.wrappedText(
          SMALL_FONT_ID,
          "A quiet story of maps, memory, and the places we carry with us. Small choices reshape a familiar coast. "
          "Old letters reveal a path home. Every harbor keeps a different story.",
          textWidth, synopsisMaxLines);
      for (const auto& line : synopsisLines) {
        if (cursorY + smallLineHeight > panelBottom) break;
        renderer.drawText(SMALL_FONT_ID, textX, cursorY, line.c_str());
        cursorY += metadataLineHeight;
      }
    }
  }

  if (showCompactMetadata && cursorY + smallLineHeight <= panelBottom) {
    char compactMetadata[112] = {};
    size_t length = 0;
    const auto appendComponent = [&compactMetadata, &length](const char* component) {
      if (component == nullptr || component[0] == '\0' || length >= sizeof(compactMetadata) - 1) return;
      const size_t remaining = sizeof(compactMetadata) - length;
      const int written = snprintf(compactMetadata + length, remaining, "%s%s", length > 0 ? " · " : "", component);
      if (written > 0) length += std::min(static_cast<size_t>(written), remaining - 1);
    };
    if (SETTINGS.featuredShowStatus) appendComponent("Ongoing");
    if (SETTINGS.featuredShowProgress) appendComponent("62%");
    if (SETTINGS.featuredShowReadingTime) appendComponent("43 min");
    if (SETTINGS.featuredShowSessions) appendComponent("22 sessions");
    renderer.drawText(SMALL_FONT_ID, textX, cursorY, compactMetadata);
    cursorY += metadataLineHeight;
    if (SETTINGS.featuredShowProgress && cursorY + 7 <= panelBottom) {
      const int progressY = cursorY + 1;
      renderer.drawRect(textX, progressY, textWidth, 7);
      const int fillWidth = std::max(0, textWidth - 2) * 62 / 100;
      if (fillWidth > 0) renderer.fillRect(textX + 1, progressY + 1, fillWidth, 5);
      cursorY = progressY + 9;
    }
  }
  if (showDates && cursorY + smallLineHeight <= panelBottom) {
    const char* dates = SETTINGS.featuredShowStartedDate && SETTINGS.featuredShowFinishedDate
                            ? "Started 2026-02-14 · Finished 2026-10-02"
                            : (SETTINGS.featuredShowStartedDate ? "Started · 2026-02-14" : "Finished · 2026-10-02");
    const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, dates, textWidth);
    renderer.drawText(SMALL_FONT_ID, textX, cursorY, fitted.c_str());
    cursorY += metadataLineHeight;
  }
}

void VisualPreviewActivity::renderCover(const int x, const int y, const int width, const int height) {
  const int coverHeight = std::min(height - 28, 260);
  const int coverWidth = std::min(width - 32, coverHeight * 2 / 3);
  const int coverX = x + (width - coverWidth) / 2;
  const int coverY = y + 6;
  renderer.fillRectDither(coverX, coverY, coverWidth, coverHeight, Color::LightGray);
  renderer.drawRect(coverX + 9, coverY + 12, coverWidth - 18, coverHeight - 24, true);
  renderer.drawCenteredText(UI_10_FONT_ID, coverY + coverHeight / 2, "SAMPLE COVER", true);
  BookCoverTreatment::drawFrame(renderer, coverX, coverY, coverWidth, coverHeight, true, true);
  BookCoverTreatment::drawProgress(renderer, coverX, coverY, coverWidth, coverHeight, 62, true, true);
  const StrId choice = kCoverProgressOptions[SETTINGS.coverProgressStyle < std::size(kCoverProgressOptions)
                                                ? SETTINGS.coverProgressStyle
                                                : CrossPointSettings::COVER_PROGRESS_BOOKMARK];
  const char* label = I18N.get(choice);
  renderer.drawCenteredText(UI_10_FONT_ID, y + height - 18, label, true);
}

void VisualPreviewActivity::renderSpine(const int x, const int y, const int width, const int height) {
  renderer.fillRect(x, y, width, height, false);
  renderer.drawRect(x, y, width, height, true);
  const uint8_t layout = spineTarget_ == 0 ? SETTINGS.recentBookLayout : SETTINGS.finishedBookLayout;
  const bool twoRows = layout == CrossPointSettings::FOLIO_LAYOUT_TWO_ROW_SPINE;
  const int rowCount = twoRows ? 2 : 1;
  const uint8_t appearance = SETTINGS.spineAppearanceMode < CrossPointSettings::SPINE_APPEARANCE_MODE_COUNT
                                 ? SETTINGS.spineAppearanceMode
                                 : CrossPointSettings::SPINE_APPEARANCE_ALL_SPINES;
  const int rowHeight = (height - 24) / rowCount;
  const int margin = 16;

  for (int row = 0; row < rowCount; ++row) {
    const int rowTop = y + 8 + row * rowHeight;
    const int shelfY = rowTop + rowHeight - 20;
    const int shelfX = x + margin;
    const int shelfWidth = width - margin * 2;
    const int bookLimit = shelfX + shelfWidth - 48;
    int cursor = shelfX + 4;
    const int bookCount = twoRows ? 8 : 7;
    for (int book = 0; book < bookCount; ++book) {
      const uint32_t hash = sampleHash(rerollSeed_ + static_cast<uint32_t>(row * 61), static_cast<uint32_t>(book));
      const int gap = twoRows ? 3 : 4;
      const int targetWidth = twoRows ? 35 : 42;
      const int variation = twoRows ? 9 : 16;
      const int availableHeight = std::max(24, rowHeight - 28);
      const int heightVariation = std::max(1, availableHeight / 5);
      const int bookHeight = std::max(24, availableHeight - static_cast<int>((hash >> 8) % heightVariation));
      const bool coverFace = appearance == CrossPointSettings::SPINE_APPEARANCE_COVERS_ONLY ||
                             (appearance == CrossPointSettings::SPINE_APPEARANCE_SPINES_AND_COVERS &&
                              sampleHash(0x43E219u, static_cast<uint32_t>(book + row * 17)) % 4u == 0u);
      const int faceWidth = std::max(targetWidth, bookHeight * 2 / 3);
      const int plannedWidth = coverFace ? faceWidth : targetWidth + static_cast<int>(hash % variation) - variation / 2;
      const int bookWidth = std::min(plannedWidth, bookLimit - cursor - 2);
      if (bookWidth < 12) break;
      const int bookY = shelfY - bookHeight;
      const Color tone = (hash & 7u) == 0u ? Color::DarkGray : ((hash & 3u) == 0u ? Color::LightGray : Color::White);
      renderer.fillRectDither(cursor, bookY, bookWidth, bookHeight, tone);
      renderer.drawRect(cursor, bookY, bookWidth, bookHeight, true);

      if (coverFace) {
        const char* coverTitle = book % 2 == 0 ? "LANTERN" : "FIELD NOTES";
        const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, coverTitle, std::max(1, bookWidth - 6));
        const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, fitted.c_str());
        renderer.drawText(UI_10_FONT_ID, cursor + (bookWidth - textWidth) / 2,
                          bookY + bookHeight / 2, fitted.c_str(), true);
      }

      uint32_t styleRoll = (hash >> 16) & 7u;
      if (coverFace) styleRoll = 0;
      if (SETTINGS.spineDecorationStyle == CrossPointSettings::SPINE_DECORATION_SUBTLE && styleRoll < 5) styleRoll = 0;
      if (SETTINGS.spineDecorationStyle == CrossPointSettings::SPINE_DECORATION_BALANCED && styleRoll < 3)
        styleRoll = 0;
      if (styleRoll != 0) {
        switch ((hash >> 20) % 4u) {
          case 0:
            renderer.drawLine(cursor + 3, bookY + 8, cursor + bookWidth - 3, bookY + 8, true);
            break;
          case 1:
            renderer.drawLine(cursor + 3, shelfY - 8, cursor + bookWidth - 3, shelfY - 8, true);
            break;
          case 2:
            renderer.drawLine(cursor + 4, bookY + 9, cursor + 4, shelfY - 8, true);
            break;
          default:
            renderer.drawRect(cursor + 4, bookY + 7, std::max(1, bookWidth - 8), 8, true);
            break;
        }
      }
      const char* sampleTitle = book % 2 == 0 ? "LANTERN" : "FIELD NOTES";
      if (!coverFace && !twoRows && bookWidth >= 22 && bookHeight >= 78) {
        renderer.drawTextRotated90CW(SMALL_FONT_ID, cursor + bookWidth / 2, bookY + bookHeight / 2 + 23,
                                     sampleTitle, true);
      } else if (!coverFace && twoRows && bookWidth >= 32) {
        renderer.drawCenteredText(SMALL_FONT_ID, bookY + bookHeight / 2, book % 2 == 0 ? "LN" : "FN", true);
      }
      cursor += bookWidth + gap;
      if (cursor >= bookLimit) break;
    }

    renderer.fillRectDither(shelfX, shelfY, shelfWidth, 16, Color::DarkGray);
    renderer.drawRect(shelfX, shelfY, shelfWidth, 16, true);
    renderer.drawLine(shelfX + 3, shelfY + 3, shelfX + shelfWidth - 4, shelfY + 3, 2, true);
    renderer.drawLine(shelfX + 5, shelfY + 10, shelfX + shelfWidth - 6, shelfY + 10, true);

    const int plantX = shelfX + shelfWidth - 32;
    const int plantBase = shelfY;
    const int plantTop = std::max(rowTop + 10, shelfY - (twoRows ? 46 : 66));
    const int centerX = plantX + 12;
    renderer.drawLine(centerX, plantTop + 8, centerX, plantBase - 4, 2, true);
    const int leaves = 3 + static_cast<int>(sampleHash(rerollSeed_, static_cast<uint32_t>(row + 99)) % 4u);
    for (int leaf = 0; leaf < leaves; ++leaf) {
      const int offset = leaf / 2 * 7 + 4;
      const bool left = (leaf & 1) == 0;
      const int tipX = centerX + (left ? -1 : 1) * (5 + static_cast<int>((rerollSeed_ >> (leaf % 16)) & 3u));
      const int tipY = plantTop + 7 + offset;
      const int branchY = tipY + 4;
      renderer.drawLine(centerX, branchY, tipX, tipY, 2, true);
    }
    const int potWidth = 18 + static_cast<int>((rerollSeed_ >> (row * 3)) % 8u);
    renderer.drawRect(centerX - potWidth / 2, plantBase - 8, potWidth, 7, true);
    renderer.drawLine(centerX - potWidth / 2 + 2, plantBase - 2, centerX + potWidth / 2 - 2, plantBase - 2, true);
  }
}

void VisualPreviewActivity::renderControls(const int x, const int y, const int width, const int height) {
  if (tab_ == Tab::Featured) {
    constexpr int columns = 2;
    const int rowHeight = height / 5;
    const int cellWidth = width / columns;
    for (size_t i = 0; i < std::size(kFeaturedFieldLabels); ++i) {
      const int row = static_cast<int>(i) / columns;
      const int col = static_cast<int>(i) % columns;
      const int cellX = x + col * cellWidth + 4;
      const int cellY = y + row * rowHeight + 2;
      const int cellH = std::max(18, rowHeight - 4);
      renderer.drawRect(cellX, cellY, cellWidth - 8, cellH,
                        selectedRow_ == i ? 2 : 1, true);
      const bool enabled = SETTINGS.*(kFeaturedFieldSettings[i]) != 0;
      char label[96];
      snprintf(label, sizeof(label), "%s %s", enabled ? "[x]" : "[ ]", I18N.get(kFeaturedFieldLabels[i]));
      const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, label, cellWidth - 18);
      renderer.drawText(UI_10_FONT_ID, cellX + 5, cellY + (cellH - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                        fitted.c_str());
    }
    return;
  }

  const int rows = rowCount();
  const int rowHeight = tab_ == Tab::Cover ? std::min(height, 54) : (rows > 0 ? height / rows : height);
  for (int row = 0; row < rows; ++row) {
    const int rowY = y + row * rowHeight + 2;
    const int rowH = std::max(20, rowHeight - 4);
    renderer.drawRect(x + 4, rowY, width - 8, rowH, selectedRow_ == row ? 2 : 1, true);
    char value[96];
    const char* title = "";
    if (tab_ == Tab::Cover) {
      title = tr(STR_COVER_PROGRESS_STYLE);
      const uint8_t style = SETTINGS.coverProgressStyle < std::size(kCoverProgressOptions)
                                ? SETTINGS.coverProgressStyle
                                : CrossPointSettings::COVER_PROGRESS_BOOKMARK;
      snprintf(value, sizeof(value), "%s: %s", title, I18N.get(kCoverProgressOptions[style]));
    } else {
      const uint8_t layout = spineTarget_ == 0 ? SETTINGS.recentBookLayout : SETTINGS.finishedBookLayout;
      const bool twoRows = layout == CrossPointSettings::FOLIO_LAYOUT_TWO_ROW_SPINE;
      if (row == 0) {
        title = tr(STR_PREVIEW_TARGET);
        snprintf(value, sizeof(value), "%s: %s", title,
                 I18N.get(spineTarget_ == 0 ? StrId::STR_RECENT : StrId::STR_FINISHED));
      } else if (row == 1) {
        title = tr(STR_LAYOUT_SPINE);
        snprintf(value, sizeof(value), "%s: %s", title,
                 I18N.get(twoRows ? StrId::STR_LAYOUT_TWO_ROW_SPINE : StrId::STR_LAYOUT_SPINE));
      } else if (row == 2) {
        const uint8_t appearance = SETTINGS.spineAppearanceMode < std::size(kSpineAppearanceOptions)
                                      ? SETTINGS.spineAppearanceMode
                                      : CrossPointSettings::SPINE_APPEARANCE_ALL_SPINES;
        title = tr(STR_SPINE_APPEARANCE);
        snprintf(value, sizeof(value), "%s: %s", title, I18N.get(kSpineAppearanceOptions[appearance]));
      } else if (row == 3) {
        const uint8_t style = SETTINGS.spineDecorationStyle < std::size(kSpineDecorationOptions)
                                  ? SETTINGS.spineDecorationStyle
                                  : CrossPointSettings::SPINE_DECORATION_BALANCED;
        title = tr(STR_SPINE_DECORATION);
        snprintf(value, sizeof(value), "%s: %s", title, I18N.get(kSpineDecorationOptions[style]));
      } else {
        snprintf(value, sizeof(value), "%s", tr(STR_REROLL_PREVIEW));
      }
    }
    const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, value, width - 20);
    renderer.drawText(UI_10_FONT_ID, x + 12, rowY + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      fitted.c_str());
  }
}

void VisualPreviewActivity::render(RenderLock&&) {
  if (optionPopup_.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VISUAL_PREVIEW));
  for (size_t i = 0; i < tabs_.size(); ++i) tabs_[i].selected = tabKinds_[i] == tab_;
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs_,
                 false);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int previewHeight = previewPanelHeight(bottom - contentTop);
  const int controlTop = contentTop + previewHeight + metrics.verticalSpacing;
  const int controlHeight = std::max(0, bottom - controlTop);
  if (tab_ == Tab::Featured) {
    renderFeatured(8, contentTop, pageWidth - 16, previewHeight);
  } else if (tab_ == Tab::Cover) {
    renderCover(8, contentTop, pageWidth - 16, previewHeight);
  } else {
    renderSpine(8, contentTop, pageWidth - 16, previewHeight);
  }
  renderControls(4, controlTop, pageWidth - 8, controlHeight);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
