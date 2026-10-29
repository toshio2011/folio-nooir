#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <BidiUtils.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Bitmap.h>
#include <Cbz.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Utf8.h>
#include <Xtc.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>

#include "MappedInputManager.h"
#include "CrossPointState.h"
#include "BleInput.h"
#include "RecentBooksStore.h"
#include "BookStateStore.h"
#include "BookMetadataOverridesStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/home/SynopsisActivity.h"
#include "activities/home/ReadingStatsActivity.h"
#include "activities/home/ClockWeatherActivity.h"
#include "activities/home/ToDoListActivity.h"
#include "activities/reader/EpubReaderBookmarksActivity.h"
#include "activities/reader/EpubReaderClippingListActivity.h"
#include "activities/RenderLock.h"
#include "util/BookCacheUtils.h"
#include "util/BookFormat.h"
#include "util/HomeSnapshotPolicy.h"
#include "util/FolioLiteLayoutPolicy.h"
#include "util/StatisticsDate.h"
#include "util/RecentBooksViewPolicy.h"
#include "util/LibraryBookPreparationPolicy.h"
#include "util/SynopsisPreview.h"
#include "util/EpubDiagnostics.h"
#include "components/UITheme.h"
#include "components/themes/BookCoverTreatment.h"
#include "components/themes/folio_nooir/CarouselTheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "util/CoverStackGeometry.h"
#include "util/CbzDiagnostics.h"
#include "fontIds.h"

namespace {
enum class CarouselBitmapDrawResult : uint8_t { Unavailable, Drawn, Failed };
using SpineThumbnailResolver = Bitmap* (*)(void*, const char*);
ThumbnailPathMissMemo spineThumbnailPathMisses;
constexpr char FOLIO_HOME_SNAPSHOT[] = "/.crosspoint/folio_home.bin";
constexpr uint32_t FOLIO_HOME_MAGIC = 0x464E484D;  // "FNHM"
constexpr uint16_t FOLIO_HOME_VERSION = 11;
constexpr uint16_t FOLIO_HOME_PREVIOUS_VERSION = 10;
constexpr uint16_t FOLIO_HOME_LEGACY_VERSION = 9;
constexpr uint16_t FOLIO_HOME_OLDEST_VERSION = 8;
constexpr size_t FOLIO_HOME_LEGACY_PAGE_CAPACITY = SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY;
static_assert(HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS == SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY,
              "Home snapshot capacity must match the three-row planner bound");
bool isFolioNooirLiteTheme(const uint8_t theme) {
  return theme == CrossPointSettings::UI_THEME::FOLIO_NOOIR_LITE;
}

bool hasActiveHomeInput(const MappedInputManager& input) {
  if (input.wasAnyPressed() || input.wasAnyReleased()) return true;
  for (uint8_t index = 0; index < MappedInputManager::kButtonCount; ++index) {
    if (input.isPressed(static_cast<MappedInputManager::Button>(index))) return true;
  }
  return false;
}

bool isFolioShelfTheme(const uint8_t theme) {
  return theme == CrossPointSettings::UI_THEME::FOLIO_NOOIR || isFolioNooirLiteTheme(theme);
}

FolioShelfLayout homeShelfLayout(const FolioNooirTheme& theme, const GfxRenderer& renderer,
                                const ThemeMetrics& metrics) {
  return theme.shelfLayout(renderer, metrics);
}

CarouselLayout liteCarouselPageLayout(const GfxRenderer& renderer, const CarouselTheme& carouselTheme,
                                      const FolioShelfLayout& folioLayout) {
  const int lineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  return carouselTheme.carouselLayout(renderer, FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES,
                                      lineHeight, folioLayout.statsTop);
}

FolioShelfLayout liteBookDisplayLayout(FolioShelfLayout layout, const CarouselLayout& carouselLayout,
                                      const int screenWidth) {
  layout.gridTop = carouselLayout.carouselRegion.y;
  layout.gridHeight = carouselLayout.carouselRegion.height;
  layout.gridGap = 6;
  layout.columns = 4;
  layout.cardWidth = std::max(1, (screenWidth - layout.gridGap * (layout.columns + 1)) / layout.columns);
  layout.cardHeight = std::max(1, layout.gridHeight / 2);
  return layout;
}

void drawLiteSelectedBookDetails(GfxRenderer& renderer, const FolioNooirTheme& folioPresentation,
                                 const CarouselLayout& carouselLayout, const RecentBook& selected,
                                 const bool clearPreviousDetails) {
  const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  if (clearPreviousDetails) {
    renderer.fillRect(carouselLayout.headerRegion.x, carouselLayout.headerRegion.y,
                      carouselLayout.headerRegion.width, carouselLayout.headerRegion.height, false);
    renderer.fillRect(carouselLayout.synopsisRegion.x, carouselLayout.synopsisRegion.y,
                      carouselLayout.synopsisRegion.width,
                      carouselLayout.synopsisRegion.height + carouselLayout.statusRegion.height + 6, false);
  }

  const int badgeWidth = folioPresentation.featuredFormatBadgeWidth(renderer, selected.path.c_str());
  const int badgeGap = badgeWidth > 0 ? 8 : 0;
  const int titleRegionWidth = std::max(1, carouselLayout.titleRegion.width - badgeWidth - badgeGap);
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, selected.title.c_str(), titleRegionWidth);
  const auto authorUpdate = FolioLiteLayoutPolicy::authorTextUpdate(clearPreviousDetails, !selected.author.empty());
  const std::string author = authorUpdate.drawCurrent
                                 ? renderer.truncatedText(UI_10_FONT_ID, selected.author.c_str(),
                                                          carouselLayout.authorRegion.width)
                                 : std::string();
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title.c_str());
  renderer.drawText(UI_12_FONT_ID,
                    carouselLayout.titleRegion.x + (titleRegionWidth - titleWidth) / 2,
                    carouselLayout.titleRegion.y, title.c_str(), true);
  if (badgeWidth > 0) {
    folioPresentation.drawFeaturedFormatBadge(renderer, selected.path.c_str(),
                                              carouselLayout.titleRegion.x + carouselLayout.titleRegion.width,
                                              carouselLayout.titleRegion.y - 1);
  }
  if (authorUpdate.drawCurrent) {
    const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, author.c_str());
    renderer.drawText(UI_10_FONT_ID,
                      carouselLayout.authorRegion.x + (carouselLayout.authorRegion.width - authorWidth) / 2,
                      carouselLayout.authorRegion.y, author.c_str());
  }

  const std::string synopsisPreview = SynopsisPreview::firstWords(selected.synopsis);
  const char* synopsisText = synopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : synopsisPreview.c_str();
  const int synopsisMaxLines = std::clamp(
      carouselLayout.synopsisRegion.height / synopsisLineHeight, 1,
      static_cast<int>(FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_RESERVED_LINES));
  const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsisText,
                                                  carouselLayout.synopsisRegion.width, synopsisMaxLines);
  int synopsisY = carouselLayout.synopsisRegion.y;
  for (const auto& line : synopsisLines) {
    renderer.drawText(SMALL_FONT_ID, carouselLayout.synopsisRegion.x, synopsisY, line.c_str());
    synopsisY += synopsisLineHeight;
  }

  // The synopsis region is reserved at a fixed height, so the status row must
  // stay at its fixed bottom edge even when only the one-line placeholder fits.
  const int statusY = carouselLayout.statusRegion.y + 6;
  renderer.fillRect(carouselLayout.statusRegion.x, statusY - 6, carouselLayout.statusRegion.width, 1);

  const uint32_t readingMinutes = (selected.readingSeconds + 30) / 60;
  const char* readingState = selected.progressPercent >= 100 ? tr(STR_COMPLETE)
                                                               : (selected.progressPercent > 0 ? tr(STR_ONGOING)
                                                                                               : tr(STR_NEW));
  char progressValue[16];
  char readingTimeValue[32];
  char sessionsValue[32];
  snprintf(progressValue, sizeof(progressValue), "%u%%", selected.progressPercent);
  snprintf(readingTimeValue, sizeof(readingTimeValue), "%lu %s",
           static_cast<unsigned long>(readingMinutes), tr(STR_MIN_SHORT));
  const char* sessionLabel = selected.readingSessions == 1 ? tr(STR_SESSION_SINGULAR)
                                                            : tr(STR_SESSION_PLURAL);
  snprintf(sessionsValue, sizeof(sessionsValue), "%u %s", selected.readingSessions, sessionLabel);
  const char* statusValues[] = {readingState, progressValue, readingTimeValue, sessionsValue};
  for (int field = 0; field < 4; ++field) {
    const int fieldLeft = carouselLayout.statusRegion.x + carouselLayout.statusRegion.width * field / 4;
    const int fieldRight = carouselLayout.statusRegion.x + carouselLayout.statusRegion.width * (field + 1) / 4;
    const std::string value = renderer.truncatedText(SMALL_FONT_ID, statusValues[field],
                                                     std::max(1, fieldRight - fieldLeft - 4));
    const int valueWidth = renderer.getTextWidth(SMALL_FONT_ID, value.c_str());
    renderer.drawText(SMALL_FONT_ID, fieldLeft + (fieldRight - fieldLeft - valueWidth) / 2,
                      statusY, value.c_str());
  }
}

#if NOOIR_EPUB_DIAGNOSTICS
bool lifecycleHomeCoverRecorded = false;
#endif

bool isClippingsExport(const std::string& path) {
  std::string name = path;
  const size_t slash = name.find_last_of('/');
  if (slash != std::string::npos) name.erase(0, slash + 1);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name == "my clippings.txt" || name == "clippings.txt";
}

void hashBytes(uint64_t& hash, const void* data, const size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
}

uint64_t hashText(const std::string& text) {
  uint64_t hash = 1469598103934665603ULL;
  hashBytes(hash, text.data(), text.size());
  const size_t length = text.size();
  hashBytes(hash, &length, sizeof(length));
  return hash;
}

SpineShelfPlanner::Rect spineShelfBounds(const int pageWidth, const FolioShelfLayout& layout) {
  // Use the actual region below the featured divider and reserve the lower
  // quarter as breathing room above the statistics strip. The planner derives
  // the shelf baseline from this same rectangle, so drawing and hit testing
  // remain exact while taller books use the space available on each device.
  const int gridHeight = std::max(1, layout.gridHeight);
  const int bookTop = layout.gridTop + std::min(8, std::max(1, gridHeight / 12));
  const int shelfY = layout.gridTop + std::max(1, gridHeight * 3 / 4);
  const int usableBookHeight = std::max(1, shelfY - bookTop);
  return SpineShelfPlanner::Rect{layout.gridGap, bookTop,
                                 std::max(1, pageWidth - layout.gridGap * 2),
                                 usableBookHeight + SpineShelfPlanner::SHELF_BOTTOM_GAP};
}

SpineShelfPlanner::Rect twoRowSpineBounds(const int pageWidth, const FolioShelfLayout& layout) {
  // The two-row option uses the same featured/statistics boundaries as the
  // existing Spine, but gives the planner the complete bounded shelf region.
  const int top = layout.gridTop + 4;
  const int height = std::max(1, layout.gridHeight - 4 - SpineShelfPlanner::TWO_ROW_BOTTOM_GAP);
  return SpineShelfPlanner::Rect{layout.gridGap, top, std::max(1, pageWidth - layout.gridGap * 2), height};
}

SpineShelfPlanner::Rect threeRowSpineBounds(const int pageWidth, const FolioShelfLayout& layout) {
  const int top = layout.gridTop + 3;
  const int height = std::max(1, layout.gridHeight - SpineShelfPlanner::THREE_ROW_BOTTOM_GAP);
  return SpineShelfPlanner::Rect{layout.gridGap, top, std::max(1, pageWidth - layout.gridGap * 2), height};
}

void displayHomeFrame(GfxRenderer& renderer, const char* reason, const uint32_t renderStartedUs,
                      const uint8_t activeLayout) {
  const uint32_t renderDurationUs = micros() - renderStartedUs;
  const char* themeName = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR_LITE
                              ? "Lite"
                              : (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR ? "Main" : "other");
  const uint32_t nowMs = millis();
  static uint32_t lastSlowDiagnosticMs = 0;
  static uint32_t lastSelectionDiagnosticMs = 0;
  const bool initialFrame = reason != nullptr &&
                            (strcmp(reason, "initial") == 0 || strcmp(reason, "resume") == 0);
  const bool layoutFrame = reason != nullptr && strcmp(reason, "layout") == 0;
  const bool selectionFrame = reason != nullptr && strcmp(reason, "selection") == 0;
  const bool selectionSample = selectionFrame &&
                               (lastSelectionDiagnosticMs == 0 || nowMs - lastSelectionDiagnosticMs >= 1000);
  const bool slowFrame = renderDurationUs >= 100000 &&
                         (lastSlowDiagnosticMs == 0 || nowMs - lastSlowDiagnosticMs >= 5000);
  if (initialFrame || layoutFrame || selectionSample || slowFrame) {
    LOG_INF("SHELF", "render_ready theme=%s layout=%u reason=%s cpu_us=%lu free=%u largest=%u",
            themeName, static_cast<unsigned>(activeLayout), reason ? reason : "unknown",
            static_cast<unsigned long>(renderDurationUs), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    LOG_INF("SHELFPERF", "theme=%s layout=%u reason=%s total_us=%lu",
            themeName, static_cast<unsigned>(activeLayout), reason ? reason : "unknown",
            static_cast<unsigned long>(renderDurationUs));
    if (slowFrame) lastSlowDiagnosticMs = nowMs;
    if (selectionSample) lastSelectionDiagnosticMs = nowMs;
  }
  // Keep ordinary Home restores, patches, and overlay closes on the fast
  // waveform. The existing refresh setting latches periodic ghost-clean debt
  // for the next sleep-screen boundary; orientation still forces a full resync.
  auto& refreshState = HomeSnapshotPolicy::sharedRefreshState();
  static bool haveHomeOrientation = false;
  static uint8_t previousHomeOrientation = 0;
  const bool orientationChanged = haveHomeOrientation && previousHomeOrientation != SETTINGS.orientation;
  const uint8_t interval = HomeSnapshotPolicy::refreshIntervalForFrequency(SETTINGS.refreshFrequency);
  const auto decision = HomeSnapshotPolicy::chooseRefresh(orientationChanged);
  const auto waveform = decision.waveform == HomeSnapshotPolicy::RefreshWaveform::Full
                            ? HalDisplay::FULL_REFRESH
                            : HalDisplay::FAST_REFRESH;
  if (decision.waveform == HomeSnapshotPolicy::RefreshWaveform::Full) {
    LOG_INF("SHELF", "refresh_policy mode=full cause=%s interval=%u completed_fast=%u",
            HomeSnapshotPolicy::refreshReasonName(decision.reason), static_cast<unsigned>(interval),
            static_cast<unsigned>(refreshState.completedFastUpdates));
  }
  // Keep CPU rendering separate from the panel's blocking refresh time. The
  // display driver reports the latter after this call.
  const bool cleanupWasPending = refreshState.periodicCleanupPending;
  renderer.displayBuffer(waveform, reason);
  HomeSnapshotPolicy::recordRefresh(refreshState, decision, interval);
  if (!cleanupWasPending && refreshState.periodicCleanupPending) {
    LOG_INF("SHELF", "refresh_policy mode=deferred cause=periodic_ghost_cleanup interval=%u completed_fast=%u "
                     "boundary=sleep_screen",
            static_cast<unsigned>(interval), static_cast<unsigned>(refreshState.completedFastUpdates));
  }
  previousHomeOrientation = SETTINGS.orientation;
  haveHomeOrientation = true;
}

void drawLitePageIndicator(const GfxRenderer& renderer, const CarouselLayout& layout,
                           const size_t page, const size_t pages) {
  renderer.fillRect(layout.paginationRegion.x, layout.paginationRegion.y,
                    layout.paginationRegion.width, layout.paginationRegion.height, false);
  if (pages <= 1) return;
  char text[32];
  snprintf(text, sizeof(text), "< %s %u/%u >", tr(STR_PAGE),
           static_cast<unsigned>(page), static_cast<unsigned>(pages));
  const int width = renderer.getTextWidth(SMALL_FONT_ID, text) + 12;
  const int x = (renderer.getScreenWidth() - width) / 2;
  const int lineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  const int y = layout.paginationRegion.y + (layout.paginationRegion.height - lineHeight) / 2;
  renderer.fillRect(x, layout.paginationRegion.y, width, layout.paginationRegion.height, false);
  renderer.drawText(SMALL_FONT_ID, x + 6, y, text);
}

void drawSpinePlant(const GfxRenderer& renderer, const SpineShelfPlanner::Rect& rect, const uint32_t seed) {
  const int centerX = rect.x + rect.width / 2;
  const int potWidth = std::max(8, rect.width - 4 - static_cast<int>(seed % 5u));
  const int potX = rect.x + (rect.width - potWidth) / 2;
  const int potHeight = 5 + static_cast<int>((seed >> 4) % 4u);
  const int potY = rect.y + rect.height - potHeight;
  const int lean = static_cast<int>((seed >> 8) % 5u) - 2;
  const int stemX = std::clamp(centerX + lean, rect.x + 2, rect.x + rect.width - 3);
  renderer.drawLine(centerX, rect.y + 3, stemX, potY, 2);
  const int availableLeafHeight = std::max(1, potY - rect.y - 5);
  const int maxLeaves = std::max(2, availableLeafHeight / 3);
  const int leafCount = std::min(3 + static_cast<int>((seed >> 12) % 4u), maxLeaves);
  const int leafStep = leafCount > 1 ? availableLeafHeight / (leafCount - 1) : 0;
  for (int leaf = 0; leaf < leafCount; ++leaf) {
    const int stemY = std::max(rect.y + 2, potY - 3 - leaf * leafStep);
    const bool left = ((seed >> (leaf % 16)) & 1u) != 0u;
    const int leafLength = std::min(std::max(4, rect.width / 3), 5 + static_cast<int>((seed >> (leaf + 2)) % 5u));
    const int tipX = std::clamp(stemX + (left ? -leafLength : leafLength), rect.x + 2, rect.x + rect.width - 3);
    const int tipY = std::max(rect.y + 1, stemY - 1 - static_cast<int>((seed >> (leaf + 6)) & 1u));
    renderer.drawLine(stemX, stemY, tipX, tipY, 2);
  }
  if ((seed & 1u) != 0u && potWidth >= 14) {
    const int topLeft = potX;
    const int topRight = potX + potWidth - 1;
    const int bottomLeft = potX + 2;
    const int bottomRight = potX + potWidth - 3;
    const int xPoints[] = {topLeft, topRight, bottomRight, bottomLeft};
    const int yPoints[] = {potY, potY, potY + potHeight - 1, potY + potHeight - 1};
    renderer.fillPolygon(xPoints, yPoints, 4, true);
  } else {
    renderer.fillRect(potX, potY, potWidth, potHeight);
  }
  renderer.drawRect(potX, potY, potWidth, potHeight);
}

int spineFontIdForText(const GfxRenderer& renderer, const char* text) {
  // Rotated spine text does not go through the normal EPUB font selection
  // path. Use Nooir's configured Arabic family for RTL metadata so a valid
  // Arabic title is not rejected by the Latin-only Ubuntu UI family.
  if (text != nullptr && utf8ContainsRtlScript(text) &&
      renderer.getFontMap().find(ARABIC_12_FONT_ID) != renderer.getFontMap().end()) {
    return ARABIC_12_FONT_ID;
  }
  return text != nullptr && utf8ContainsRtlScript(text) ? UI_12_FONT_ID : SMALL_FONT_ID;
}

bool prepareSpineVisualText(const char* text, std::string& visual) {
  visual.clear();
  if (text == nullptr || *text == '\0') return false;
  if (!utf8ContainsRtlScript(text)) return true;
  return BidiUtils::applyBidiVisual(text, visual, static_cast<int>(BidiUtils::BidiBaseDir::AUTO)) && !visual.empty();
}

bool spineTextSupported(const GfxRenderer& renderer, const char* text, const int fontId,
                        const EpdFontFamily::Style style) {
  if (text == nullptr || *text == '\0') return false;
  const auto fontIt = renderer.getFontMap().find(renderer.resolveUiFontId(fontId));
  if (fontIt == renderer.getFontMap().end()) return false;

  std::string visual;
  const char* rendered = text;
  if (utf8ContainsRtlScript(text)) {
    if (!prepareSpineVisualText(text, visual)) return false;
    rendered = visual.c_str();
  }

  const char* cursor = rendered;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    if (cp == REPLACEMENT_GLYPH || !fontIt->second.hasCodepoint(cp, style)) return false;
  }
  return true;
}

void drawSpineHorizontalRule(const GfxRenderer& renderer, int x1, int x2, int y, bool state, int thickness = 1);
void drawSpineVerticalRule(const GfxRenderer& renderer, int x, int y1, int y2, bool state, int thickness = 1);
void drawSpineRectFast(const GfxRenderer& renderer, int x, int y, int width, int height, bool state);

void drawSpineBookDetails(const GfxRenderer& renderer, const SpineShelfPlanner::Rect& rect,
                          const SpineShelfPlanner::SpineStyle style, const bool black) {
  const int right = rect.x + rect.width - 1;
  const int bottom = rect.y + rect.height - 1;
  switch (style) {
    case SpineShelfPlanner::SpineStyle::TopBand:
      if (rect.width >= 40) drawSpineHorizontalRule(renderer, rect.x + 4, right - 3, rect.y + 9, black, 2);
      break;
    case SpineShelfPlanner::SpineStyle::BottomBand:
      if (rect.width >= 40) drawSpineHorizontalRule(renderer, rect.x + 4, right - 3, bottom - 9, black, 2);
      break;
    case SpineShelfPlanner::SpineStyle::InsetDivider:
      if (rect.width >= 46) drawSpineVerticalRule(renderer, rect.x + 5, rect.y + 8, bottom - 8, black);
      break;
    case SpineShelfPlanner::SpineStyle::DoubleRules:
      if (rect.width >= 40) {
        drawSpineHorizontalRule(renderer, rect.x + 5, right - 4, rect.y + 8, black);
        drawSpineHorizontalRule(renderer, rect.x + 5, right - 4, rect.y + 12, black);
        drawSpineHorizontalRule(renderer, rect.x + 5, right - 4, bottom - 12, black);
        drawSpineHorizontalRule(renderer, rect.x + 5, right - 4, bottom - 8, black);
      }
      break;
    case SpineShelfPlanner::SpineStyle::Ornament:
      if (rect.width >= 38 && rect.height >= 32) {
        const int cx = rect.x + 9;
        const int cy = rect.y + 14;
        renderer.drawLine(cx, cy - 3, cx + 3, cy, black);
        renderer.drawLine(cx + 3, cy, cx, cy + 3, black);
        renderer.drawLine(cx, cy + 3, cx - 3, cy, black);
        renderer.drawLine(cx - 3, cy, cx, cy - 3, black);
      }
      break;
    case SpineShelfPlanner::SpineStyle::LeatherBands:
      if (rect.width >= 38 && rect.height >= 38) {
        drawSpineHorizontalRule(renderer, rect.x + 3, right - 2, rect.y + 15, black, 3);
        drawSpineHorizontalRule(renderer, rect.x + 3, right - 2, bottom - 15, black, 3);
      }
      break;
    case SpineShelfPlanner::SpineStyle::CenterLabel:
      if (rect.width >= 36 && rect.height >= 58) {
        const int labelHeight = std::min(52, rect.height / 3);
        const int labelY = rect.y + (rect.height - labelHeight) / 2;
        drawSpineRectFast(renderer, rect.x + 4, labelY, rect.width - 8, labelHeight, true);
        if (rect.width >= 46 && labelHeight >= 24)
          drawSpineHorizontalRule(renderer, rect.x + 8, right - 7, labelY + labelHeight / 2, black);
      }
      break;
    case SpineShelfPlanner::SpineStyle::OldLibrary:
      if (rect.width >= 40 && rect.height >= 48) {
        drawSpineHorizontalRule(renderer, rect.x + 4, right - 3, rect.y + 8, black);
        drawSpineHorizontalRule(renderer, rect.x + 4, right - 3, rect.y + 11, black);
        drawSpineHorizontalRule(renderer, rect.x + 4, right - 3, bottom - 11, black);
        drawSpineHorizontalRule(renderer, rect.x + 4, right - 3, bottom - 8, black);
        const int centerY = rect.y + rect.height / 2;
        renderer.drawLine(rect.x + rect.width / 2, centerY - 3, rect.x + rect.width / 2 + 3, centerY, black);
        renderer.drawLine(rect.x + rect.width / 2 + 3, centerY, rect.x + rect.width / 2, centerY + 3, black);
        renderer.drawLine(rect.x + rect.width / 2, centerY + 3, rect.x + rect.width / 2 - 3, centerY, black);
        renderer.drawLine(rect.x + rect.width / 2 - 3, centerY, rect.x + rect.width / 2, centerY - 3, black);
      }
      break;
    case SpineShelfPlanner::SpineStyle::Plain:
    default:
      break;
  }
}

void drawSpineHorizontalRule(const GfxRenderer& renderer, const int x1, const int x2, const int y,
                              const bool state, const int thickness) {
  if (thickness <= 0) return;
  const int left = std::min(x1, x2);
  const int right = std::max(x1, x2);
  renderer.fillRect(left, y, right - left + 1, thickness, state);
}

void drawSpineVerticalRule(const GfxRenderer& renderer, const int x, const int y1, const int y2,
                           const bool state, const int thickness) {
  if (thickness <= 0) return;
  const int top = std::min(y1, y2);
  const int bottom = std::max(y1, y2);
  renderer.fillRect(x, top, thickness, bottom - top + 1, state);
}

void drawSpineRectFast(const GfxRenderer& renderer, const int x, const int y, const int width,
                       const int height, const bool state) {
  if (width <= 0 || height <= 0) return;
  drawSpineHorizontalRule(renderer, x, x + width - 1, y, state);
  drawSpineHorizontalRule(renderer, x, x + width - 1, y + height - 1, state);
  drawSpineVerticalRule(renderer, x, y, y + height - 1, state);
  drawSpineVerticalRule(renderer, x + width - 1, y, y + height - 1, state);
}

// Preserve GfxRenderer's inclusive x+width/y+height endpoints while replacing
// per-pixel outline loops with the renderer's row and column fill primitive.
void drawSpineRectStroke(const GfxRenderer& renderer, const int x, const int y, const int width,
                         const int height, const int lineWidth, const bool state) {
  if (width < 0 || height < 0 || lineWidth <= 0) return;
  for (int inset = 0; inset < lineWidth; ++inset) {
    const int left = x + inset;
    const int top = y + inset;
    const int right = x + width - inset;
    const int bottom = y + height - inset;
    if (right < left || bottom < top) continue;
    drawSpineHorizontalRule(renderer, left, right, top, state);
    drawSpineHorizontalRule(renderer, left, right, bottom, state);
    drawSpineVerticalRule(renderer, left, top, bottom, state);
    drawSpineVerticalRule(renderer, right, top, bottom, state);
  }
}

void drawSpineBook(const GfxRenderer& renderer, const SpineShelfPlanner::Book& book,
                   const SpineShelfPlanner::Rect& rect, const bool selected, const bool coverFace,
                   bool& thumbnailUnavailable, void* resolverContext,
                   const SpineThumbnailResolver resolveThumbnail, const bool fillCoverFaces = false,
                   ThreeRowShelfRenderProfile* profile = nullptr) {
  const uint32_t slotStartedUs = profile ? micros() : 0;
  if (profile) ++profile->slots;
  if (coverFace) {
    // Shelf furniture is drawn before cover faces, and bitmap drawing leaves
    // white source pixels untouched. Clear this face so the shelf cannot show
    // through the white parts of the cover.
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    bool drewCover = false;
    if (!thumbnailUnavailable && book.coverBmpPath != nullptr && *book.coverBmpPath != '\0') {
      // Spine shelves reuse the bounded parsed-source cache and never generate
      // thumbnails or touch source books while navigating.
      Bitmap* bitmap = nullptr;
      if (!spineThumbnailPathMisses.contains(book.coverBmpPath) && resolveThumbnail != nullptr) {
        const uint32_t resolveStartedUs = profile ? micros() : 0;
        bitmap = resolveThumbnail(resolverContext, book.coverBmpPath);
        if (profile) profile->resolveUs += micros() - resolveStartedUs;
      }
      if (bitmap != nullptr && bitmap->getWidth() > 0 && bitmap->getHeight() > 0) {
        const int inset = fillCoverFaces ? 2 : 3;
        const int innerWidth = std::max(1, rect.width - inset * 2);
        const int innerHeight = std::max(1, rect.height - inset * 2);
        if (fillCoverFaces) {
          const float sourceAspect = static_cast<float>(bitmap->getWidth()) / bitmap->getHeight();
          const float targetAspect = static_cast<float>(innerWidth) / innerHeight;
          const float cropX = sourceAspect > targetAspect ? 1.0f - targetAspect / sourceAspect : 0.0f;
          const float cropY = sourceAspect < targetAspect ? 1.0f - sourceAspect / targetAspect : 0.0f;
          const uint32_t bitmapStartedUs = profile ? micros() : 0;
          renderer.drawBitmap(*bitmap, rect.x + inset, rect.y + inset, innerWidth, innerHeight, cropX, cropY);
          if (profile) profile->bitmapDrawUs += micros() - bitmapStartedUs;
        } else {
          const float scale = std::min(static_cast<float>(innerWidth) / bitmap->getWidth(),
                                       static_cast<float>(innerHeight) / bitmap->getHeight());
          const int drawWidth = std::max(1, static_cast<int>(bitmap->getWidth() * scale));
          const int drawHeight = std::max(1, static_cast<int>(bitmap->getHeight() * scale));
          const uint32_t bitmapStartedUs = profile ? micros() : 0;
          renderer.drawBitmap(*bitmap, rect.x + (rect.width - drawWidth) / 2,
                              rect.y + (rect.height - drawHeight) / 2, drawWidth, drawHeight);
          if (profile) profile->bitmapDrawUs += micros() - bitmapStartedUs;
        }
        drewCover = true;
      } else {
        thumbnailUnavailable = true;
        spineThumbnailPathMisses.markMissing(book.coverBmpPath);
      }
    } else {
      thumbnailUnavailable = true;
    }

    if (!drewCover) {
      // A fixed, title-label placeholder keeps Covers Only readable when a
      // thumbnail is missing without repeating text measurement on navigation.
      renderer.fillRectDither(rect.x + 2, rect.y + 2, std::max(1, rect.width - 4),
                              std::max(1, rect.height - 4), Color::LightGray);
      const int labelWidth = std::max(4, rect.width - 12 - static_cast<int>(SpineShelfPlanner::stableHash(book) % 9u));
      const int labelX = rect.x + (rect.width - labelWidth) / 2;
      const int labelY = rect.y + rect.height / 3;
      drawSpineRectFast(renderer, labelX, labelY, labelWidth, std::max(14, rect.height / 4), true);
      drawSpineHorizontalRule(renderer, labelX + 4, labelX + labelWidth - 5, labelY + 5, true);
      drawSpineHorizontalRule(renderer, labelX + 4, labelX + std::max(4, labelWidth * 2 / 3), labelY + 10, true);
    }

    drawSpineRectStroke(renderer, rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, true);
    if (selected && rect.width > 10 && rect.height > 10) {
      drawSpineRectFast(renderer, rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6, true);
    }
    if (book.progressPercent > 0 && book.progressPercent < 100) {
      const int progressWidth = std::max(2, (rect.width - 8) * book.progressPercent / 100);
      renderer.fillRect(rect.x + 4, rect.y + rect.height - 5, progressWidth, 2, true);
    }
    if (profile) profile->slotUs += micros() - slotStartedUs;
    return;
  }

  char title[40];
  char author[32];
  const bool titleReady = SpineShelfPlanner::makeBookTitle(title, sizeof(title), book, rect);
  const int titleFontId = titleReady ? spineFontIdForText(renderer, title) : SMALL_FONT_ID;
  const bool titleSupported = titleReady && spineTextSupported(renderer, title, titleFontId, EpdFontFamily::BOLD);
  const bool authorReady = SpineShelfPlanner::makeAuthor(author, sizeof(author), book.author, rect);
  const int authorFontId = authorReady ? spineFontIdForText(renderer, author) : SMALL_FONT_ID;
  const bool authorSupported = authorReady && spineTextSupported(renderer, author, authorFontId,
                                                                  EpdFontFamily::REGULAR);
  std::string titleVisual;
  std::string authorVisual;
  const char* titleRenderText = title;
  const char* authorRenderText = author;
  bool titleRenderReady = titleSupported;
  bool authorRenderReady = authorSupported;
  if (titleRenderReady && utf8ContainsRtlScript(title)) {
    titleRenderReady = prepareSpineVisualText(title, titleVisual);
    if (titleRenderReady) titleRenderText = titleVisual.c_str();
  }
  if (authorRenderReady && utf8ContainsRtlScript(author)) {
    authorRenderReady = prepareSpineVisualText(author, authorVisual);
    if (authorRenderReady) authorRenderText = authorVisual.c_str();
  }

  const auto tone = SpineShelfPlanner::toneFor(book);
  const bool darkFill = tone == SpineShelfPlanner::SpineTone::DarkGray;
  const Color fillColor = darkFill ? Color::DarkGray
                                   : (tone == SpineShelfPlanner::SpineTone::LightGray ? Color::LightGray : Color::White);
  // Existing Nooir UI uses DarkGray with normal black text. Keep that proven
  // contrast treatment here; do not introduce an inverted-text path.
  constexpr bool blackDetailsAndText = true;
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, fillColor);
  drawSpineRectStroke(renderer, rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, true);
  if (selected && rect.width > 10 && rect.height > 10) {
    drawSpineRectFast(renderer, rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6, true);
  }
  drawSpineBookDetails(renderer, rect, SpineShelfPlanner::styleFor(book), blackDetailsAndText);

  if (book.progressPercent > 0 && book.progressPercent < 100) {
    const int progressWidth = std::max(2, (rect.width - 8) * book.progressPercent / 100);
    renderer.fillRect(rect.x + 4, rect.y + rect.height - 5, progressWidth, 2, blackDetailsAndText);
  }

  const int titleLength = titleSupported ? renderer.getTextWidth(titleFontId, title, EpdFontFamily::BOLD) : 0;
  const int authorLength = authorSupported ? renderer.getTextWidth(authorFontId, author) : 0;
  const bool drawTitle = titleRenderReady && titleLength > 0;
  const bool drawAuthor = authorRenderReady && authorLength > 0;
  constexpr int textInset = 10;
  constexpr int titleAuthorGap = 14;
  const int authorBudget = std::min(28, std::max(18, rect.height / 6));
  const bool drawBoth = drawTitle && drawAuthor && authorLength <= authorBudget &&
                        titleLength + titleAuthorGap + authorLength <= rect.height - textInset * 2;
  if (drawBoth) {
    const int textTop = rect.y + (rect.height - titleLength - titleAuthorGap - authorLength) / 2;
    renderer.drawTextRotated90CW(titleFontId, rect.x + rect.width / 2, textTop + titleLength, titleRenderText,
                                 blackDetailsAndText, EpdFontFamily::BOLD);
    if (SpineShelfPlanner::styleFor(book) == SpineShelfPlanner::SpineStyle::InsetDivider) {
      drawSpineHorizontalRule(renderer, rect.x + 7, rect.x + rect.width - 8,
                              textTop + titleLength + titleAuthorGap / 2, blackDetailsAndText);
    }
    renderer.drawTextRotated90CW(authorFontId, rect.x + rect.width / 2,
                                 textTop + titleLength + titleAuthorGap + authorLength, authorRenderText,
                                 blackDetailsAndText);
  } else if (drawTitle && titleLength <= rect.height - textInset * 2) {
    const int titleY = rect.y + (rect.height + titleLength) / 2;
    renderer.drawTextRotated90CW(titleFontId, rect.x + rect.width / 2, titleY, titleRenderText,
                                 blackDetailsAndText, EpdFontFamily::BOLD);
  }
  if (profile) profile->slotUs += micros() - slotStartedUs;
}

void drawSpineShelfFurniture(const GfxRenderer& renderer, const SpineShelfPlanner::Rect& shelf,
                             const SpineShelfPlanner::Rect& bounds, const bool drawSupports) {
  const int shelfRight = shelf.x + shelf.width - 1;
  const int shelfBottom = shelf.y + shelf.height - 1;
  renderer.fillRectDither(shelf.x, shelf.y, shelf.width, shelf.height, Color::DarkGray);
  drawSpineRectStroke(renderer, shelf.x, shelf.y, shelf.width, shelf.height, 2, true);
  drawSpineHorizontalRule(renderer, shelf.x + 3, shelfRight - 3, shelf.y, true, 2);
  drawSpineHorizontalRule(renderer, shelf.x + 4, shelfRight - 4, shelf.y + 7, true);
  drawSpineHorizontalRule(renderer, shelf.x, shelfRight, shelfBottom, true, 2);
  if (shelf.width > 140) {
    const int grainLeft = shelf.x + shelf.width / 4;
    const int grainRight = grainLeft + std::min(34, shelf.width / 9);
    drawSpineHorizontalRule(renderer, grainLeft, grainRight, shelf.y + 11, true);
    drawSpineHorizontalRule(renderer, shelfRight - shelf.width / 4 - 24, shelfRight - shelf.width / 4, shelf.y + 11, true);
  }

  // Two compact braced supports make the plank read as furniture while
  // staying inside the reserved gap above the statistics strip.
  const int supportTop = shelfBottom + 1;
  const int supportBottom = std::min(bounds.y + bounds.height - 1,
                                     supportTop + SpineShelfPlanner::SHELF_SUPPORT_HEIGHT);
  if (drawSupports && supportBottom >= supportTop + 8 && shelf.width >= 120) {
    const int leftSupportX = shelf.x + shelf.width / 5;
    const int rightSupportX = shelf.x + shelf.width - shelf.width / 5;
    constexpr int supportWidth = 8;
    for (const int supportX : {leftSupportX, rightSupportX}) {
      const int supportLeft = supportX - supportWidth / 2;
      const int supportHeight = supportBottom - supportTop + 1;
      renderer.fillRectDither(supportLeft, supportTop, supportWidth, supportHeight, Color::DarkGray);
      drawSpineRectStroke(renderer, supportLeft, supportTop, supportWidth, supportHeight, 1, true);
      renderer.drawLine(supportX, supportTop + 4,
                        supportX + (supportX == leftSupportX ? 17 : -17), supportBottom, 2, true);
      drawSpineHorizontalRule(renderer, supportLeft, supportLeft + supportWidth - 1, supportBottom, true, 2);
    }
  }
}

void drawSpineSlotFocusOutline(const GfxRenderer& renderer, const SpineShelfPlanner::FocusFrame& frame,
                               const bool selected) {
  if (!frame.valid()) return;

  // The book's own bottom border meets the shelf and closes this three-sided
  // outline. Keep the focus stroke one pixel outside the cached slot so it can
  // be erased with white strokes without redrawing the book face, thumbnail,
  // or shelf. Its explicit inclusive bounds include the outer right stroke.
  // One pixel also fits the tight gap in the two-row layout.
  drawSpineHorizontalRule(renderer, frame.left, frame.right, frame.top, selected);
  drawSpineVerticalRule(renderer, frame.left, frame.top, frame.bottom, selected);
  drawSpineVerticalRule(renderer, frame.right, frame.top, frame.bottom, selected);
}

void drawSpinePageSelectionMarker(const GfxRenderer& renderer, const SpineShelfPlanner::Page& page,
                                 const size_t index, const bool selected,
                                 SpineShelfPlanner::RenderedFocusGeometry& renderedFocus) {
  for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
    const auto& slot = page.slots[slotIndex];
    if (slot.itemIndex != index || slot.itemIndex < page.firstIndex ||
        slot.itemIndex - page.firstIndex >= SpineShelfPlanner::MAX_BOOKS) continue;
    SpineShelfPlanner::FocusFrame frame{};
    SpineShelfPlanner::Rect renderedSlot{};
    bool restoreRenderedRightEdge = false;
    if (selected) {
      renderedFocus.remember(index, slot.rect);
      frame = SpineShelfPlanner::focusFrameFor(slot.rect);
    } else {
      const auto deselection = SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, index, slot.rect);
      renderedSlot = deselection.finalSlot;
      frame = deselection.frame;
      restoreRenderedRightEdge = deselection.restoredPreviousGeometry;
    }
    drawSpineSlotFocusOutline(renderer, frame, selected);
    if (restoreRenderedRightEdge) {
      // Erasing the exterior focus stroke can visually break the adjacent
      // book edge after incremental refresh. Restore its right edge from the
      // exact final slot geometry that was painted.
      const int right = SpineShelfPlanner::slotRightEdgeX(renderedSlot);
      renderer.fillRect(right, renderedSlot.y, 1, renderedSlot.height, true);
    }
    return;
  }
}

void drawTwoRowSpinePageSelectionMarker(const GfxRenderer& renderer, const SpineShelfPlanner::TwoRowPage& page,
                                       const size_t index, const bool selected,
                                       SpineShelfPlanner::RenderedFocusGeometry& renderedFocus) {
  for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
    const auto& slot = page.slots[slotIndex];
    if (slot.itemIndex != index || slot.itemIndex < page.firstIndex ||
        slot.itemIndex - page.firstIndex >= SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY) continue;
    SpineShelfPlanner::FocusFrame frame{};
    SpineShelfPlanner::Rect renderedSlot{};
    bool restoreRenderedRightEdge = false;
    if (selected) {
      renderedFocus.remember(index, slot.rect);
      frame = SpineShelfPlanner::focusFrameFor(slot.rect);
    } else {
      const auto deselection = SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, index, slot.rect);
      renderedSlot = deselection.finalSlot;
      frame = deselection.frame;
      restoreRenderedRightEdge = deselection.restoredPreviousGeometry;
    }
    drawSpineSlotFocusOutline(renderer, frame, selected);
    if (restoreRenderedRightEdge) {
      const int right = SpineShelfPlanner::slotRightEdgeX(renderedSlot);
      renderer.fillRect(right, renderedSlot.y, 1, renderedSlot.height, true);
    }
    return;
  }
}

void drawThreeRowSpinePageSelectionMarker(const GfxRenderer& renderer, const SpineShelfPlanner::ThreeRowPage& page,
                                         const size_t index, const bool selected,
                                         SpineShelfPlanner::RenderedFocusGeometry& renderedFocus) {
  for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
    const auto& slot = page.slots[slotIndex];
    if (slot.itemIndex != index || slot.itemIndex < page.firstIndex ||
        slot.itemIndex - page.firstIndex >= SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY) continue;
    SpineShelfPlanner::Rect renderedSlot{};
    const auto deselection = selected
                                 ? SpineShelfPlanner::FocusDeselectionGeometry{}
                                 : SpineShelfPlanner::focusGeometryForDeselection(renderedFocus, index, slot.rect);
    SpineShelfPlanner::FocusFrame frame = selected ? SpineShelfPlanner::focusFrameFor(slot.rect) : deselection.frame;
    if (selected) renderedFocus.remember(index, slot.rect);
    drawSpineSlotFocusOutline(renderer, frame, selected);
    if (!selected && deselection.restoredPreviousGeometry) {
      const int right = SpineShelfPlanner::slotRightEdgeX(deselection.finalSlot);
      renderer.fillRect(right, deselection.finalSlot.y, 1, deselection.finalSlot.height, true);
    }
    return;
  }
}

void drawSpineShelf(const GfxRenderer& renderer, const SpineShelfPlanner::Page& page,
                    const SpineShelfPlanner::Book* books, const size_t selectedIndex,
                    const SpineShelfPlanner::Rect& bounds, bool* thumbnailUnavailable,
                    void* resolverContext, const SpineThumbnailResolver resolveThumbnail,
                    const bool fillCoverFaces = false) {
  if (books == nullptr) return;
  (void)selectedIndex;
  const auto shelf = SpineShelfPlanner::shelfRect(page, bounds);
  drawSpineShelfFurniture(renderer, shelf, bounds, true);
  if (page.plant.visible) drawSpinePlant(renderer, page.plant.rect, page.plant.visualSeed);

  for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
    const auto& slot = page.slots[slotIndex];
    if (slot.itemIndex < page.firstIndex || slot.itemIndex - page.firstIndex >= SpineShelfPlanner::MAX_BOOKS) continue;
    const size_t localIndex = slot.itemIndex - page.firstIndex;
    drawSpineBook(renderer, books[localIndex], slot.rect, false, slot.cover,
                  thumbnailUnavailable[localIndex], resolverContext, resolveThumbnail, fillCoverFaces);
  }
}

void drawTwoRowSpineShelf(const GfxRenderer& renderer, const SpineShelfPlanner::TwoRowPage& page,
                          const SpineShelfPlanner::Book* books, const size_t selectedIndex,
                          const SpineShelfPlanner::Rect& bounds, bool* thumbnailUnavailable,
                          void* resolverContext, const SpineThumbnailResolver resolveThumbnail,
                          const bool fillCoverFaces = false) {
  if (books == nullptr) return;
  (void)selectedIndex;
  for (size_t row = 0; row < 2; ++row) {
    const auto shelf = SpineShelfPlanner::twoRowShelfRect(page, bounds, row);
    if (shelf.width <= 0 || shelf.height <= 0) continue;
    // The upper row has only a narrow inter-row gap, so its plank keeps the
    // original spine treatment but omits long supports that could touch the
    // books below. The lower shelf has the full braced treatment.
    drawSpineShelfFurniture(renderer, shelf, bounds, row == 1);
    if (page.plants[row].visible) drawSpinePlant(renderer, page.plants[row].rect, page.plants[row].visualSeed);
  }

  for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
    const auto& slot = page.slots[slotIndex];
    if (slot.itemIndex < page.firstIndex || slot.itemIndex - page.firstIndex >= SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY) continue;
    const size_t localIndex = slot.itemIndex - page.firstIndex;
    drawSpineBook(renderer, books[localIndex], slot.rect, false, slot.cover,
                  thumbnailUnavailable[localIndex], resolverContext, resolveThumbnail, fillCoverFaces);
  }
}

void drawThreeRowSpineShelf(const GfxRenderer& renderer, const SpineShelfPlanner::ThreeRowPage& page,
                            const SpineShelfPlanner::Book* books, const SpineShelfPlanner::Rect& bounds,
                            bool* thumbnailUnavailable, void* resolverContext,
                            const SpineThumbnailResolver resolveThumbnail, const bool fillCoverFaces,
                            ThreeRowShelfRenderProfile* profile = nullptr) {
  if (books == nullptr) return;
  for (size_t row = 0; row < 3; ++row) {
    const auto shelf = SpineShelfPlanner::threeRowShelfRect(page, bounds, row);
    if (shelf.width <= 0 || shelf.height <= 0) continue;
    const uint32_t furnitureStartedUs = profile ? micros() : 0;
    drawSpineShelfFurniture(renderer, shelf, bounds, false);
    if (page.plants[row].visible) drawSpinePlant(renderer, page.plants[row].rect, page.plants[row].visualSeed);
    if (profile) profile->furnitureUs += micros() - furnitureStartedUs;
  }
  for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
    const auto& slot = page.slots[slotIndex];
    if (slot.itemIndex < page.firstIndex ||
        slot.itemIndex - page.firstIndex >= SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY) continue;
    const size_t localIndex = slot.itemIndex - page.firstIndex;
    drawSpineBook(renderer, books[localIndex], slot.rect, false, slot.cover,
                  thumbnailUnavailable[localIndex], resolverContext, resolveThumbnail, fillCoverFaces, profile);
  }
}
}  // namespace

void RecentBooksActivity::loadRecentBooks(const bool renderLockHeld) {
  spineShelfVisualsDirty = true;
  liteSelectedDetailsDirty = true;
  recentBooks.reset();
  invalidateSpinePageWindow();
  if (renderLockHeld) {
    invalidateCarouselHqProbesLocked();
  } else {
    invalidateCarouselHqProbes();
  }
  // Take the view after any RenderLock acquisition above. This keeps lock
  // ordering consistent and avoids holding the store mutex while waiting for
  // a render boundary.
  recentBooks = RECENT_BOOKS.getBooks();
}

void RecentBooksActivity::markStatsReturnFrameReady() {
  if (!statsReturnAwaitingFirstRender) return;
  statsReturnAwaitingFirstRender = false;
  LOG_INF("RSDIAG", "stage=parent_return_ready books=%u selected=%u tab=%u free=%u largest=%u",
          static_cast<unsigned>(recentBooks.size()), static_cast<unsigned>(selectorIndex),
          static_cast<unsigned>(activeTab), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  LOG_INF("RSDIAG", "stage=parent_recentbooks_resumed free=%u largest=%u",
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  bleinput::requestLifecycleReevaluation();
}

void RecentBooksActivity::invalidateCarouselHqProbes() {
  RenderLock lock;
  invalidateCarouselHqProbesLocked();
}

void RecentBooksActivity::invalidateCarouselHqProbesLocked() {
  for (auto& probe : carouselHqProbes) probe = {};
  normalThumbnailMisses.reset(recentBooks.size());
  invalidateCarouselSourceCacheLocked();
}

void RecentBooksActivity::invalidateCarouselSourceCache() {
  RenderLock lock;
  invalidateCarouselSourceCacheLocked();
}

void RecentBooksActivity::invalidateCarouselSourceCacheLocked() {
  for (auto& entry : carouselSourceCache) {
    entry.bitmap.reset();
    entry.file = HalFile{};
    entry.path.clear();
    entry.valid = false;
    entry.lastUsed = 0;
  }
  carouselSourceCacheClock = 0;
}

void RecentBooksActivity::invalidateCarouselSource(const std::string& path) {
  RenderLock lock;
  invalidateCarouselSourceLocked(path);
}

void RecentBooksActivity::invalidateCarouselSourceLocked(const std::string& path) {
  for (auto& entry : carouselSourceCache) {
    if (!entry.valid || entry.path != path) continue;
    entry.bitmap.reset();
    entry.file = HalFile{};
    entry.path.clear();
    entry.valid = false;
    entry.lastUsed = 0;
    return;
  }
}

RecentBooksActivity::CarouselSourceCacheEntry* RecentBooksActivity::getCarouselSource(
    const std::string& path, CarouselSourceTiming& timing, CarouselSourceFrame* frame) {
  timing = {};
  if (path.empty()) return nullptr;

  if (++carouselSourceCacheClock == 0) carouselSourceCacheClock = 1;
  for (auto& entry : carouselSourceCache) {
    if (!entry.valid || entry.path != path) continue;
    if (!entry.bitmap || !entry.file.isOpen()) {
      invalidateCarouselSourceLocked(path);
      break;
    }
    entry.lastUsed = carouselSourceCacheClock;
    timing.reused = true;
    return &entry;
  }

  CarouselSourceCacheEntry* replacement = nullptr;
  for (auto& entry : carouselSourceCache) {
    if (!entry.valid) {
      replacement = &entry;
      break;
    }
    if (frame != nullptr && frame->protects(entry.path)) continue;
    if (replacement == nullptr || entry.lastUsed < replacement->lastUsed) replacement = &entry;
  }
  if (replacement == nullptr) {
    return nullptr;
  }

  replacement->bitmap.reset();
  replacement->file = HalFile{};
  replacement->path.clear();
  replacement->valid = false;
  replacement->lastUsed = 0;

  const uint32_t openStartedUs = activeThreeRowShelfProfile != nullptr ? micros() : 0;
  const bool opened = Storage.openFileForRead("SHELF", path, replacement->file);
  if (activeThreeRowShelfProfile != nullptr) timing.openUs = micros() - openStartedUs;
  if (!opened) {
    replacement->file = HalFile{};
    return nullptr;
  }

  std::unique_ptr<Bitmap> bitmap(new Bitmap(replacement->file));
  const uint32_t headerStartedUs = activeThreeRowShelfProfile != nullptr ? micros() : 0;
  const BmpReaderError headerResult = bitmap->parseHeaders();
  if (activeThreeRowShelfProfile != nullptr) timing.headerUs = micros() - headerStartedUs;
  const bool valid = headerResult == BmpReaderError::Ok && bitmap->getWidth() > 0 && bitmap->getHeight() > 0;
  if (valid) replacement->path = path;
  if (!valid) {
    bitmap.reset();
    replacement->file = HalFile{};
    return nullptr;
  }

  replacement->bitmap = std::move(bitmap);
  replacement->valid = true;
  replacement->lastUsed = carouselSourceCacheClock;
  return replacement;
}

Bitmap* RecentBooksActivity::getSpineThumbnail(const char* sourcePath) {
  if (sourcePath == nullptr || *sourcePath == '\0') return nullptr;
  const std::string thumbPath = UITheme::getCoverThumbPath(sourcePath, FolioNooirTheme::COVER_HEIGHT);
  CarouselSourceTiming timing;
  CarouselSourceCacheEntry* source = getCarouselSource(thumbPath, timing);
  if (activeThreeRowShelfProfile != nullptr) {
    activeThreeRowShelfProfile->sourceOpenUs += timing.openUs;
    activeThreeRowShelfProfile->headerParseUs += timing.headerUs;
    if (timing.reused && activeThreeRowShelfProfile->sourceCacheHits < UINT16_MAX) {
      ++activeThreeRowShelfProfile->sourceCacheHits;
    }
  }
  if (source == nullptr || source->bitmap == nullptr) return nullptr;
  if (source->bitmap->rewindToData() != BmpReaderError::Ok) {
    invalidateCarouselSourceLocked(thumbPath);
    return nullptr;
  }
  return source->bitmap.get();
}

Bitmap* RecentBooksActivity::spineThumbnailResolver(void* context, const char* sourcePath) {
  if (context == nullptr) return nullptr;
  return static_cast<RecentBooksActivity*>(context)->getSpineThumbnail(sourcePath);
}

void RecentBooksActivity::prepareCarouselSourceFrame(
    const std::array<CoverStackSlot, CAROUSEL_FRAME_SLOT_COUNT>& stackSlots, CarouselSourceFrame& frame) {
  frame = {};

  for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
    const auto& stackSlot = stackSlots[slotIndex];
    if (!stackSlot.valid || stackSlot.itemIndex >= visibleBookCount) continue;
    const size_t recentIndex = recentIndexForVisibleIndex(stackSlot.itemIndex);
    if (recentIndex == SIZE_MAX) continue;

    auto& planned = frame.slots[slotIndex];
    const RecentBook& book = recentBooks[recentIndex];
    planned.valid = true;
    planned.fallbackPath = UITheme::getCoverThumbPath(book.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);

    const bool isCenter = stackSlot.depth == 0 && !book.coverBmpPath.empty();
    if (isCenter) {
      auto& hqProbe = carouselHqProbe(book);
      if (!hqProbe.unavailable) {
        planned.hqAttempted = true;
        planned.primaryPath = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
      }
    }
    if (planned.primaryPath.empty()) planned.primaryPath = planned.fallbackPath;

    // Keep both possible center variants protected when the HQ probe is still
    // eligible. Only one will become the resolved source, but the fallback
    // must not be evicted before an HQ open/read failure is known.
    frame.protectPath(planned.primaryPath);
    if (planned.hqAttempted) frame.protectPath(planned.fallbackPath);
  }

  for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
    auto& planned = frame.slots[slotIndex];
    if (!planned.valid || planned.primaryPath.empty()) continue;

    auto resolve = [&](const std::string& path, const bool hq, bool& fromCache) {
      fromCache = false;
      if (path.empty()) return false;
      const size_t recentIndex = recentIndexForVisibleIndex(stackSlots[slotIndex].itemIndex);
      if (!hq && recentIndex != SIZE_MAX &&
          (normalThumbnailMisses.contains(recentIndex) ||
           carouselHqProbe(recentBooks[recentIndex]).normalUnavailable)) {
        return false;
      }
      CarouselSourceTiming timing;
      const bool available = getCarouselSource(path, timing, &frame) != nullptr;
      fromCache = available && timing.reused;
      if (!available && hq) {
        // The current render path marks an unavailable HQ file and then uses
        // the normal thumbnail without retrying it on later frames.
        if (recentIndex != SIZE_MAX) carouselHqProbe(recentBooks[recentIndex]).unavailable = true;
      } else if (!available && recentIndex != SIZE_MAX) {
        // Keep the existing small probe hot, and remember the miss for this
        // activity's full recent-book list so paging cannot evict it.
        carouselHqProbe(recentBooks[recentIndex]).normalUnavailable = true;
        normalThumbnailMisses.markMissing(recentIndex);
      }
      return available;
    };

    bool resolvedFromCache = false;
    if (planned.hqAttempted && resolve(planned.primaryPath, true, resolvedFromCache)) {
      planned.resolvedPath = planned.primaryPath;
      planned.resolvedHq = true;
      planned.resolvedFromCache = resolvedFromCache;
    } else if (resolve(planned.fallbackPath, false, resolvedFromCache)) {
      planned.resolvedPath = planned.fallbackPath;
      planned.resolvedHq = false;
      planned.resolvedFromCache = resolvedFromCache;
    }
  }

  // Candidate protection was needed while resolving HQ-versus-normal
  // fallback. During the actual draw, retain only the final source selected
  // for each slot; a render-failure fallback can use the spare cache slot.
  frame.retainResolvedPaths();
}

uint64_t RecentBooksActivity::carouselCoverIdentity(const RecentBook& book) const {
  uint64_t identity = 1469598103934665603ULL;
  hashBytes(identity, book.path.data(), book.path.size());
  hashBytes(identity, book.coverBmpPath.data(), book.coverBmpPath.size());
  return identity;
}

RecentBooksActivity::CarouselHqProbe& RecentBooksActivity::carouselHqProbe(const RecentBook& book) {
  const uint64_t identity = carouselCoverIdentity(book);
  for (auto& probe : carouselHqProbes) {
    if (probe.valid && probe.identity == identity) return probe;
  }
  for (auto& probe : carouselHqProbes) {
    if (!probe.valid) {
      probe = {true, identity, false, false};
      return probe;
    }
  }
  auto& replacement = carouselHqProbes[identity % carouselHqProbes.size()];
  replacement = {true, identity, false, false};
  return replacement;
}

void RecentBooksActivity::rebuildVisibleBooks() {
  visibleBookCount = RecentBooksViewPolicy::countVisible(
      recentBooks, [this](const RecentBook& book) { return isVisibleRecentBook(book); });
  invalidateSpinePageWindow();
  if (visibleBookCount == 0) {
    selectorIndex = 0;
  } else if (selectorIndex >= visibleBookCount) {
    selectorIndex = visibleBookCount - 1;
  }
}

void RecentBooksActivity::logRecentViewState(const char* stage) const {
  const int storeCount = recentBooks.ownsLock() ? static_cast<int>(recentBooks.size()) : RECENT_BOOKS.getCount();
  const char* emptyReason = visibleBookCount != 0 ? "none"
                            : !recentBooks.ownsLock() ? "view_suspended"
                            : storeCount > 0 ? "filtered_view"
                            : RECENT_BOOKS.getLoadResult() == RecentBooksLoadResult::Uncertain ? "load_uncertain"
                                                                                              : "no_records";
  LOG_INF("SHELF", "stage=%s store_result=%s store_records=%u view_records=%u visible=%u selected=%u tab=%u "
                   "empty_reason=%s",
          stage ? stage : "unknown", recentBooksLoadResultName(RECENT_BOOKS.getLoadResult()),
          static_cast<unsigned>(storeCount), static_cast<unsigned>(recentBooks.size()),
          static_cast<unsigned>(visibleBookCount), static_cast<unsigned>(selectorIndex),
          static_cast<unsigned>(activeTab), emptyReason);
}

bool RecentBooksActivity::isVisibleRecentBook(const RecentBook& book) const {
  if (isClippingsExport(book.path)) return false;
  const BookState* state = BOOK_STATES.find(book.path);
  const bool finished = state ? state->status == BookStatus::Finished : book.progressPercent >= 100;
  return RecentBooksViewPolicy::tabIncludes(activeTab, finished);
}

size_t RecentBooksActivity::recentIndexForVisibleIndex(const size_t visibleIndex) const {
  return RecentBooksViewPolicy::rawIndexForVisible(
      recentBooks, visibleIndex, [this](const RecentBook& book) { return isVisibleRecentBook(book); });
}

const RecentBooksActivity::SpinePageWindow& RecentBooksActivity::spinePageForSelection(
    const SpineShelfPlanner::Rect& bounds) {
  const auto* cached = std::get_if<SpinePageWindow>(&spinePageWindow);
  if (cached != nullptr && cached->valid && cached->bounds.x == bounds.x && cached->bounds.y == bounds.y &&
      cached->bounds.width == bounds.width && cached->bounds.height == bounds.height &&
      cached->visualSeed == spineSessionSeed && cached->decorationStyle == SETTINGS.spineDecorationStyle &&
      cached->appearanceMode == SETTINGS.spineAppearanceMode && cached->page.itemCount > 0 &&
      selectorIndex >= cached->page.firstIndex && selectorIndex < cached->page.firstIndex + cached->page.itemCount) {
    return *cached;
  }

  auto& window = spinePageWindow.emplace<SpinePageWindow>();
  window = {};
  window.thumbnailUnavailable.fill(false);
  window.bounds = bounds;
  window.visualSeed = spineSessionSeed;
  window.decorationStyle = SETTINGS.spineDecorationStyle;
  window.appearanceMode = SETTINGS.spineAppearanceMode;
  size_t rawStart = 0;
  size_t visibleStart = 0;
  size_t previousPageStart = 0;
  size_t pageNumber = 0;
  bool found = false;

  while (visibleStart < visibleBookCount && rawStart < recentBooks.size()) {
    std::array<SpineShelfPlanner::Book, SpineShelfPlanner::MAX_BOOKS> candidateBooks{};
    std::array<size_t, SpineShelfPlanner::MAX_BOOKS> candidateIndexes{};
    size_t candidateCount = 0;
    for (size_t raw = rawStart; raw < recentBooks.size() && candidateCount < candidateBooks.size(); ++raw) {
      if (!isVisibleRecentBook(recentBooks[raw])) continue;
      const auto& book = recentBooks[raw];
      candidateIndexes[candidateCount] = raw;
      candidateBooks[candidateCount++] = SpineShelfPlanner::Book{
          book.path.c_str(), book.title.c_str(), book.progressPercent, book.author.c_str(), spineSessionSeed,
          SETTINGS.spineDecorationStyle, false,
          static_cast<SpineShelfPlanner::Appearance>(SETTINGS.spineAppearanceMode), book.coverBmpPath.c_str()};
    }
    if (candidateCount == 0) break;

    const SpineShelfPlanner::Page candidate =
        SpineShelfPlanner::pageForSelection(candidateBooks.data(), candidateCount, 0, bounds);
    if (candidate.itemCount == 0) break;
    ++pageNumber;
    const size_t pageEnd = visibleStart + candidate.itemCount;
    if (!found && selectorIndex >= visibleStart && selectorIndex < pageEnd) {
      window.page = candidate;
      window.page.firstIndex = visibleStart;
      window.pageNumber = pageNumber;
      window.previousPageStart = pageNumber > 1 ? previousPageStart : 0;
      for (size_t i = 0; i < candidateCount; ++i) {
        window.books[i] = candidateBooks[i];
        window.recentIndexes[i] = candidateIndexes[i];
      }
      for (size_t i = 0; i < window.page.itemCount; ++i) {
        window.page.slots[i].itemIndex += visibleStart;
      }
      window.nextPageStart = pageEnd < visibleBookCount ? pageEnd : 0;
      found = true;
    }

    rawStart = candidateIndexes[candidate.itemCount - 1] + 1;
    previousPageStart = visibleStart;
    visibleStart = pageEnd;
  }

  window.pageCount = pageNumber;
  window.valid = true;
  return window;
}

const RecentBooksActivity::TwoRowSpinePageWindow& RecentBooksActivity::twoRowSpinePageForSelection(
    const SpineShelfPlanner::Rect& bounds) {
  const auto* cached = std::get_if<TwoRowSpinePageWindow>(&spinePageWindow);
  if (cached != nullptr && cached->valid && cached->bounds.x == bounds.x && cached->bounds.y == bounds.y &&
      cached->bounds.width == bounds.width && cached->bounds.height == bounds.height &&
      cached->visualSeed == spineSessionSeed && cached->decorationStyle == SETTINGS.spineDecorationStyle &&
      cached->appearanceMode == SETTINGS.spineAppearanceMode && cached->page.itemCount > 0 &&
      selectorIndex >= cached->page.firstIndex && selectorIndex < cached->page.firstIndex + cached->page.itemCount) {
    return *cached;
  }

  auto& window = spinePageWindow.emplace<TwoRowSpinePageWindow>();
  window = {};
  window.thumbnailUnavailable.fill(false);
  window.bounds = bounds;
  window.visualSeed = spineSessionSeed;
  window.decorationStyle = SETTINGS.spineDecorationStyle;
  window.appearanceMode = SETTINGS.spineAppearanceMode;
  size_t rawStart = 0;
  size_t visibleStart = 0;
  size_t previousPageStart = 0;
  size_t pageNumber = 0;
  bool found = false;

  while (visibleStart < visibleBookCount && rawStart < recentBooks.size()) {
    std::array<SpineShelfPlanner::Book, SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY> candidateBooks{};
    std::array<size_t, SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY> candidateIndexes{};
    size_t candidateCount = 0;
    for (size_t raw = rawStart; raw < recentBooks.size() && candidateCount < candidateBooks.size(); ++raw) {
      if (!isVisibleRecentBook(recentBooks[raw])) continue;
      const auto& book = recentBooks[raw];
      candidateIndexes[candidateCount] = raw;
      candidateBooks[candidateCount++] = SpineShelfPlanner::Book{
          book.path.c_str(), book.title.c_str(), book.progressPercent, book.author.c_str(), spineSessionSeed,
          SETTINGS.spineDecorationStyle, true,
          static_cast<SpineShelfPlanner::Appearance>(SETTINGS.spineAppearanceMode), book.coverBmpPath.c_str()};
    }
    if (candidateCount == 0) break;

    const SpineShelfPlanner::TwoRowPage candidate =
        SpineShelfPlanner::twoRowPageForSelection(candidateBooks.data(), candidateCount, 0, bounds);
    if (candidate.itemCount == 0) break;
    ++pageNumber;
    const size_t pageEnd = visibleStart + candidate.itemCount;
    if (!found && selectorIndex >= visibleStart && selectorIndex < pageEnd) {
      window.page = candidate;
      window.page.firstIndex = visibleStart;
      window.pageNumber = pageNumber;
      window.previousPageStart = pageNumber > 1 ? previousPageStart : 0;
      for (size_t i = 0; i < candidateCount; ++i) {
        window.books[i] = candidateBooks[i];
        window.recentIndexes[i] = candidateIndexes[i];
      }
      for (size_t i = 0; i < window.page.itemCount; ++i) {
        window.page.slots[i].itemIndex += visibleStart;
      }
      window.nextPageStart = pageEnd < visibleBookCount ? pageEnd : 0;
      found = true;
    }

    rawStart = candidateIndexes[candidate.itemCount - 1] + 1;
    previousPageStart = visibleStart;
    visibleStart = pageEnd;
  }

  window.pageCount = pageNumber;
  window.valid = true;
  return window;
}

const RecentBooksActivity::ThreeRowSpinePageWindow& RecentBooksActivity::threeRowSpinePageForSelection(
    const SpineShelfPlanner::Rect& bounds) {
  const auto* cached = std::get_if<ThreeRowSpinePageWindow>(&spinePageWindow);
  if (cached != nullptr && cached->valid && cached->bounds.x == bounds.x && cached->bounds.y == bounds.y &&
      cached->bounds.width == bounds.width && cached->bounds.height == bounds.height &&
      cached->visualSeed == spineSessionSeed && cached->decorationStyle == SETTINGS.spineDecorationStyle &&
      cached->appearanceMode == SETTINGS.spineAppearanceMode && cached->page.itemCount > 0 &&
      selectorIndex >= cached->page.firstIndex && selectorIndex < cached->page.firstIndex + cached->page.itemCount) {
    return *cached;
  }

  auto& window = spinePageWindow.emplace<ThreeRowSpinePageWindow>();
  window = {};
  window.thumbnailUnavailable.fill(false);
  window.bounds = bounds;
  window.visualSeed = spineSessionSeed;
  window.decorationStyle = SETTINGS.spineDecorationStyle;
  window.appearanceMode = SETTINGS.spineAppearanceMode;
  size_t rawStart = 0;
  size_t visibleStart = 0;
  size_t previousPageStart = 0;
  size_t pageNumber = 0;
  bool found = false;

  while (visibleStart < visibleBookCount && rawStart < recentBooks.size()) {
    std::array<SpineShelfPlanner::Book, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> candidateBooks{};
    std::array<size_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> candidateIndexes{};
    size_t candidateCount = 0;
    for (size_t raw = rawStart; raw < recentBooks.size() && candidateCount < candidateBooks.size(); ++raw) {
      if (!isVisibleRecentBook(recentBooks[raw])) continue;
      const auto& book = recentBooks[raw];
      candidateIndexes[candidateCount] = raw;
      candidateBooks[candidateCount++] = SpineShelfPlanner::Book{
          book.path.c_str(), book.title.c_str(), book.progressPercent, book.author.c_str(), spineSessionSeed,
          SETTINGS.spineDecorationStyle, true,
          static_cast<SpineShelfPlanner::Appearance>(SETTINGS.spineAppearanceMode), book.coverBmpPath.c_str()};
    }
    if (candidateCount == 0) break;

    const SpineShelfPlanner::ThreeRowPage candidate =
        SpineShelfPlanner::threeRowPageForSelection(candidateBooks.data(), candidateCount, 0, bounds);
    if (candidate.itemCount == 0) break;
    ++pageNumber;
    const size_t pageEnd = visibleStart + candidate.itemCount;
    if (!found && selectorIndex >= visibleStart && selectorIndex < pageEnd) {
      window.page = candidate;
      window.page.firstIndex = visibleStart;
      window.pageNumber = pageNumber;
      window.previousPageStart = pageNumber > 1 ? previousPageStart : 0;
      for (size_t i = 0; i < candidateCount; ++i) {
        window.books[i] = candidateBooks[i];
        window.recentIndexes[i] = candidateIndexes[i];
      }
      for (size_t i = 0; i < window.page.itemCount; ++i) {
        window.page.slots[i].itemIndex += visibleStart;
      }
      window.nextPageStart = pageEnd < visibleBookCount ? pageEnd : 0;
      found = true;
    }
    rawStart = candidateIndexes[candidate.itemCount - 1] + 1;
    previousPageStart = visibleStart;
    visibleStart = pageEnd;
  }

  window.pageCount = pageNumber;
  window.valid = true;
  return window;
}

size_t RecentBooksActivity::moveSpinePage(const FolioShelfLayout& layout, const bool next) {
  const auto bounds = spineShelfBounds(renderer.getScreenWidth(), layout);
  const auto& page = spinePageForSelection(bounds);
  if (page.pageCount <= 1) {
    return next ? static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), visibleBookCount))
                : static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), visibleBookCount));
  }
  return next ? page.nextPageStart : page.previousPageStart;
}

size_t RecentBooksActivity::moveTwoRowSpinePage(const FolioShelfLayout& layout, const bool next) {
  const auto bounds = twoRowSpineBounds(renderer.getScreenWidth(), layout);
  const auto& page = twoRowSpinePageForSelection(bounds);
  if (page.pageCount <= 1) {
    return next ? static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), visibleBookCount))
                : static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), visibleBookCount));
  }
  return next ? page.nextPageStart : page.previousPageStart;
}

size_t RecentBooksActivity::moveThreeRowSpinePage(const FolioShelfLayout& layout, const bool next) {
  const auto bounds = threeRowSpineBounds(renderer.getScreenWidth(), layout);
  const auto& page = threeRowSpinePageForSelection(bounds);
  if (page.pageCount <= 1) {
    return next ? static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), visibleBookCount))
                : static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), visibleBookCount));
  }
  return next ? page.nextPageStart : page.previousPageStart;
}

size_t RecentBooksActivity::selectedRecentIndex() const {
  if (visibleBookCount == 0) return 0;
  const size_t index = recentIndexForVisibleIndex(std::min(selectorIndex, visibleBookCount - 1));
  return index == SIZE_MAX ? 0 : index;
}

uint8_t RecentBooksActivity::activeBookLayout() const {
  if (!isFolioShelfTheme(SETTINGS.uiTheme)) {
    return CrossPointSettings::FOLIO_LAYOUT_GRID;
  }
  if (isFolioNooirLiteTheme(SETTINGS.uiTheme)) {
    const uint8_t liteLayout = activeTab == 2 ? SETTINGS.liteFinishedBookLayout : SETTINGS.liteRecentBookLayout;
    return FolioLiteLayoutPolicy::toFolioLayout(liteLayout);
  }
  const uint8_t layout = activeTab == 2 ? SETTINGS.finishedBookLayout : SETTINGS.recentBookLayout;
  return layout < CrossPointSettings::FOLIO_BOOK_LAYOUT_COUNT ? layout : CrossPointSettings::FOLIO_LAYOUT_GRID;
}

bool RecentBooksActivity::usesCarouselLayout() const {
  const uint8_t layout = activeBookLayout();
  return layout == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL ||
         layout == CrossPointSettings::FOLIO_LAYOUT_FIVE_COVER_CAROUSEL;
}

bool RecentBooksActivity::usesSpineLayout() const {
  return isFolioShelfTheme(SETTINGS.uiTheme) && activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_SPINE;
}

bool RecentBooksActivity::usesTwoRowSpineLayout() const {
  return isFolioShelfTheme(SETTINGS.uiTheme) &&
         activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_TWO_ROW_SPINE;
}

bool RecentBooksActivity::usesThreeRowSpineLayout() const {
  return isFolioNooirLiteTheme(SETTINGS.uiTheme) &&
         activeBookLayout() == FolioLiteLayoutPolicy::FOLIO_LAYOUT_THREE_ROW_SPINE;
}

bool RecentBooksActivity::usesThreeCoverGrid() const {
  return isFolioShelfTheme(SETTINGS.uiTheme) &&
         activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVERS;
}

bool RecentBooksActivity::usesFourByTwoGrid() const {
  return isFolioShelfTheme(SETTINGS.uiTheme) && activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_GRID;
}

bool RecentBooksActivity::usesPersistentHomeSnapshot() const {
  // Folio Nooir and Lite share this single framebuffer snapshot. Theme and
  // layout are both stored as explicit identity fields so a frame from one
  // presentation can never be reused by the other.
  return isFolioShelfTheme(SETTINGS.uiTheme);
}

bool RecentBooksActivity::currentSnapshotPageInfo(size_t& pageStart, size_t& pageItems) {
  if (usesCarouselLayout()) {
    pageItems = HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS;
    pageStart = HomeSnapshotPolicy::pageStartForSelection(selectorIndex, pageItems);
    return pageStart != SIZE_MAX && selectorIndex < visibleBookCount;
  }

  if (usesThreeCoverGrid() || usesFourByTwoGrid()) {
    pageItems = usesThreeCoverGrid() ? HomeSnapshotPolicy::THREE_COVER_PAGE_ITEMS
                                     : HomeSnapshotPolicy::FOUR_BY_TWO_PAGE_ITEMS;
    pageStart = HomeSnapshotPolicy::pageStartForSelection(selectorIndex, pageItems);
    return pageStart != SIZE_MAX;
  }

  if (!usesSpineLayout() && !usesTwoRowSpineLayout() && !usesThreeRowSpineLayout()) return false;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
  FolioShelfLayout layout = homeShelfLayout(folioTheme, renderer, metrics);
  if (isFolioNooirLiteTheme(SETTINGS.uiTheme)) {
    static const CarouselTheme carouselPresentation;
    const CarouselLayout carouselLayout = liteCarouselLayoutCacheValid
                                              ? liteCarouselLayoutCache
                                              : liteCarouselPageLayout(renderer, carouselPresentation, layout);
    layout = liteBookDisplayLayout(layout, carouselLayout, renderer.getScreenWidth());
  }
  if (usesSpineLayout()) {
    const auto& window = spinePageForSelection(spineShelfBounds(renderer.getScreenWidth(), layout));
    pageStart = window.page.firstIndex;
    pageItems = window.page.itemCount;
  } else if (usesTwoRowSpineLayout()) {
    const auto& window = twoRowSpinePageForSelection(twoRowSpineBounds(renderer.getScreenWidth(), layout));
    pageStart = window.page.firstIndex;
    pageItems = window.page.itemCount;
  } else {
    const auto& window = threeRowSpinePageForSelection(threeRowSpineBounds(renderer.getScreenWidth(), layout));
    pageStart = window.page.firstIndex;
    pageItems = window.page.itemCount;
  }
  const size_t pageCapacity = usesThreeRowSpineLayout()
                                  ? SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY
                              : usesTwoRowSpineLayout()
                                  ? SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY
                                  : SpineShelfPlanner::MAX_BOOKS;
  return pageItems > 0 && pageItems <= pageCapacity;
}

int RecentBooksActivity::activePageItems() const {
  if (usesThreeCoverGrid()) return 3;
  if (usesCarouselLayout()) return 1;
  if (usesTwoRowSpineLayout()) return static_cast<int>(SpineShelfPlanner::TWO_ROW_SLOT_CAPACITY);
  if (usesThreeRowSpineLayout()) return static_cast<int>(SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY);
  return BOOKS_PER_PAGE;
}

uint64_t RecentBooksActivity::snapshotKey(const bool exactThemeKey) const {
  uint64_t hash = 1469598103934665603ULL;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  hashBytes(hash, &width, sizeof(width));
  hashBytes(hash, &height, sizeof(height));
  // Version 9 normalized Folio Nooir and Lite. Pass exactThemeKey only for
  // version 8 and the new format; version 9 validation keeps its old key.
  const uint8_t themeKey = exactThemeKey
                               ? SETTINGS.uiTheme
                               : (isFolioShelfTheme(SETTINGS.uiTheme)
                                      ? static_cast<uint8_t>(CrossPointSettings::UI_THEME::FOLIO_NOOIR)
                                      : SETTINGS.uiTheme);
  hashBytes(hash, &themeKey, sizeof(themeKey));
  hashBytes(hash, &SETTINGS.orientation, sizeof(SETTINGS.orientation));
  hashBytes(hash, &SETTINGS.uiScalePercent, sizeof(SETTINGS.uiScalePercent));
  hashBytes(hash, &activeTab, sizeof(activeTab));
  const uint8_t layout = activeBookLayout();
  hashBytes(hash, &layout, sizeof(layout));
  const uint8_t visualSettings[] = {
      SETTINGS.coverProgressStyle,       SETTINGS.featuredShowCover,       SETTINGS.featuredShowFileType,
      SETTINGS.featuredShowAuthor,
      SETTINGS.featuredShowSynopsis,     SETTINGS.featuredShowStatus,     SETTINGS.featuredShowProgress,
      SETTINGS.featuredShowStartedDate,  SETTINGS.featuredShowFinishedDate,
      SETTINGS.featuredShowReadingTime,  SETTINGS.featuredShowSessions,
  };
  hashBytes(hash, visualSettings, sizeof(visualSettings));
  hashBytes(hash, &SETTINGS.language, sizeof(SETTINGS.language));
  hashBytes(hash, &SETTINGS.hideBatteryPercentage, sizeof(SETTINGS.hideBatteryPercentage));
  const bool spineSnapshot = usesSpineLayout() || usesTwoRowSpineLayout() || usesThreeRowSpineLayout();
  if (spineSnapshot) {
    hashBytes(hash, &SETTINGS.spineAppearanceMode, sizeof(SETTINGS.spineAppearanceMode));
    hashBytes(hash, &SETTINGS.spineDecorationStyle, sizeof(SETTINGS.spineDecorationStyle));
    hashBytes(hash, &spineSessionSeed, sizeof(spineSessionSeed));
  }
  const bool patchLiteThreeRowSlots = HomeSnapshotPolicy::threeRowProgressIsDynamic(
      isFolioNooirLiteTheme(SETTINGS.uiTheme), usesThreeRowSpineLayout());
  if (!patchLiteThreeRowSlots) {
    // Other shelf frames encode their complete book list in the structural
    // key. Lite three-row frames instead validate each occupied visible slot
    // from the saved path/visual keys and can repaint only changed faces.
    const size_t bookCount = recentBooks.size();
    hashBytes(hash, &bookCount, sizeof(bookCount));
    for (const auto& book : recentBooks) {
      const uint64_t pathHash = hashText(book.path);
      const uint64_t titleHash = hashText(book.title);
      const uint64_t coverHash = hashText(book.coverBmpPath);
      hashBytes(hash, &pathHash, sizeof(pathHash));
      hashBytes(hash, &titleHash, sizeof(titleHash));
      hashBytes(hash, &coverHash, sizeof(coverHash));
      const uint64_t authorHash = hashText(book.author);
      const uint64_t synopsisHash = hashText(book.synopsis);
      hashBytes(hash, &authorHash, sizeof(authorHash));
      hashBytes(hash, &synopsisHash, sizeof(synopsisHash));
      if (spineSnapshot) {
        // The saved shelf pixels contain book faces, labels, and progress bars.
        // The featured panel and stats strip are redrawn after restore.
        hashBytes(hash, &book.progressPercent, sizeof(book.progressPercent));
        const BookState* state = BOOK_STATES.find(book.path);
        const uint8_t status = state ? static_cast<uint8_t>(state->status) : 0;
        hashBytes(hash, &status, sizeof(status));
        if (state != nullptr) hashBytes(hash, &state->progressPercent, sizeof(state->progressPercent));
      }
      const BookMetadataOverride* override = BOOK_METADATA_OVERRIDES.find(book.path);
      const uint8_t hasOverride = override != nullptr;
      hashBytes(hash, &hasOverride, sizeof(hasOverride));
      if (override != nullptr) {
        const uint64_t overrideTitleHash = hashText(override->title);
        const uint64_t overrideAuthorHash = hashText(override->author);
        const uint64_t overrideSynopsisHash = hashText(override->synopsis);
        hashBytes(hash, &overrideTitleHash, sizeof(overrideTitleHash));
        hashBytes(hash, &overrideAuthorHash, sizeof(overrideAuthorHash));
        hashBytes(hash, &overrideSynopsisHash, sizeof(overrideSynopsisHash));
      }
    }
  }
  return hash;
}

uint64_t RecentBooksActivity::snapshotPresentationKey() const {
  uint64_t hash = 1469598103934665603ULL;
  const auto settings = visualSettingsSignature();
  hashBytes(hash, settings.data(), settings.size());
  const bool spineSnapshot = usesSpineLayout() || usesTwoRowSpineLayout() || usesThreeRowSpineLayout();
  if (spineSnapshot) {
    hashBytes(hash, &spineSessionSeed, sizeof(spineSessionSeed));
  }
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  hashBytes(hash, &width, sizeof(width));
  hashBytes(hash, &height, sizeof(height));
  if (spineSnapshot) {
    // Spine selection changes repaint only the featured panel and its small
    // selection marker. Keep the selected book's visible shelf fields in the
    // key so progress/status changes invalidate the frame, while reading
    // counters remain outside it.
    hashBytes(hash, &visibleBookCount, sizeof(visibleBookCount));
    if (visibleBookCount > 0 && !recentBooks.empty()) {
      const RecentBook& selected = recentBooks[selectedRecentIndex()];
      hashBytes(hash, selected.path.data(), selected.path.size());
      hashBytes(hash, selected.title.data(), selected.title.size());
      hashBytes(hash, selected.author.data(), selected.author.size());
      hashBytes(hash, selected.coverBmpPath.data(), selected.coverBmpPath.size());
      hashBytes(hash, &selected.progressPercent, sizeof(selected.progressPercent));
      const BookState* state = BOOK_STATES.find(selected.path);
      const uint8_t status = state ? static_cast<uint8_t>(state->status) : 0;
      hashBytes(hash, &status, sizeof(status));
      if (state != nullptr) hashBytes(hash, &state->progressPercent, sizeof(state->progressPercent));
    }
    return hash;
  }
  if (visibleBookCount > 0 && !recentBooks.empty()) {
    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    const uint64_t pathHash = hashText(selected.path);
    const uint64_t titleHash = hashText(selected.title);
    const uint64_t authorHash = hashText(selected.author);
    const uint64_t coverHash = hashText(selected.coverBmpPath);
    const uint64_t synopsisHash = hashText(selected.synopsis);
    hashBytes(hash, &pathHash, sizeof(pathHash));
    hashBytes(hash, &titleHash, sizeof(titleHash));
    hashBytes(hash, &authorHash, sizeof(authorHash));
    hashBytes(hash, &coverHash, sizeof(coverHash));
    hashBytes(hash, &synopsisHash, sizeof(synopsisHash));
    const BookMetadataOverride* override = BOOK_METADATA_OVERRIDES.find(selected.path);
    const uint8_t hasOverride = override != nullptr;
    hashBytes(hash, &hasOverride, sizeof(hasOverride));
    if (override != nullptr) {
      const uint64_t overrideTitleHash = hashText(override->title);
      const uint64_t overrideAuthorHash = hashText(override->author);
      const uint64_t overrideSynopsisHash = hashText(override->synopsis);
      hashBytes(hash, &overrideTitleHash, sizeof(overrideTitleHash));
      hashBytes(hash, &overrideAuthorHash, sizeof(overrideAuthorHash));
      hashBytes(hash, &overrideSynopsisHash, sizeof(overrideSynopsisHash));
    }
  }
  return hash;
}

HomeSnapshotPolicy::VisualSettingsSignature RecentBooksActivity::visualSettingsSignature() const {
  const bool spineSnapshot = usesSpineLayout() || usesTwoRowSpineLayout() || usesThreeRowSpineLayout();
  return {
      static_cast<uint8_t>(SETTINGS.uiTheme),
      static_cast<uint8_t>(SETTINGS.orientation),
      SETTINGS.uiScalePercent,
      activeTab,
      activeBookLayout(),
      SETTINGS.coverProgressStyle,
      SETTINGS.featuredShowCover,
      SETTINGS.featuredShowFileType,
      SETTINGS.featuredShowAuthor,
      SETTINGS.featuredShowSynopsis,
      SETTINGS.featuredShowStatus,
      SETTINGS.featuredShowProgress,
      SETTINGS.featuredShowStartedDate,
      SETTINGS.featuredShowFinishedDate,
      SETTINGS.featuredShowReadingTime,
      SETTINGS.featuredShowSessions,
      static_cast<uint8_t>(SETTINGS.language),
      SETTINGS.hideBatteryPercentage,
      static_cast<uint8_t>(spineSnapshot ? SETTINGS.spineAppearanceMode : 0),
      static_cast<uint8_t>(spineSnapshot ? SETTINGS.spineDecorationStyle : 0),
  };
}

uint64_t RecentBooksActivity::bookDynamicKey(const RecentBook& book) const {
  uint64_t hash = 1469598103934665603ULL;
  const uint64_t pathHash = hashText(book.path);
  hashBytes(hash, &pathHash, sizeof(pathHash));
  hashBytes(hash, &book.progressPercent, sizeof(book.progressPercent));
  hashBytes(hash, &book.readingSeconds, sizeof(book.readingSeconds));
  hashBytes(hash, &book.lastSessionSeconds, sizeof(book.lastSessionSeconds));
  hashBytes(hash, &book.dailyReadingSeconds, sizeof(book.dailyReadingSeconds));
  hashBytes(hash, &book.dailyReadingDateKey, sizeof(book.dailyReadingDateKey));
  hashBytes(hash, &book.readingSessions, sizeof(book.readingSessions));
  hashBytes(hash, &book.pagesTurned, sizeof(book.pagesTurned));
  const BookState* state = BOOK_STATES.find(book.path);
  const uint8_t hasState = state != nullptr;
  hashBytes(hash, &hasState, sizeof(hasState));
  if (state != nullptr) {
    hashBytes(hash, &state->status, sizeof(state->status));
    hashBytes(hash, &state->progressPercent, sizeof(state->progressPercent));
    hashBytes(hash, &state->startDate, sizeof(state->startDate));
    hashBytes(hash, &state->finishDate, sizeof(state->finishDate));
    hashBytes(hash, &state->lastOpenedDate, sizeof(state->lastOpenedDate));
    hashBytes(hash, &state->readingSeconds, sizeof(state->readingSeconds));
    hashBytes(hash, &state->readingSessions, sizeof(state->readingSessions));
    hashBytes(hash, &state->pagesTurned, sizeof(state->pagesTurned));
  }
  return hash;
}

uint64_t RecentBooksActivity::bookCoverVisualKey(const RecentBook& book) const {
  uint64_t hash = 1469598103934665603ULL;
  const uint64_t pathHash = hashText(book.path);
  const uint64_t titleHash = hashText(book.title);
  const uint64_t authorHash = hashText(book.author);
  const uint64_t coverHash = hashText(book.coverBmpPath);
  hashBytes(hash, &pathHash, sizeof(pathHash));
  hashBytes(hash, &titleHash, sizeof(titleHash));
  hashBytes(hash, &authorHash, sizeof(authorHash));
  hashBytes(hash, &coverHash, sizeof(coverHash));
  hashBytes(hash, &book.progressPercent, sizeof(book.progressPercent));
  const BookMetadataOverride* override = BOOK_METADATA_OVERRIDES.find(book.path);
  const uint8_t hasOverride = override != nullptr;
  hashBytes(hash, &hasOverride, sizeof(hasOverride));
  if (override != nullptr) {
    const uint64_t overrideTitleHash = hashText(override->title);
    const uint64_t overrideAuthorHash = hashText(override->author);
    hashBytes(hash, &overrideTitleHash, sizeof(overrideTitleHash));
    hashBytes(hash, &overrideAuthorHash, sizeof(overrideAuthorHash));
  }
  return hash;
}

uint64_t RecentBooksActivity::snapshotDynamicKey() {
  uint64_t hash = 1469598103934665603ULL;
  const bool accumulated = !halClock.isAvailable();
  const uint32_t today = halClock.getDateKey();
  hashBytes(hash, &accumulated, sizeof(accumulated));
  hashBytes(hash, &today, sizeof(today));
  const size_t bookCount = recentBooks.size();
  hashBytes(hash, &bookCount, sizeof(bookCount));
  for (const auto& book : recentBooks) {
    const uint64_t pathHash = hashText(book.path);
    hashBytes(hash, &pathHash, sizeof(pathHash));
    hashBytes(hash, &book.progressPercent, sizeof(book.progressPercent));
    hashBytes(hash, &book.readingSeconds, sizeof(book.readingSeconds));
    hashBytes(hash, &book.lastSessionSeconds, sizeof(book.lastSessionSeconds));
    hashBytes(hash, &book.dailyReadingSeconds, sizeof(book.dailyReadingSeconds));
    hashBytes(hash, &book.dailyReadingDateKey, sizeof(book.dailyReadingDateKey));
    hashBytes(hash, &book.readingSessions, sizeof(book.readingSessions));
    hashBytes(hash, &book.pagesTurned, sizeof(book.pagesTurned));
  }
  // Reading Stats can edit a book's Last Read date without changing its
  // structural shelf metadata or RecentBooksStore entry. Keep those dates in
  // the dynamic signature so a resumed Home frame refreshes after that edit.
  const auto& bookStates = BOOK_STATES.getBooks();
  const size_t stateCount = bookStates.size();
  hashBytes(hash, &stateCount, sizeof(stateCount));
  for (const auto& state : bookStates) {
    const uint64_t pathHash = hashText(state.path);
    hashBytes(hash, &pathHash, sizeof(pathHash));
    hashBytes(hash, &state.lastOpenedDate, sizeof(state.lastOpenedDate));
  }
  if (visibleBookCount > 0 && !recentBooks.empty()) {
    const uint64_t selectedKey = bookDynamicKey(recentBooks[selectedRecentIndex()]);
    hashBytes(hash, &selectedKey, sizeof(selectedKey));
  }
  return hash;
}

uint64_t RecentBooksActivity::featuredVisualKey() const {
  uint64_t hash = 1469598103934665603ULL;
  const bool visible = SETTINGS.featuredShowCover != 0;
  hashBytes(hash, &visible, sizeof(visible));
  if (visibleBookCount == 0 || recentBooks.empty()) return hash;
  const RecentBook& selected = recentBooks[selectedRecentIndex()];
  const uint64_t pathHash = hashText(selected.path);
  const uint64_t coverHash = hashText(selected.coverBmpPath);
  hashBytes(hash, &pathHash, sizeof(pathHash));
  hashBytes(hash, &coverHash, sizeof(coverHash));
  return hash;
}

void RecentBooksActivity::captureSnapshotVisualState(const size_t pageStart, const size_t pageItems) {
  snapshotBookPathKeys.fill(0);
  snapshotBookVisualKeys.fill(0);
  size_t visibleIndex = 0;
  const size_t pageEnd = pageStart + pageItems;
  for (const auto& book : recentBooks) {
    if (!isVisibleRecentBook(book)) continue;
    if (visibleIndex >= pageStart && visibleIndex < pageEnd) {
      const size_t slot = visibleIndex - pageStart;
      snapshotBookPathKeys[slot] = hashText(book.path);
      snapshotBookVisualKeys[slot] = bookCoverVisualKey(book);
    }
    if (++visibleIndex >= pageEnd) break;
  }
  snapshotBookGeometryAvailable = captureSnapshotRects(pageStart, pageItems, snapshotBookRects);
  snapshotDynamicNeedsPatch = false;
}

bool RecentBooksActivity::captureSnapshotRects(
    const size_t pageStart, const size_t pageItems,
    std::array<HomeSnapshotPolicy::SnapshotRect, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY>& rects) {
  rects.fill({});
  if (!usesThreeRowSpineLayout() || pageItems == 0 ||
      pageItems > SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY) {
    return false;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
  FolioShelfLayout layout = homeShelfLayout(folioTheme, renderer, metrics);
  if (isFolioNooirLiteTheme(SETTINGS.uiTheme)) {
    static const CarouselTheme carouselPresentation;
    const CarouselLayout carouselLayout = liteCarouselLayoutCacheValid
                                              ? liteCarouselLayoutCache
                                              : liteCarouselPageLayout(renderer, carouselPresentation, layout);
    layout = liteBookDisplayLayout(layout, carouselLayout, renderer.getScreenWidth());
  }
  const auto bounds = threeRowSpineBounds(renderer.getScreenWidth(), layout);
  const auto& window = threeRowSpinePageForSelection(bounds);
  if (window.page.firstIndex != pageStart || window.page.itemCount != pageItems) return false;

  size_t recorded = 0;
  for (size_t slotIndex = 0; slotIndex < window.page.itemCount; ++slotIndex) {
    const auto& slot = window.page.slots[slotIndex];
    if (slot.itemIndex < pageStart || slot.itemIndex - pageStart >= rects.size()) return false;
    const auto& rect = slot.rect;
    if (rect.width <= 0 || rect.height <= 0 ||
        rect.x < std::numeric_limits<int16_t>::min() || rect.x > std::numeric_limits<int16_t>::max() ||
        rect.y < std::numeric_limits<int16_t>::min() || rect.y > std::numeric_limits<int16_t>::max() ||
        rect.width > std::numeric_limits<int16_t>::max() || rect.height > std::numeric_limits<int16_t>::max()) {
      return false;
    }
    const size_t localIndex = slot.itemIndex - pageStart;
    rects[localIndex] = {static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
                         static_cast<int16_t>(rect.width), static_cast<int16_t>(rect.height)};
    ++recorded;
  }
  return recorded == pageItems;
}

void RecentBooksActivity::drawSnapshotSelectionOutline(const FolioShelfLayout& layout, const size_t index,
                                                       const bool selected) {
  if (index >= visibleBookCount) return;
  if (usesSpineLayout()) {
    const auto bounds = spineShelfBounds(renderer.getScreenWidth(), layout);
    const auto& window = spinePageForSelection(bounds);
    drawSpinePageSelectionMarker(renderer, window.page, index, selected, renderedSpineFocus);
    return;
  }
  if (usesTwoRowSpineLayout()) {
    const auto bounds = twoRowSpineBounds(renderer.getScreenWidth(), layout);
    const auto& window = twoRowSpinePageForSelection(bounds);
    drawTwoRowSpinePageSelectionMarker(renderer, window.page, index, selected, renderedSpineFocus);
    return;
  }
  if (usesThreeRowSpineLayout()) {
    const auto bounds = threeRowSpineBounds(renderer.getScreenWidth(), layout);
    const auto& window = threeRowSpinePageForSelection(bounds);
    drawThreeRowSpinePageSelectionMarker(renderer, window.page, index, selected, renderedSpineFocus);
    return;
  }
  if (usesThreeCoverGrid()) {
    constexpr int gap = 10;
    constexpr int columns = 3;
    const int pageWidth = renderer.getScreenWidth();
    const int cardWidth = (pageWidth - gap * (columns + 1)) / columns;
    const int coverHeight = std::min(layout.gridHeight - 20, cardWidth * 3 / 2);
    const size_t slot = index % HomeSnapshotPolicy::THREE_COVER_PAGE_ITEMS;
    const int x = gap + static_cast<int>(slot) * (cardWidth + gap);
    const int y = layout.gridTop + std::max(0, (layout.gridHeight - coverHeight) / 2);
    renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6, selected);
    return;
  }
  if (!usesFourByTwoGrid()) return;

  const int columns = std::max(1, layout.columns);
  const int gap = layout.gridGap;
  const int cardWidth = layout.cardWidth;
  const int cardHeight = layout.cardHeight;
  const size_t slot = index % HomeSnapshotPolicy::FOUR_BY_TWO_PAGE_ITEMS;
  const int x = gap + static_cast<int>(slot % static_cast<size_t>(columns)) * (cardWidth + gap);
  const int y = layout.gridTop + static_cast<int>(slot / static_cast<size_t>(columns)) * cardHeight;
  const int coverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
  renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6, selected);
}

bool RecentBooksActivity::restoreSnapshot() {
  snapshotRewriteAfterRestore = false;
  snapshotRestoreFailureReason = "unknown";
  const auto invalidate = [this](const char* reason) {
    snapshotRestoreFailureReason = reason;
    LOG_INF("SHELF", "stage=snapshot_restore_invalidated reason=%s", reason);
    return false;
  };
  if (!usesPersistentHomeSnapshot()) return invalidate("unsupported_theme");
  if (!renderer.hasFrameBuffer()) return invalidate("framebuffer_unavailable");
  HalFile file;
  if (!Storage.openFileForRead("SHELF", FOLIO_HOME_SNAPSHOT, file)) return invalidate("snapshot_missing");

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t dataSize = 0;
  uint8_t pageItems = 0;
  HomeSnapshotPolicy::SnapshotIdentity savedIdentity{};
  uint64_t key = 0;
  uint32_t pageStart = 0;
  uint32_t frameSelectorIndex = 0;
  uint32_t restoredSelectorIndex = 0;
  uint8_t restoredTab = 0;
  uint32_t restoredVisibleBookCount = 0;
  uint64_t dynamicKey = 0;
  uint64_t savedFeaturedKey = 0;
  std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> bookPathKeys{};
  std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> bookVisualKeys{};
  std::array<HomeSnapshotPolicy::SnapshotRect, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> bookRects{};
  if (file.read(&magic, sizeof(magic)) != sizeof(magic) || file.read(&version, sizeof(version)) != sizeof(version) ||
      file.read(&width, sizeof(width)) != sizeof(width) || file.read(&height, sizeof(height)) != sizeof(height) ||
      file.read(&dataSize, sizeof(dataSize)) != sizeof(dataSize) || file.read(&pageItems, sizeof(pageItems)) != sizeof(pageItems) ||
      magic != FOLIO_HOME_MAGIC || version < FOLIO_HOME_OLDEST_VERSION || version > FOLIO_HOME_VERSION) {
    return invalidate("bad_header_or_version");
  }
  const bool hasExtendedHeader = version >= FOLIO_HOME_PREVIOUS_VERSION;
  const bool legacySnapshotCompatible = !hasExtendedHeader &&
                                        SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR;
  if ((!hasExtendedHeader && !legacySnapshotCompatible) || width != renderer.getScreenWidth() ||
      height != renderer.getScreenHeight() || dataSize != renderer.getBufferSize() ||
      !HomeSnapshotPolicy::isSupportedPageCapacity(pageItems) ||
      (!hasExtendedHeader && pageItems > FOLIO_HOME_LEGACY_PAGE_CAPACITY)) {
    return invalidate("geometry_or_legacy_capacity_mismatch");
  }
  if (hasExtendedHeader) {
    if (file.read(&savedIdentity.theme, sizeof(savedIdentity.theme)) != sizeof(savedIdentity.theme) ||
        file.read(&savedIdentity.layout, sizeof(savedIdentity.layout)) != sizeof(savedIdentity.layout) ||
        savedIdentity.theme != SETTINGS.uiTheme) {
      return invalidate("theme_identity_mismatch");
    }
  }
  if (file.read(&key, sizeof(key)) != sizeof(key) ||
      file.read(&pageStart, sizeof(pageStart)) != sizeof(pageStart) ||
      (hasExtendedHeader &&
       file.read(&frameSelectorIndex, sizeof(frameSelectorIndex)) != sizeof(frameSelectorIndex)) ||
      file.read(&restoredSelectorIndex, sizeof(restoredSelectorIndex)) != sizeof(restoredSelectorIndex) ||
      file.read(&restoredTab, sizeof(restoredTab)) != sizeof(restoredTab) ||
      file.read(&restoredVisibleBookCount, sizeof(restoredVisibleBookCount)) != sizeof(restoredVisibleBookCount) ||
      file.read(&dynamicKey, sizeof(dynamicKey)) != sizeof(dynamicKey) ||
      file.read(&savedFeaturedKey, sizeof(savedFeaturedKey)) != sizeof(savedFeaturedKey)) {
    return invalidate("truncated_metadata");
  }
  if (!hasExtendedHeader) frameSelectorIndex = restoredSelectorIndex;
  if (hasExtendedHeader) {
    if (file.read(bookPathKeys.data(), sizeof(bookPathKeys)) != sizeof(bookPathKeys) ||
        file.read(bookVisualKeys.data(), sizeof(bookVisualKeys)) != sizeof(bookVisualKeys)) {
      return invalidate("truncated_slot_keys");
    }
    if (version == FOLIO_HOME_VERSION &&
        file.read(bookRects.data(), sizeof(bookRects)) != sizeof(bookRects)) {
      return invalidate("truncated_slot_geometry");
    }
  } else {
    std::array<uint64_t, FOLIO_HOME_LEGACY_PAGE_CAPACITY> legacyPathKeys{};
    std::array<uint64_t, FOLIO_HOME_LEGACY_PAGE_CAPACITY> legacyVisualKeys{};
    if (file.read(legacyPathKeys.data(), sizeof(legacyPathKeys)) != sizeof(legacyPathKeys) ||
        file.read(legacyVisualKeys.data(), sizeof(legacyVisualKeys)) != sizeof(legacyVisualKeys)) {
      return invalidate("truncated_legacy_slot_keys");
    }
    std::copy(legacyPathKeys.begin(), legacyPathKeys.end(), bookPathKeys.begin());
    std::copy(legacyVisualKeys.begin(), legacyVisualKeys.end(), bookVisualKeys.begin());
  }
  if (restoredTab != 1 && restoredTab != 2) return invalidate("invalid_saved_tab");
  const uint8_t originalTab = activeTab;
  activeTab = restoredTab;
  if (hasExtendedHeader &&
      !HomeSnapshotPolicy::identityMatches(
          savedIdentity, HomeSnapshotPolicy::SnapshotIdentity{SETTINGS.uiTheme, activeBookLayout()})) {
    activeTab = originalTab;
    return invalidate("layout_identity_mismatch");
  }
  rebuildVisibleBooks();
  const size_t savedVisiblePageItems =
      HomeSnapshotPolicy::visiblePageItemCount(pageStart, pageItems, restoredVisibleBookCount);
  const bool pageWithinSavedRange = savedVisiblePageItems > 0;
  const bool liteThreeRowLayout = HomeSnapshotPolicy::threeRowProgressIsDynamic(
      isFolioNooirLiteTheme(SETTINGS.uiTheme), usesThreeRowSpineLayout());
  bool savedGeometryValid = version == FOLIO_HOME_VERSION && pageWithinSavedRange;
  if (savedGeometryValid) {
    for (size_t slot = 0; slot < savedVisiblePageItems; ++slot) {
      if (bookPathKeys[slot] == 0 || !bookRects[slot].valid()) {
        savedGeometryValid = false;
        break;
      }
    }
  }
  const size_t restoredPageEnd = pageWithinSavedRange ? static_cast<size_t>(pageStart) + savedVisiblePageItems : 0;
  const bool savedSelectedPathKeyValid = restoredSelectorIndex >= pageStart &&
                                         restoredSelectorIndex < restoredPageEnd &&
                                         restoredSelectorIndex - pageStart < bookPathKeys.size() &&
                                         bookPathKeys[restoredSelectorIndex - pageStart] != 0;
  const uint64_t savedSelectedPathKey = savedSelectedPathKeyValid
                                            ? bookPathKeys[restoredSelectorIndex - pageStart]
                                            : 0;
  bool selectedPathFound = false;
  selectorIndex = restoredSelectorIndex;
  size_t visibleIndex = 0;
  for (const auto& book : recentBooks) {
    if (!isVisibleRecentBook(book)) continue;
    if (savedSelectedPathKeyValid && savedSelectedPathKey == hashText(book.path)) {
      selectorIndex = visibleIndex;
      selectedPathFound = true;
    }
    ++visibleIndex;
  }
  if (!selectedPathFound || selectorIndex >= visibleBookCount) {
    selectorIndex = visibleBookCount == 0 ? 0 : std::min<size_t>(restoredSelectorIndex, visibleBookCount - 1);
  }
  size_t currentPageStart = SIZE_MAX;
  size_t currentPageItems = 0;
  std::array<HomeSnapshotPolicy::SnapshotRect, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentRects{};
  std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentPathKeys{};
  const bool currentPageKnown = currentSnapshotPageInfo(currentPageStart, currentPageItems);
  const bool currentGeometryAvailable = liteThreeRowLayout && currentPageKnown &&
                                        captureSnapshotRects(currentPageStart, currentPageItems, currentRects);
  if (liteThreeRowLayout && currentPageKnown) {
    size_t index = 0;
    for (const auto& book : recentBooks) {
      if (!isVisibleRecentBook(book)) continue;
      if (index >= currentPageStart && index - currentPageStart < currentPageItems) {
        currentPathKeys[index - currentPageStart] = hashText(book.path);
      }
      ++index;
    }
  }
  const bool canPatchMovedThreeRowPage = HomeSnapshotPolicy::canPatchLiteThreeRowPage(
      liteThreeRowLayout && version == FOLIO_HOME_VERSION, savedGeometryValid, savedVisiblePageItems,
      currentPageItems) && currentGeometryAvailable;
  const bool sameThreeRowPageStructure = HomeSnapshotPolicy::threeRowPageStructureMatches(
      liteThreeRowLayout && version == FOLIO_HOME_VERSION, currentPageKnown && currentPageStart == pageStart,
      savedVisiblePageItems, currentPageItems, bookPathKeys, currentPathKeys, bookRects, currentRects);
  if (canPatchMovedThreeRowPage && !sameThreeRowPageStructure) {
    // Changed book identities, page occupancy, or packed slot geometry also
    // moves row furniture and plant positions. Let the ordinary full Lite
    // renderer clear and rebuild this frame instead of patching faces alone.
    return invalidate("lite_three_row_structure_changed");
  }
  bool sameVisiblePage = restoredVisibleBookCount == visibleBookCount && pageWithinSavedRange;
  if (liteThreeRowLayout && version == FOLIO_HOME_PREVIOUS_VERSION) {
    sameVisiblePage = sameVisiblePage && currentPageKnown && currentPageStart == pageStart &&
                      currentPageItems == pageItems && savedVisiblePageItems == currentPageItems;
    if (sameVisiblePage) {
      size_t index = 0;
      for (const auto& book : recentBooks) {
        if (!isVisibleRecentBook(book)) continue;
        if (index >= pageStart && index - pageStart < savedVisiblePageItems &&
            bookPathKeys[index - pageStart] != hashText(book.path)) {
          sameVisiblePage = false;
          break;
        }
        ++index;
      }
    }
  }
  if (canPatchMovedThreeRowPage) sameVisiblePage = true;
  const bool keyMatches = key == snapshotKey(version != FOLIO_HOME_LEGACY_VERSION);
  const bool pageRangeValid = canPatchMovedThreeRowPage ||
                              HomeSnapshotPolicy::pageRangeMatches(frameSelectorIndex, pageStart, pageItems,
                                                                   visibleBookCount);
  const auto invalidationReason = HomeSnapshotPolicy::classifySnapshotInvalidation(
      keyMatches, sameVisiblePage && pageRangeValid, liteThreeRowLayout && version == FOLIO_HOME_VERSION,
      canPatchMovedThreeRowPage, selectorIndex < visibleBookCount);
  if (invalidationReason != HomeSnapshotPolicy::SnapshotInvalidationReason::None) {
    activeTab = originalTab;
    rebuildVisibleBooks();
    selectorIndex = 0;
    return invalidate(HomeSnapshotPolicy::snapshotInvalidationReasonName(invalidationReason));
  }
  if (!currentPageKnown || (!canPatchMovedThreeRowPage &&
                            (currentPageStart != pageStart || currentPageItems != pageItems))) {
    // Navigation may move beyond the saved frame while the activity is open.
    // Keep the persisted tab and selection, but repaint this frame instead of
    // restoring pixels from another page/layout position.
    if (hasExtendedHeader) {
      return invalidate(HomeSnapshotPolicy::snapshotInvalidationReasonName(
          HomeSnapshotPolicy::SnapshotInvalidationReason::CurrentPageGeometryMismatch));
    }
    activeTab = originalTab;
    rebuildVisibleBooks();
    selectorIndex = 0;
    return invalidate(HomeSnapshotPolicy::snapshotInvalidationReasonName(
        HomeSnapshotPolicy::SnapshotInvalidationReason::CurrentPageGeometryMismatch));
  }
  // The selected book is part of the dynamic signature because the featured
  // panel can change when navigation state is saved without rewriting pixels.
  // Compare against the persisted selection, not the transient pre-restore
  // selector left by the previous activity instance.
  const auto restoreAction = HomeSnapshotPolicy::classifyRestore(
      key, snapshotKey(version != FOLIO_HOME_LEGACY_VERSION), dynamicKey, snapshotDynamicKey());
  std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentVisualKeys{};
  bool anyVisibleSlotChanged = false;
  if (liteThreeRowLayout && currentPageKnown) {
    size_t index = 0;
    for (const auto& book : recentBooks) {
      if (!isVisibleRecentBook(book)) continue;
      if (index >= currentPageStart && index - currentPageStart < currentPageItems) {
        const size_t slot = index - currentPageStart;
        currentVisualKeys[slot] = bookCoverVisualKey(book);
        if (slot >= bookPathKeys.size() || bookPathKeys[slot] != currentPathKeys[slot] ||
            bookVisualKeys[slot] != currentVisualKeys[slot]) {
          anyVisibleSlotChanged = true;
        }
      }
      ++index;
    }
    if (canPatchMovedThreeRowPage) {
      for (size_t slot = 0; slot < currentPageItems; ++slot) {
        if (HomeSnapshotPolicy::slotNeedsPatch(bookPathKeys[slot], currentPathKeys[slot],
                                               bookVisualKeys[slot], currentVisualKeys[slot],
                                               bookRects[slot], currentRects[slot])) {
          anyVisibleSlotChanged = true;
        }
      }
      for (size_t slot = currentPageItems; slot < savedVisiblePageItems; ++slot) {
        if (bookPathKeys[slot] != 0) anyVisibleSlotChanged = true;
      }
    }
  }
  const bool loaded = file.read(renderer.getFrameBuffer(), dataSize) == static_cast<int>(dataSize);
  if (loaded) {
    snapshotPageStart = currentPageStart;
    snapshotPageItems = currentPageItems;
    snapshotSelectorIndex = selectorIndex;
    snapshotPresentationKeyValue = snapshotPresentationKey();
    snapshotBookPathKeys = bookPathKeys;
    snapshotBookVisualKeys = bookVisualKeys;
    snapshotBookRects = bookRects;
    snapshotBookGeometryAvailable = savedGeometryValid;
    if (liteThreeRowLayout && version == FOLIO_HOME_PREVIOUS_VERSION && sameVisiblePage &&
        captureSnapshotRects(currentPageStart, currentPageItems, snapshotBookRects)) {
      snapshotBookGeometryAvailable = true;
    }
    const bool featuredChanged = savedFeaturedKey != featuredVisualKey();
    snapshotDynamicNeedsPatch = restoreAction == HomeSnapshotPolicy::RestoreAction::PatchDynamic || featuredChanged ||
                                anyVisibleSlotChanged || restoredVisibleBookCount != visibleBookCount ||
                                currentPageStart != pageStart || currentPageItems != pageItems;
    snapshotRewriteAfterRestore = snapshotDynamicNeedsPatch;
    spineShelfVisualsDirty = false;
    liteSelectedDetailsDirty = false;
    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    lastFeaturedPath = featuredChanged ? std::string() : selected.path;
    lastFeaturedCoverPath = featuredChanged ? std::string() : selected.coverBmpPath;
    lastFeaturedCoverVisible = !featuredChanged && SETTINGS.featuredShowCover != 0;
    LOG_DBG("SHELF", "Restored %u-byte Home snapshot", dataSize);
    logRecentViewState("snapshot_restored");
  } else {
    snapshotRewriteAfterRestore = false;
    if (version != FOLIO_HOME_VERSION) {
      activeTab = originalTab;
      rebuildVisibleBooks();
      selectorIndex = 0;
    }
    return invalidate("framebuffer_read_failed");
  }
  return loaded;
}

bool RecentBooksActivity::restoreSnapshotAfterOverlayDismissal() {
  const uint8_t tabBeforeRestore = activeTab;
  const size_t selectionBeforeRestore = selectorIndex;
  const bool selectedPathWasValid = visibleBookCount > 0 && selectedRecentIndex() < recentBooks.size();
  const uint64_t selectedPathBeforeRestore = selectedPathWasValid
                                                ? hashText(recentBooks[selectedRecentIndex()].path)
                                                : 0;
  const bool restored = restoreSnapshot();
  if (!restored) {
    if (activeTab != tabBeforeRestore) {
      activeTab = tabBeforeRestore;
      rebuildVisibleBooks();
    }
    selectorIndex = visibleBookCount == 0 ? 0 : std::min(selectionBeforeRestore, visibleBookCount - 1);
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    snapshotSelectorIndex = SIZE_MAX;
    snapshotDynamicNeedsPatch = false;
    snapshotRewriteAfterRestore = false;
    return false;
  }
  if (activeTab != tabBeforeRestore) {
    activeTab = tabBeforeRestore;
    rebuildVisibleBooks();
    selectorIndex = visibleBookCount == 0 ? 0 : std::min(selectionBeforeRestore, visibleBookCount - 1);
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    snapshotSelectorIndex = SIZE_MAX;
    snapshotDynamicNeedsPatch = false;
    snapshotRewriteAfterRestore = false;
    return false;
  }

  const uint64_t snapshotSelectedPath = visibleBookCount > 0 && selectedRecentIndex() < recentBooks.size()
                                            ? hashText(recentBooks[selectedRecentIndex()].path)
                                            : 0;
  bool selectedPathFound = false;
  size_t visibleIndex = 0;
  for (const auto& book : recentBooks) {
    if (!isVisibleRecentBook(book)) continue;
    if (selectedPathWasValid && hashText(book.path) == selectedPathBeforeRestore) {
      selectorIndex = visibleIndex;
      selectedPathFound = true;
    }
    ++visibleIndex;
  }
  if (!selectedPathFound || selectorIndex >= visibleBookCount) {
    selectorIndex = visibleBookCount == 0 ? 0 : std::min(selectionBeforeRestore, visibleBookCount - 1);
  }

  size_t currentPageStart = SIZE_MAX;
  size_t currentPageItems = 0;
  if (!currentSnapshotPageInfo(currentPageStart, currentPageItems) || currentPageStart != snapshotPageStart ||
      currentPageItems != snapshotPageItems) {
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    snapshotSelectorIndex = SIZE_MAX;
    snapshotDynamicNeedsPatch = false;
    snapshotRewriteAfterRestore = false;
    return false;
  }
  if (selectedPathWasValid && snapshotSelectedPath != selectedPathBeforeRestore) {
    snapshotDynamicNeedsPatch = true;
    snapshotRewriteAfterRestore = true;
  }
  // A verified disk snapshot supersedes any deferred write for the overlayed
  // framebuffer. Dynamic changes are queued again after their targeted patch.
  snapshotWritePending = false;
  snapshotWriteRequestedMs = 0;
  snapshotSelectorIndex = selectorIndex;
  snapshotPresentationKeyValue = snapshotPresentationKey();
  snapshotFastPathUsed = false;
  initialRenderPending = true;
  lastRenderedSelectorIndex = SIZE_MAX;
  lastRenderedPageStart = SIZE_MAX;
  lastRenderedTab = 0;
  return true;
}

void RecentBooksActivity::writeSnapshot() {
  if (!usesPersistentHomeSnapshot() || !renderer.hasFrameBuffer()) return;
  size_t pageStart = SIZE_MAX;
  size_t pageItemsValue = 0;
  if (!currentSnapshotPageInfo(pageStart, pageItemsValue) ||
      pageItemsValue > SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY || pageStart != snapshotPageStart ||
      pageItemsValue != snapshotPageItems) return;
  captureSnapshotVisualState(pageStart, pageItemsValue);
  if (HomeSnapshotPolicy::threeRowProgressIsDynamic(isFolioNooirLiteTheme(SETTINGS.uiTheme),
                                                     usesThreeRowSpineLayout()) &&
      !snapshotBookGeometryAvailable) {
    LOG_INF("SHELF", "stage=snapshot_write_skipped reason=slot_geometry_unavailable");
    return;
  }
  Storage.ensureDirectoryExists("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("SHELF", FOLIO_HOME_SNAPSHOT, file)) return;
  const uint32_t magic = FOLIO_HOME_MAGIC;
  const uint16_t version = FOLIO_HOME_VERSION;
  const uint16_t width = static_cast<uint16_t>(renderer.getScreenWidth());
  const uint16_t height = static_cast<uint16_t>(renderer.getScreenHeight());
  const uint32_t dataSize = renderer.getBufferSize();
  const uint8_t pageItems = static_cast<uint8_t>(pageItemsValue);
  const uint8_t savedTheme = SETTINGS.uiTheme;
  const uint8_t savedLayout = activeBookLayout();
  const uint64_t key = snapshotKey(true);
  const uint32_t savedPageStart = static_cast<uint32_t>(pageStart);
  const uint32_t savedFrameSelectorIndex = static_cast<uint32_t>(selectorIndex);
  const uint64_t dynamicKey = snapshotDynamicKey();
  const uint32_t savedSelectorIndex = static_cast<uint32_t>(selectorIndex);
  const uint8_t savedTab = activeTab;
  const uint32_t savedVisibleBookCount = static_cast<uint32_t>(visibleBookCount);
  const uint64_t savedFeaturedKey = featuredVisualKey();
  const bool ok = file.write(&magic, sizeof(magic)) == sizeof(magic) &&
                  file.write(&version, sizeof(version)) == sizeof(version) &&
                  file.write(&width, sizeof(width)) == sizeof(width) &&
                  file.write(&height, sizeof(height)) == sizeof(height) &&
                  file.write(&dataSize, sizeof(dataSize)) == sizeof(dataSize) &&
                  file.write(&pageItems, sizeof(pageItems)) == sizeof(pageItems) &&
                  file.write(&savedTheme, sizeof(savedTheme)) == sizeof(savedTheme) &&
                  file.write(&savedLayout, sizeof(savedLayout)) == sizeof(savedLayout) &&
                  file.write(&key, sizeof(key)) == sizeof(key) &&
                  file.write(&savedPageStart, sizeof(savedPageStart)) == sizeof(savedPageStart) &&
                  file.write(&savedFrameSelectorIndex, sizeof(savedFrameSelectorIndex)) == sizeof(savedFrameSelectorIndex) &&
                  file.write(&savedSelectorIndex, sizeof(savedSelectorIndex)) == sizeof(savedSelectorIndex) &&
                  file.write(&savedTab, sizeof(savedTab)) == sizeof(savedTab) &&
                  file.write(&savedVisibleBookCount, sizeof(savedVisibleBookCount)) == sizeof(savedVisibleBookCount) &&
                  file.write(&dynamicKey, sizeof(dynamicKey)) == sizeof(dynamicKey) &&
                  file.write(&savedFeaturedKey, sizeof(savedFeaturedKey)) == sizeof(savedFeaturedKey) &&
                  file.write(snapshotBookPathKeys.data(), sizeof(snapshotBookPathKeys)) == sizeof(snapshotBookPathKeys) &&
                  file.write(snapshotBookVisualKeys.data(), sizeof(snapshotBookVisualKeys)) == sizeof(snapshotBookVisualKeys) &&
                  file.write(snapshotBookRects.data(), sizeof(snapshotBookRects)) == sizeof(snapshotBookRects) &&
                  file.write(renderer.getFrameBuffer(), dataSize) == dataSize;
  if (ok) {
    snapshotPageStart = savedPageStart;
    snapshotPageItems = pageItems;
    snapshotSelectorIndex = selectorIndex;
    snapshotPresentationKeyValue = snapshotPresentationKey();
    snapshotDynamicNeedsPatch = false;
    snapshotRewriteAfterRestore = false;
  } else {
    LOG_ERR("SHELF", "Failed to write Home snapshot");
  }
}

void RecentBooksActivity::writeSnapshotNavigationState() {
  if (!usesPersistentHomeSnapshot()) return;
  size_t currentPageStart = SIZE_MAX;
  size_t currentPageItems = 0;
  if (!currentSnapshotPageInfo(currentPageStart, currentPageItems)) return;
  HalFile file = Storage.open(FOLIO_HOME_SNAPSHOT, O_RDWR);
  if (!file) return;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t dataSize = 0;
  uint8_t pageItems = 0;
  HomeSnapshotPolicy::SnapshotIdentity savedIdentity{};
  uint64_t key = 0;
  if (file.read(&magic, sizeof(magic)) != sizeof(magic) || file.read(&version, sizeof(version)) != sizeof(version) ||
      file.read(&width, sizeof(width)) != sizeof(width) || file.read(&height, sizeof(height)) != sizeof(height) ||
      file.read(&dataSize, sizeof(dataSize)) != sizeof(dataSize) || file.read(&pageItems, sizeof(pageItems)) != sizeof(pageItems) ||
      magic != FOLIO_HOME_MAGIC || width != renderer.getScreenWidth() ||
      height != renderer.getScreenHeight() || dataSize != renderer.getBufferSize() ||
      !HomeSnapshotPolicy::isSupportedPageCapacity(pageItems) ||
      version < FOLIO_HOME_OLDEST_VERSION || version > FOLIO_HOME_VERSION ||
      (version < FOLIO_HOME_PREVIOUS_VERSION && pageItems > FOLIO_HOME_LEGACY_PAGE_CAPACITY)) {
    return;
  }
  const bool hasExtendedHeader = version >= FOLIO_HOME_PREVIOUS_VERSION;
  const bool legacySnapshotCompatible = !hasExtendedHeader &&
                                        SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR;
  if (hasExtendedHeader &&
      (file.read(&savedIdentity.theme, sizeof(savedIdentity.theme)) != sizeof(savedIdentity.theme) ||
       file.read(&savedIdentity.layout, sizeof(savedIdentity.layout)) != sizeof(savedIdentity.layout) ||
       !HomeSnapshotPolicy::identityMatches(
           savedIdentity, HomeSnapshotPolicy::SnapshotIdentity{SETTINGS.uiTheme, activeBookLayout()}))) {
    return;
  }
  if ((!hasExtendedHeader && !legacySnapshotCompatible) ||
      file.read(&key, sizeof(key)) != sizeof(key) ||
      key != snapshotKey(version != FOLIO_HOME_LEGACY_VERSION)) {
    return;
  }
  uint32_t storedPageStart = 0;
  uint32_t frameSelectorIndex = 0;
  if (file.read(&storedPageStart, sizeof(storedPageStart)) != sizeof(storedPageStart) ||
      (hasExtendedHeader &&
       file.read(&frameSelectorIndex, sizeof(frameSelectorIndex)) != sizeof(frameSelectorIndex))) {
    return;
  }
  if (!hasExtendedHeader) frameSelectorIndex = selectorIndex;
  const bool validFramePage = HomeSnapshotPolicy::pageRangeMatches(
      frameSelectorIndex, storedPageStart, pageItems, visibleBookCount);
  const bool sameActivePage = storedPageStart == currentPageStart && pageItems == currentPageItems;
  if (!validFramePage || selectorIndex >= visibleBookCount ||
      (!hasExtendedHeader && !sameActivePage)) return;

  const size_t selectorOffset = sizeof(magic) + sizeof(version) + sizeof(width) + sizeof(height) + sizeof(dataSize) +
                                sizeof(pageItems) + (hasExtendedHeader ? 2U : 0U) * sizeof(uint8_t) +
                                sizeof(key) + (hasExtendedHeader ? 2U : 1U) * sizeof(uint32_t);
  if (!file.seek(selectorOffset)) return;
  const uint32_t savedSelectorIndex = static_cast<uint32_t>(selectorIndex);
  const uint8_t savedTab = activeTab;
  if (file.write(&savedSelectorIndex, sizeof(savedSelectorIndex)) != sizeof(savedSelectorIndex) ||
      file.write(&savedTab, sizeof(savedTab)) != sizeof(savedTab)) {
    LOG_ERR("SHELF", "Failed to update Home snapshot navigation");
    return;
  }
  LOG_DBG("SHELF", "Updated Home snapshot selection=%u tab=%u", static_cast<unsigned>(selectorIndex),
          static_cast<unsigned>(activeTab));
}

void RecentBooksActivity::generateNextCover() {
  if (coverGenerationActive) return;
  coverGenerationActive = true;
  // Source metadata and covers are rebuilt only after the user explicitly
  // chooses Refresh Book Cache. Ordinary Recent/Home visits consume persisted
  // metadata and thumbnails without a background migration queue.
  const bool forceRebuild = coverGenerationRequested;
  const bool metadataOnly = forceRebuild;
  // The Refresh Book Cache popup has already rendered. Keep that frame on the
  // panel and reuse its 48 KiB for bounded ZIP inflation during this explicit
  // one-book operation.
  std::optional<GfxRenderer::FrameBufferLoan> framebufferLoan;
  if (forceRebuild) framebufferLoan.emplace(renderer);
  const size_t recentBookCount = recentBooks.size();
  // Metadata extraction and thumbnail work may perform SD reads and update the
  // store. Release the long-lived Home read view and copy only the one record
  // being explicitly refreshed.
  recentBooks.reset();
  while (nextCoverToGenerate < recentBookCount) {
    const size_t bookIndex = nextCoverToGenerate++;
    RecentBook book;
    if (!RECENT_BOOKS.getBookAt(bookIndex, book)) continue;
    if (!FsHelpers::hasEpubExtension(book.path) && !FsHelpers::hasXtcExtension(book.path) &&
        !FsHelpers::hasCbzExtension(book.path)) continue;
    bool attempted = false;
    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      const bool loaded = metadataOnly ? epub.loadMetadataOnly() : epub.load(false, true);
      if (loaded) {
        attempted = true;
        const std::string title = epub.getTitle().empty() ? book.title : epub.getTitle();
        const std::string author = epub.getAuthor();
        const std::string cover = epub.getThumbBmpPath();
        // Some EPUBs expose their description through a metadata variant that
        // the lightweight pass cannot see. Never erase a working shelf synopsis
        // just because this refresh returned an empty description.
        std::string parsedSynopsis = epub.getDescription();
        // If the fresh lightweight OPF pass did not expose a description,
        // reuse the existing book.bin metadata when available. This recovers
        // a synopsis that was cached before a transient/partial refresh.
        if (parsedSynopsis.empty() && metadataOnly) {
          Epub cached(book.path, "/.crosspoint");
          if (cached.loadCachedMetadataOnly()) parsedSynopsis = cached.getDescription();
        }
        const std::string synopsis = parsedSynopsis.empty() ? book.synopsis : parsedSynopsis.substr(0, 384);
        const bool spineVisualChanged =
            book.title != title || book.author != author || book.coverBmpPath != cover;
        const bool synopsisChanged = book.synopsis != synopsis;
        if (spineVisualChanged || synopsisChanged) {
          if (spineVisualChanged) spineShelfVisualsDirty = true;
          if (synopsisChanged) liteSelectedDetailsDirty = true;
          book.title = title;
          book.author = author;
          book.coverBmpPath = cover;
          book.synopsis = synopsis;
          RECENT_BOOKS.refreshBookMetadata(book.path, book.title, book.author, book.coverBmpPath, book.synopsis);
        }
        // Never decode a missing cover during ordinary Recent bootstrap. A
        // placeholder keeps navigation responsive; manual refresh is the only
        // operation allowed to spend time and heap on image conversion.
        if (forceRebuild && !book.coverBmpPath.empty()) {
          const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
          if (!isValidBookThumbnail(thumb)) {
            const size_t coverBytes = epub.getCoverImageSize();
            if (coverBytes > library_book_preparation::MAX_COVER_SOURCE_BYTES) {
              LOG_DBG("SHELF", "Skipping oversized manual thumbnail source (%lu bytes): %s",
                      static_cast<unsigned long>(coverBytes), book.path.c_str());
            } else if (coverBytes == 0) {
              // No cover reference (or an unresolvable reference). Keep the
              // lightweight metadata and let the shelf use its placeholder.
              LOG_DBG("SHELF", "No usable EPUB cover for Recent: %s", book.path.c_str());
            } else {
              Storage.remove(thumb.c_str());
              if (!epub.generateThumbBmp(BOOKSHELF_COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
                LOG_ERR("SHELF", "Could not regenerate EPUB thumbnail: %s", book.path.c_str());
              }
            }
          }
          if (!refreshCarouselHqPath.empty() &&
              UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT) == refreshCarouselHqPath) {
            generateCarouselHqThumbnail(book, true);
          }
        }
      }
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        attempted = true;
        std::string title = xtc.getTitle().empty() ? book.title : xtc.getTitle();
        std::string author = xtc.getAuthor();
        const std::string cover = xtc.getThumbBmpPath();
        const std::string& parsedSynopsis = xtc.getDescription();
        std::string synopsis = parsedSynopsis.empty() ? book.synopsis : parsedSynopsis.substr(0, 384);
        const std::string& parsedSeries = xtc.getSeries();
        std::string series = parsedSeries.empty() ? book.series : parsedSeries.substr(0, 128);
        if (const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(book.path)) {
          title = overrideData->title;
          author = overrideData->author;
          synopsis = overrideData->synopsis.substr(0, 384);
        }
        const bool spineVisualChanged = book.title != title || book.author != author || book.coverBmpPath != cover;
        const bool synopsisChanged = book.synopsis != synopsis;
        if (spineVisualChanged || synopsisChanged || !book.xtcMetadataChecked) {
          if (spineVisualChanged) spineShelfVisualsDirty = true;
          if (synopsisChanged) liteSelectedDetailsDirty = true;
          book.title = title;
          book.author = author;
          book.coverBmpPath = cover;
          book.synopsis = synopsis;
        }
        book.series = series;
        book.xtcMetadataChecked = book.xtcMetadataChecked || xtc.hasLoadedExtendedMetadata();
        RECENT_BOOKS.refreshXtcMetadata(book.path, book.title, book.author, book.coverBmpPath, book.synopsis,
                                        book.series, book.xtcMetadataChecked);
        const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
        if (forceRebuild && !isValidBookThumbnail(thumb)) {
          // The XTCH writer commits a validated temporary thumbnail, so the
          // previous cache is retained until replacement succeeds.
          if (!xtc.generateThumbBmp(BOOKSHELF_COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
            LOG_ERR("SHELF", "Could not regenerate XTC thumbnail: %s", book.path.c_str());
          }
        }
        if (forceRebuild && !refreshCarouselHqPath.empty() &&
            UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT) == refreshCarouselHqPath) {
          generateCarouselHqThumbnail(book, true);
        }
      }
    } else if (FsHelpers::hasCbzExtension(book.path)) {
      Cbz cbz(book.path, "/.crosspoint");
      const bool loaded = cbz.loadMetadataOnly();
      if (loaded) {
        attempted = true;
        const std::string title = cbz.getTitle().empty() ? book.title : cbz.getTitle();
        const std::string author = cbz.getAuthor();
        const std::string cover = cbz.getThumbBmpPath();
        const std::string synopsis = cbz.getSynopsis().empty() ? book.synopsis : cbz.getSynopsis().substr(0, 384);
        const std::string thumb = UITheme::getCoverThumbPath(cover, BOOKSHELF_COVER_HEIGHT);
        logCbzCacheLookup(thumb, Storage.exists(thumb.c_str()));
        const bool spineVisualChanged =
            book.title != title || book.author != author || book.coverBmpPath != cover;
        const bool synopsisChanged = book.synopsis != synopsis;
        if (spineVisualChanged || synopsisChanged) {
          if (spineVisualChanged) spineShelfVisualsDirty = true;
          if (synopsisChanged) liteSelectedDetailsDirty = true;
          book.title = title;
          book.author = author;
          book.coverBmpPath = cover;
          book.synopsis = synopsis;
          RECENT_BOOKS.refreshBookMetadata(book.path, book.title, book.author, book.coverBmpPath, book.synopsis);
        }
        if (forceRebuild && !book.coverBmpPath.empty()) {
          const bool thumbValid = isValidBookThumbnail(thumb);
          if (!thumbValid) {
            if (!cbz.generateThumbBmp(BOOKSHELF_COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
              LOG_ERR("SHELF", "Could not generate CBZ thumbnail: %s", book.path.c_str());
            }
          }
          if (!refreshCarouselHqPath.empty() &&
              UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT) == refreshCarouselHqPath) {
            generateCarouselHqThumbnail(book, true);
          }
        }
      }
    }
    // A manual refresh targets one highlighted book. If its source cannot be
    // opened, stop here instead of scanning every Recent entry and making the
    // shelf appear locked for a long time.
    if (forceRebuild && !attempted) break;
    if (attempted) {
      // A manual refresh changes only the selected book. Keep the in-memory
      // shelf snapshot when possible so the next frame does not decode all
      // eight covers again; the featured panel and selected card are redrawn.
      const bool hadShelfSnapshot = snapshotRestored;
      const bool spineLayout = usesSpineLayout() || usesTwoRowSpineLayout();
      if (spineLayout) invalidateSpinePageWindow();
      size_t selectedPageStart = SIZE_MAX;
      size_t selectedPageItems = 0;
      const bool selectedPageKnown = !spineLayout && currentSnapshotPageInfo(selectedPageStart, selectedPageItems);
      const bool keepShelfSnapshot = manualSingleRefresh && hadShelfSnapshot && selectedPageKnown &&
                                     snapshotPageStart == selectedPageStart && snapshotPageItems == selectedPageItems;
      if (keepShelfSnapshot) {
        snapshotRestored = true;
        initialRenderPending = false;
        snapshotWritePending = true;
        snapshotWriteRequestedMs = millis();
      } else {
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotPageItems = 0;
        snapshotDynamicNeedsPatch = false;
        if (Storage.exists(FOLIO_HOME_SNAPSHOT)) Storage.remove(FOLIO_HOME_SNAPSHOT);
        initialRenderPending = true;
      }
      coverGenerationActive = false;
      refreshCarouselHqPath.clear();
      spineThumbnailPathMisses.clear();  // Explicit refresh may have created or replaced thumb_220.bmp.
      loadRecentBooks();
      // Refresh is an explicit one-book action; do not queue background
      // metadata migration after the selected cover is rebuilt.
      coverGenerationRequested = false;
      requestUpdate();
      return;  // At most one expensive extraction per activity cycle.
    }
  }
  coverGenerationActive = false;
  coverGenerationRequested = false;
  loadRecentBooks();
  requestUpdate();
}

bool RecentBooksActivity::generateCarouselHqThumbnail(const RecentBook& book, const bool force) {
  if (book.coverBmpPath.empty()) return false;

  const std::string output = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
  // A complete HQ cover remains a cache hit even during explicit refresh.
  // Generate into a temporary file and replace only an invalid cache.
  if (isValidBookThumbnail(output)) return true;

  bool attempted = false;
  bool generated = false;
  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    if (epub.loadMetadataOnly()) {
      attempted = true;
      const size_t coverBytes = epub.getCoverImageSize();
      if (coverBytes == 0) {
        LOG_DBG("SHELF", "No usable EPUB cover for Carousel HQ: %s", book.path.c_str());
      } else if (coverBytes > library_book_preparation::MAX_COVER_SOURCE_BYTES) {
        LOG_DBG("SHELF", "Skipping oversized Carousel HQ source (%lu bytes): %s",
                static_cast<unsigned long>(coverBytes), book.path.c_str());
      } else {
        generated = epub.generateThumbBmp(CAROUSEL_HQ_COVER_HEIGHT);
      }
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    if (xtc.load()) {
      attempted = true;
      generated = xtc.generateThumbBmp(CAROUSEL_HQ_COVER_HEIGHT);
    }
  } else if (FsHelpers::hasCbzExtension(book.path)) {
    Cbz cbz(book.path, "/.crosspoint");
    if (cbz.loadMetadataOnly()) {
      attempted = true;
      generated = cbz.generateThumbBmp(CAROUSEL_HQ_COVER_HEIGHT);
    }
  }

  if (!attempted || !generated || !isValidBookThumbnail(output)) {
    LOG_ERR("SHELF", "Could not prepare Carousel HQ thumbnail: %s", book.path.c_str());
    if (Storage.exists(output.c_str()) && !isValidBookThumbnail(output)) Storage.remove(output.c_str());
    return false;
  }
  return true;
}

void RecentBooksActivity::startCarouselHqPreparation() {
  // An explicit preparation pass is also the invalidation boundary for files
  // created or replaced since the last Carousel frame.
  invalidateCarouselHqProbes();
  carouselHqScanIndex = 0;
  carouselHqCurrentIndex = SIZE_MAX;
  carouselHqPreparedCount = 0;
  carouselHqPreparationActive = !recentBooks.empty();
  carouselHqPopupRendered = false;
  carouselHqCompletionPopupPending = false;

  if (recentBooks.empty()) {
    LOG_DBG("SHELF", "Carousel HQ preparation found no missing covers");
    // This menu action is confirmed on press, while normal book opening is
    // handled on the matching release. Consume that release and show a
    // completion message instead of opening the selected book.
    swallowBookConfirmRelease = true;
    longPressActionShown = true;
    carouselHqCompletionPopupPending = true;
    requestUpdate(true);
    return;
  }

  carouselHqPreparationActive = true;
  requestUpdate(true);
}

void RecentBooksActivity::prepareNextCarouselCover() {
  if (!carouselHqPreparationActive) return;
  if (!carouselHqPopupRendered) return;

  carouselHqPopupRendered = false;

  // Keep SD probing incremental. This bounds the work performed by one loop
  // iteration on X3 while retaining the existing one-cover-at-a-time decode.
  constexpr size_t SCAN_BUDGET = 12;
  size_t scanned = 0;
  while (carouselHqCurrentIndex == SIZE_MAX && carouselHqScanIndex < recentBooks.size() &&
         scanned++ < SCAN_BUDGET) {
    const size_t index = carouselHqScanIndex++;
    const RecentBook& book = recentBooks[index];
    if (book.coverBmpPath.empty() || RecentBooksStore::isMissing(book)) continue;
    if (!FsHelpers::hasEpubExtension(book.path) && !FsHelpers::hasXtcExtension(book.path) &&
        !FsHelpers::hasCbzExtension(book.path)) {
      continue;
    }

    // RecentBooksStore keeps one entry per path during normal updates, so no
    // per-session deduplication vector is needed here.
    const std::string hqPath = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
    if (!isValidBookThumbnail(hqPath)) carouselHqCurrentIndex = index;
  }

  if (carouselHqCurrentIndex == SIZE_MAX) {
    if (carouselHqScanIndex < recentBooks.size()) {
      requestUpdate(true);
      return;
    }
    carouselHqPreparationActive = false;
    requestUpdate(true);
    return;
  }

  requestUpdateAndWait();
  const RecentBook& book = recentBooks[carouselHqCurrentIndex];
  const bool success = generateCarouselHqThumbnail(book, true);
  if (!success) LOG_DBG("SHELF", "Carousel HQ fallback remains available: %s", book.path.c_str());
  invalidateCarouselHqProbes();
  carouselHqCurrentIndex = SIZE_MAX;
  ++carouselHqPreparedCount;
  requestUpdate(true);
}

void RecentBooksActivity::showMenu() {
  std::vector<std::string> options = {tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE), tr(STR_CLOCK_WEATHER),
                                      tr(STR_TODO_LIST), tr(STR_READING_STATS),
                                      "Bookmarks (all books)", "Clippings (all books)"};
  const bool carouselTheme = usesCarouselLayout();
  const int prepareCarouselCoversIndex = static_cast<int>(options.size());
  if (carouselTheme) options.emplace_back(tr(STR_PREPARE_CAROUSEL_COVERS));
  menuPopup.show(StrId::STR_MENU, options, 0, [this, carouselTheme, prepareCarouselCoversIndex](const int index) {
    // Always close the menu before starting another activity. This is
    // especially important for Settings, which replaces the shelf activity.
    menuPopup.dismiss();
    if (index == 0) {
      activityManager.goToFileTransfer();
    } else if (index == 1) {
      activityManager.goToSettings();
    } else if (index == 2) {
      startActivityForResult(std::make_unique<ClockWeatherActivity>(renderer, mappedInput), nullptr);
    } else if (index == 3) {
      startActivityForResult(std::make_unique<ToDoListActivity>(renderer, mappedInput), nullptr);
    } else if (index == 4) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), nullptr);
    } else if (index == 5) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, std::string{}, true),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
    } else if (index == 6) {
      startActivityForResult(std::make_unique<EpubReaderClippingListActivity>(
                                renderer, mappedInput, std::string{}, "All books", true),
                            nullptr);
    } else if (carouselTheme && index == prepareCarouselCoversIndex) {
      startCarouselHqPreparation();
    }
  });
  requestUpdate();
}

void RecentBooksActivity::showBookActions() {
  if (visibleBookCount == 0 || selectorIndex >= visibleBookCount) return;
  const RecentBook selectedBook = recentBooks[selectedRecentIndex()];
  std::vector<std::string> actions = {tr(STR_OPEN), tr(STR_MARK_READING), tr(STR_MARK_ON_HOLD), tr(STR_FINISHED),
                                      tr(STR_RESET_PROGRESS), tr(STR_REFRESH_BOOK_CACHE),
                                      tr(STR_DELETE_CACHE), tr(STR_REMOVE_FROM_LIST),
                                      tr(STR_READ_FULL_SYNOPSIS), tr(STR_BOOK_STATISTICS)};
  int bookmarksAction = -1;
  int clippingsAction = -1;
  if (FsHelpers::hasEpubExtension(selectedBook.path) || FsHelpers::hasXtcExtension(selectedBook.path)) {
    bookmarksAction = static_cast<int>(actions.size());
    actions.emplace_back(tr(STR_BOOKMARKS));
  }
  if (FsHelpers::hasEpubExtension(selectedBook.path)) {
    clippingsAction = static_cast<int>(actions.size());
    actions.emplace_back(tr(STR_CLIPPINGS));
  }
  bookActionsPopup.show(StrId::STR_BOOK_ACTIONS, actions, 0, [this, bookmarksAction, clippingsAction](const int action) {
    if (visibleBookCount == 0 || selectorIndex >= visibleBookCount) return;
    const RecentBook selected = recentBooks[selectedRecentIndex()];
    // The store-backed view is held while the shelf is active. Release it
    // before actions that write progress, metadata, or Recent history.
    recentBooks.reset();
    if (action == 0) {
      logCbzPath("recent-book-selection", selected.path);
      onSelectBook(selected.path);
      return;
    }
    if (action == 1 || action == 2) {
      BOOK_STATES.setStatus(selected.path, action == 1 ? BookStatus::Reading : BookStatus::OnHold);
      if (selected.progressPercent >= 100) RECENT_BOOKS.recordReading(selected.path, 99, 0);
    }
    if (action == 3) {
      BOOK_STATES.setStatus(selected.path, BookStatus::Finished);
      RECENT_BOOKS.recordReading(selected.path, 100, 0);
    }
    if (action == 4) {
      BOOK_STATES.reset(selected.path);
      resetBookProgress(selected.path);
      RECENT_BOOKS.recordReading(selected.path, 0, 0);
    }
    if (action == 5) {
      // Refresh only the lightweight bookshelf metadata and thumbnail. Do not
      // clear the reader cache: progress, bookmarks, clippings, and cached
      // reading pages must remain usable while the EPUB is reread.
      refreshCarouselHqPath.clear();
      if (!selected.coverBmpPath.empty()) {
        const std::string hqPath = UITheme::getCoverThumbPath(selected.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
        if (Storage.exists(hqPath.c_str())) refreshCarouselHqPath = hqPath;
      }
      nextCoverToGenerate = selectedRecentIndex();
      coverGenerationActive = false;
      coverGenerationRequested = false;
      manualSingleRefresh = true;
      retrievingBookCache = true;
      retrievingBookCacheProgress = 5;
      retrievingBookCacheIndex = selectorIndex;
      retrievingBookCachePopupRendered = false;
    }
    if (action == 6) {
      // Preserve progress, but force CBZ reader pages to be rebuilt.
      clearBookCache(selected.path);
    }
    if (action == 7) {
      RECENT_BOOKS.removeByPath(selected.path);
      BOOK_STATES.removeByPath(selected.path);
    }
    if (action == 8) {
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, selected.title, selected.author, selected.synopsis,
                                             selected.path, selected.coverBmpPath, selected.progressPercent,
                                             selected.readingSeconds, selected.readingSessions, selected.series,
                                             selected.xtcMetadataChecked),
          nullptr);
      return;
    }
    if (action == 9) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, selected.path), nullptr);
      return;
    }
    if (action == bookmarksAction && bookmarksAction >= 0) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, selected.path),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
      return;
    }
    if (action == clippingsAction && clippingsAction >= 0) {
      startActivityForResult(std::make_unique<EpubReaderClippingListActivity>(
                                renderer, mappedInput, selected.path, selected.title),
                            nullptr);
      return;
    }
    loadRecentBooks();
    rebuildVisibleBooks();
    snapshotRestored = false;
    lastRenderedSelectorIndex = SIZE_MAX;
    lastRenderedPageStart = SIZE_MAX;
    overlayFrameShown = false;
    if (Storage.exists(FOLIO_HOME_SNAPSHOT)) Storage.remove(FOLIO_HOME_SNAPSHOT);
    requestUpdate(true);
  });
  requestUpdate();
}

void RecentBooksActivity::onEnter() {
  const uint32_t enterStartedUs = micros();
  Activity::onEnter();
  ignoreEntryInputUntilNeutral = hasActiveHomeInput(mappedInput);
  if (ignoreEntryInputUntilNeutral) LOG_INF("SHELF", "stage=entry_input_gate_armed");
  invalidateSpinePageWindow();
  statsTransientStateReleased = false;

  // Load data
  loadRecentBooks();
  rebuildVisibleBooks();
  LOG_INF("SHELF", "stage=onEnter_ready duration_us=%lu store_result=%s store_records=%u view_records=%u "
                   "visible=%u tab=%u filtered_empty=%u free=%u largest=%u",
          static_cast<unsigned long>(micros() - enterStartedUs),
          recentBooksLoadResultName(RECENT_BOOKS.getLoadResult()), static_cast<unsigned>(recentBooks.size()),
          static_cast<unsigned>(recentBooks.size()), static_cast<unsigned>(visibleBookCount),
          static_cast<unsigned>(activeTab), !recentBooks.empty() && visibleBookCount == 0 ? 1U : 0U,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  logRecentViewState("on_enter_view");
#if FREEINK_CAP_BLE_HID_HOST
  if (SETTINGS.bluetoothEnabled) {
    size_t stringCapacityChars = 0;
    for (const auto& book : recentBooks) {
      stringCapacityChars += book.path.capacity() + book.title.capacity() + book.author.capacity() +
                             book.coverBmpPath.capacity() + book.synopsis.capacity() + book.series.capacity();
    }
    LOG_INF("HEAPSHAPE", "stage=recentbooks books=%u vec=%u strchars=%u free=%u max=%u",
            static_cast<unsigned>(recentBooks.size()),
            static_cast<unsigned>(recentBooks.capacity() * sizeof(RecentBook)),
            static_cast<unsigned>(stringCapacityChars), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
#endif

  selectorIndex = 0;
  nextCoverToGenerate = 0;
  coverGenerationRequested = false;
  manualSingleRefresh = false;
  retrievingBookCache = false;
  retrievingBookCacheProgress = 0;
  retrievingBookCacheIndex = SIZE_MAX;
  retrievingBookCachePopupRendered = false;
  carouselHqScanIndex = 0;
  carouselHqCurrentIndex = SIZE_MAX;
  carouselHqPreparedCount = 0;
  carouselHqPreparationActive = false;
  carouselHqPopupRendered = false;
  carouselHqCompletionPopupPending = false;
  refreshCarouselHqPath.clear();
  // A pending write belongs to the previous shelf frame. Never carry it into
  // a new activity instance, where it could write unrelated framebuffer data
  // during the first interaction after boot.
  snapshotWritePending = false;
  snapshotWriteRequestedMs = 0;
  coverGenerationRequested = false;
  snapshotRestored = restoreSnapshot();
  if (!snapshotRestored) {
    LOG_INF("SHELF", "stage=on_enter_snapshot_invalidated reason=%s", snapshotRestoreFailureReason);
  }
  logRecentViewState(snapshotRestored ? "on_enter_snapshot_restored" : "on_enter_snapshot_invalidated");
  if (APP_STATE.selectFirstHomeBookAfterWake) {
    const bool wakeSelectionChanged = selectorIndex != 0;
    if (wakeSelectionChanged) {
      selectorIndex = 0;
      snapshotRestored = false;
      snapshotFastPathUsed = false;
      snapshotPageStart = SIZE_MAX;
      snapshotPageItems = 0;
      snapshotSelectorIndex = SIZE_MAX;
      snapshotDynamicNeedsPatch = false;
      lastFeaturedPath.clear();
      lastFeaturedCoverPath.clear();
    }
    APP_STATE.selectFirstHomeBookAfterWake = false;
    APP_STATE.saveToFile();
    LOG_INF("SHELF", "stage=wake_selection_reset changed=%u selected=%u tab=%u snapshot=%u",
            wakeSelectionChanged ? 1U : 0U, static_cast<unsigned>(selectorIndex), static_cast<unsigned>(activeTab),
            snapshotRestored ? 1U : 0U);
  }
  if (!snapshotRestored) {
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    snapshotSelectorIndex = SIZE_MAX;
    snapshotBookPathKeys.fill(0);
    snapshotBookVisualKeys.fill(0);
    snapshotBookRects.fill({});
    snapshotBookGeometryAvailable = false;
    snapshotDynamicNeedsPatch = false;
    snapshotRewriteAfterRestore = false;
  }
  snapshotFastPathUsed = false;
  lastRenderedSelectorIndex = SIZE_MAX;
  lastRenderedPageStart = SIZE_MAX;
  lastRenderedTab = 0;
  lastRenderedLiteLayout = 0xFF;
  lastRenderedLiteSpineLayout = 0xFF;
  lastRenderedLiteSpinePageStart = SIZE_MAX;
  if (!snapshotRestored) {
    lastFeaturedPath.clear();
    lastFeaturedCoverPath.clear();
  }
  overlayFrameShown = false;
  litePopupFrameRetainsShelf = false;
  initialRenderPending = true;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  logRecentViewState("on_exit_view");
#if FREEINK_CAP_BLE_HID_HOST
  LOG_INF("HEAPSHAPE", "stage=recentbooks_transition_before_release books=%u capacity=%u bytes=%u free=%u largest=%u",
          static_cast<unsigned>(recentBooks.size()), static_cast<unsigned>(recentBooks.capacity()),
          static_cast<unsigned>(recentBooks.capacity() * sizeof(RecentBook)), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  if (usesPersistentHomeSnapshot() && visibleBookCount > 0) writeSnapshotNavigationState();
  Activity::onExit();
  litePopupFrameRetainsShelf = false;
  // This activity is leaving permanently. Release its view of the canonical
  // Recent store before invalidating thumbnail sources.
  recentBooks.reset();
  visibleBookCount = 0;
  // ActivityManager calls onExit() while holding RenderLock. Keep cache
  // destruction on the already-locked path so teardown cannot deadlock while
  // still guaranteeing no render is using a cached source.
  invalidateCarouselHqProbesLocked();
#if FREEINK_CAP_BLE_HID_HOST
  LOG_INF("HEAPSHAPE", "stage=recentbooks_transition_after_release free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
}

void RecentBooksActivity::releaseTransientStateForReadingStats() {
  const size_t beforeFree = ESP.getFreeHeap();
  const size_t beforeLargest = ESP.getMaxAllocHeap();

  // These cover files/decoded bitmaps are the only RecentBooks-owned heap
  // cache that can retain large, movable blocks while a child statistics
  // screen is on top. The source is reopened lazily on the first resumed
  // shelf render; persistent RecentBook metadata is intentionally retained.
  invalidateCarouselSourceCacheLocked();
  for (auto& entry : carouselSourceCache) std::string().swap(entry.path);
  const size_t afterCacheFree = ESP.getFreeHeap();
  const size_t afterCacheLargest = ESP.getMaxAllocHeap();

  // A menu can be the activity that launched ReadingStats. Its option strings,
  // layout rectangles, and callback are no longer needed while the child is
  // active and would otherwise survive until the parent is destroyed.
  menuPopup.clear();
  bookActionsPopup.clear();
  const size_t afterPopupFree = ESP.getFreeHeap();
  const size_t afterPopupLargest = ESP.getMaxAllocHeap();

  // Release the lock-backed view while the child is active. The store already
  // owns the metadata, so there is no second vector to destroy or rebuild.
  recentBooks.reset();
  visibleBookCount = 0;
  statsTransientStateReleased = true;
  const size_t afterBooksFree = ESP.getFreeHeap();
  const size_t afterBooksLargest = ESP.getMaxAllocHeap();

  // Force the first resumed frame through the normal shelf planner/source
  // lookup without changing tab, selection, or persisted shelf ordering.
  invalidateSpinePageWindow();
  snapshotFastPathUsed = false;
  snapshotRestored = false;
  initialRenderPending = true;
  lastRenderedSelectorIndex = SIZE_MAX;
  lastRenderedPageStart = SIZE_MAX;
  lastRenderedTab = 0;
  std::string().swap(lastFeaturedPath);
  std::string().swap(lastFeaturedCoverPath);

  LOG_INF("RSDIAG",
          "stage=recentbooks_stats_transient_release owner=carousel_source_cache free=%u->%u largest=%u->%u "
          "owner=popup_state free=%u->%u owner=recentbooks_view free=%u->%u total=%u->%u",
          static_cast<unsigned>(beforeFree), static_cast<unsigned>(afterCacheFree),
          static_cast<unsigned>(beforeLargest), static_cast<unsigned>(afterCacheLargest),
          static_cast<unsigned>(afterCacheFree), static_cast<unsigned>(afterPopupFree),
          static_cast<unsigned>(afterPopupFree), static_cast<unsigned>(afterBooksFree),
          static_cast<unsigned>(afterPopupLargest), static_cast<unsigned>(afterBooksLargest),
          static_cast<unsigned>(beforeFree), static_cast<unsigned>(afterBooksFree));
}

void RecentBooksActivity::restoreTransientStateAfterReadingStats() {
  if (!statsTransientStateReleased) return;
  spineShelfVisualsDirty = true;
  liteSelectedDetailsDirty = true;

  const size_t selectedBefore = selectorIndex;
  const uint8_t tabBefore = activeTab;
  const size_t booksBeforeSize = recentBooks.size();
  const size_t booksBeforeCapacity = recentBooks.capacity();
  LOG_INF("RSDIAG", "stage=parent_restore_begin books=%u capacity=%u selected=%u tab=%u free=%u largest=%u",
          static_cast<unsigned>(booksBeforeSize), static_cast<unsigned>(booksBeforeCapacity),
          static_cast<unsigned>(selectedBefore), static_cast<unsigned>(tabBefore),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));

  // ActivityManager calls this with RenderLock held. Reacquire the canonical
  // in-memory list and invalidate only rebuildable carousel sources; this is
  // not an SD metadata rescan and must not recursively acquire RenderLock.
  const size_t booksBeforeFree = ESP.getFreeHeap();
  recentBooks = RECENT_BOOKS.getBooks();
  LOG_INF("RSDIAG", "owner=recentbooks_view books=%u capacity=%u free=%u->%u largest=%u",
          static_cast<unsigned>(recentBooks.size()), static_cast<unsigned>(recentBooks.capacity()),
          static_cast<unsigned>(booksBeforeFree), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));

  invalidateSpinePageWindow();
  const size_t carouselBeforeFree = ESP.getFreeHeap();
  const size_t carouselBeforeLargest = ESP.getMaxAllocHeap();
  invalidateCarouselHqProbesLocked();
  LOG_INF("RSDIAG", "owner=carousel_source_cache free=%u->%u largest=%u->%u",
          static_cast<unsigned>(carouselBeforeFree), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(carouselBeforeLargest), static_cast<unsigned>(ESP.getMaxAllocHeap()));

  // The launch popup was intentionally dismissed before Stats. Restore a
  // usable shelf, not a stale popup/callback owned by the suspended activity.
  LOG_INF("RSDIAG", "owner=popup_state menu=%u book_actions=%u restored=closed",
          menuPopup.isActive() ? 1U : 0U, bookActionsPopup.isActive() ? 1U : 0U);
  activeTab = tabBefore;
  rebuildVisibleBooks();
  selectorIndex = visibleBookCount == 0 ? 0 : std::min(selectedBefore, visibleBookCount - 1);
  statsTransientStateReleased = false;
  statsReturnAwaitingFirstRender = true;
  snapshotFastPathUsed = false;
  snapshotRestored = usesPersistentHomeSnapshot() && restoreSnapshot();
  selectorIndex = visibleBookCount == 0 ? 0 : std::min(selectedBefore, visibleBookCount - 1);
  if (!snapshotRestored) {
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    snapshotSelectorIndex = SIZE_MAX;
    snapshotDynamicNeedsPatch = false;
    lastFeaturedPath.clear();
    lastFeaturedCoverPath.clear();
  }
  initialRenderPending = true;
  LOG_INF("RSDIAG", "stage=parent_restore_complete books=%u capacity=%u visible=%u selected=%u tab=%u free=%u largest=%u",
          static_cast<unsigned>(recentBooks.size()), static_cast<unsigned>(recentBooks.capacity()),
          static_cast<unsigned>(visibleBookCount), static_cast<unsigned>(selectorIndex),
          static_cast<unsigned>(activeTab), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  logRecentViewState(snapshotRestored ? "stats_return_snapshot_restored" : "stats_return_snapshot_invalidated");
}

void RecentBooksActivity::releaseRecentBooksViewForChild() {
  recentBooks.reset();
  invalidateSpinePageWindow();
}

void RecentBooksActivity::restoreRecentBooksViewAfterChild(const bool renderLockHeld) {
  if (!recentBooks.ownsLock()) {
    recentBooks = RECENT_BOOKS.getBooks();
  }
  invalidateSpinePageWindow();
  if (renderLockHeld) {
    invalidateCarouselHqProbesLocked();
  } else {
    invalidateCarouselHqProbes();
  }
  rebuildVisibleBooks();
  selectorIndex = visibleBookCount == 0 ? 0 : std::min(selectorIndex, visibleBookCount - 1);
  initialRenderPending = true;
}

void RecentBooksActivity::loop() {
  if (ignoreEntryInputUntilNeutral) {
    // A transition can carry the previous screen's held key into this first
    // frame. Consume it through release so it cannot open a book, while
    // allowing input immediately once controls are neutral. A time cutoff
    // would let a long-held key escape the gate and would delay ordinary taps.
    if (hasActiveHomeInput(mappedInput)) return;
    ignoreEntryInputUntilNeutral = false;
    LOG_INF("SHELF", "stage=entry_input_gate_cleared reason=neutral");
  }
  const int pageItems = activePageItems();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool carouselTheme = usesCarouselLayout();
  const bool liteTheme = isFolioNooirLiteTheme(SETTINGS.uiTheme);
  const bool spineLayout = usesSpineLayout();
  const bool twoRowSpineLayout = usesTwoRowSpineLayout();
  const bool threeRowSpineLayout = usesThreeRowSpineLayout();
  const bool snapshotLayout = usesPersistentHomeSnapshot();
  const bool threeCoverGrid = usesThreeCoverGrid();
  FolioShelfLayout layout{};
  CarouselLayout liteCarouselLayout;
  if (liteTheme) {
    static const FolioNooirTheme folioPresentation;
    static const CarouselTheme carouselPresentation;
    const FolioShelfLayout folioLayout = homeShelfLayout(folioPresentation, renderer, metrics);
    if (liteCarouselLayoutCacheValid) {
      liteCarouselLayout = liteCarouselLayoutCache;
    } else {
      liteCarouselLayout = liteCarouselPageLayout(renderer, carouselPresentation, folioLayout);
    }
    layout = liteBookDisplayLayout(folioLayout, liteCarouselLayout, renderer.getScreenWidth());
  } else if (carouselTheme) {
    static const FolioNooirTheme folioPresentation;
    layout = homeShelfLayout(folioPresentation, renderer, metrics);
  } else {
    const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
    layout = homeShelfLayout(folioTheme, renderer, metrics);
  }

  // Snapshot persistence is an optimization, not part of showing the shelf.
  // Defer the SD write until the user has been idle so the first boot render
  // and the first button press do not wait behind a 48 KB filesystem write.
  if (snapshotLayout && snapshotWritePending && millis() - snapshotWriteRequestedMs >= 500 &&
      !mappedInput.wasAnyPressed() && !mappedInput.wasAnyReleased() && !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.isPressed(MappedInputManager::Button::Back) && !menuPopup.isActive() &&
      !bookActionsPopup.isActive() && !retrievingBookCache) {
    snapshotWritePending = false;
    RenderLock lock;
    // The on-screen frame has a focus outline, but the persisted base frame
    // must not contain it (or a popup overlay). Erase only that outline while
    // writing, then restore it in the in-memory framebuffer without another
    // panel refresh.
    bool focusOutlineErased = false;
    if (visibleBookCount > 0) {
      drawSnapshotSelectionOutline(layout, selectorIndex, false);
      focusOutlineErased = true;
    }
    writeSnapshot();
    if (focusOutlineErased && visibleBookCount > 0) {
      drawSnapshotSelectionOutline(layout, selectorIndex, true);
    }
  }

  const bool bookPopupActive = bookActionsPopup.isActive();
  const bool bookPopupConfirm = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool bookPopupBack = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
#if NOOIR_BLE_DIAGNOSTICS
  const bool bookPopupBleNext =
      bookPopupActive && mappedInput.bleWasNavigationPressed(MappedInputManager::Button::NavNext);
  const bool bookPopupBlePrevious =
      bookPopupActive && mappedInput.bleWasNavigationPressed(MappedInputManager::Button::NavPrevious);
  const bool bookPopupBleConfirm = bookPopupActive && mappedInput.bleWasPressed(MappedInputManager::Button::Confirm);
  const bool bookPopupBleBack = bookPopupActive && mappedInput.bleWasPressed(MappedInputManager::Button::Back);
#endif
  if (bookActionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
#if NOOIR_BLE_DIAGNOSTICS
    if (bookPopupBleNext) bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=book_actions action=next");
    if (bookPopupBlePrevious)
      bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=book_actions action=previous");
    if (bookPopupBleConfirm)
      bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=book_actions action=confirm");
    if (bookPopupBleBack) bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=book_actions action=back");
#endif
    if (bookPopupConfirm) swallowBookConfirmRelease = true;
    if (bookPopupBack) swallowBookBackRelease = true;
    return;
  }
  if (swallowBookBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowBookBackRelease = false;
    return;
  }

  // A child activity can consume the Confirm release that opened it. Clear
  // the long-press guard when the shelf becomes active again, otherwise the
  // next long press is incorrectly treated as the original one.
  if (!bookActionsPopup.isActive() && !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    swallowBookConfirmRelease = false;
  }

  if (retrievingBookCache && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (visibleBookCount > 0 && retrievingBookCacheIndex < visibleBookCount) {
      clearBookCache(recentBooks[selectedRecentIndex()].path);
    }
    retrievingBookCache = false;
    retrievingBookCachePopupRendered = false;
    retrievingBookCacheProgress = 0;
    coverGenerationRequested = false;
    manualSingleRefresh = false;
    requestUpdate(true);
    return;
  }

  if (retrievingBookCache) {
    // Popup-first: wait for the visible retrieval dialog instead of using a
    // fixed timeout fallback that could start ZIP/image work before feedback
    // reached a slower panel.
    if (!retrievingBookCachePopupRendered) return;
    retrievingBookCacheProgress = 35;
    requestUpdateAndWait();
    const unsigned long retrievalWorkStartedMs = millis();
    const size_t selectedIndex = selectedRecentIndex();
    const std::string selectedPath = selectedIndex < recentBooks.size() ? recentBooks[selectedIndex].path : std::string{};
    retrievingBookCache = false;
    retrievingBookCachePopupRendered = false;
    coverGenerationRequested = true;
    // Keep the retrieval dialog as the last complete frame while the
    // synchronous ZIP/thumbnail work runs. This prevents slower X3 panels
    // from appearing frozen or showing an empty cover during extraction.
    generateNextCover();
    LOG_DBG("PERF", "Metadata retrieval book=%s elapsed=%lums", selectedPath.c_str(),
            static_cast<unsigned long>(millis() - retrievalWorkStartedMs));
    retrievingBookCacheProgress = 100;
    requestUpdate();
    return;
  }

  if (carouselHqPreparationActive && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    carouselHqPreparationActive = false;
    carouselHqPopupRendered = false;
    carouselHqScanIndex = 0;
    carouselHqCurrentIndex = SIZE_MAX;
    carouselHqPreparedCount = 0;
    requestUpdate(true);
    return;
  }

  if (carouselHqPreparationActive) {
    prepareNextCarouselCover();
    return;
  }

  if (visibleBookCount > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= 1000 && !longPressActionShown) {
    longPressActionShown = true;
    swallowBookConfirmRelease = true;
    showBookActions();
    return;
  }

  const bool menuWasActive = menuPopup.isActive();
  const bool menuBackPressed =
      menuWasActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
#if NOOIR_BLE_DIAGNOSTICS
  const bool menuBleNext = menuWasActive && mappedInput.bleWasNavigationPressed(MappedInputManager::Button::NavNext);
  const bool menuBlePrevious =
      menuWasActive && mappedInput.bleWasNavigationPressed(MappedInputManager::Button::NavPrevious);
  const bool menuBleConfirm = menuWasActive && mappedInput.bleWasPressed(MappedInputManager::Button::Confirm);
  const bool menuBleBack = menuWasActive && mappedInput.bleWasPressed(MappedInputManager::Button::Back);
#endif
  if (menuPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
#if NOOIR_BLE_DIAGNOSTICS
    if (menuBleNext) bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=main action=next");
    if (menuBlePrevious) bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=main action=previous");
    if (menuBleConfirm) bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=main action=confirm");
    if (menuBleBack) bleinput::recordDiagnosticEvent("consumer=RecentBooks menu=main action=back");
#endif
    if (menuBackPressed) swallowMenuBackRelease = true;
    return;
  }

  // OptionPopup dismisses on press while this activity opens the menu on
  // release. Consume the matching release so one press cannot close and then
  // immediately reopen the menu.
  if (swallowMenuBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowMenuBackRelease = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    if (swallowBookConfirmRelease) {
      swallowBookConfirmRelease = false;
      return;
    }
    if (visibleBookCount > 0 && selectorIndex < visibleBookCount) {
      const RecentBook& selected = recentBooks[selectedRecentIndex()];
#if NOOIR_BLE_DIAGNOSTICS
      if (mappedInput.bleWasReleased(MappedInputManager::Button::Confirm)) {
        bleinput::recordDiagnosticEvent("consumer=RecentBooks action=open selection=%u",
                                        static_cast<unsigned>(selectorIndex));
      }
#endif
      LOG_DBG("RBA", "Selected recent book: %s", selected.path.c_str());
      logCbzPath("recent-book-selection", selected.path);
      onSelectBook(selected.path);
      return;
    }
  }

  const bool previousTab = mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextTab = mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (previousTab) {
#if NOOIR_BLE_DIAGNOSTICS
    if (mappedInput.bleWasReleased(MappedInputManager::Button::Left)) {
      bleinput::recordDiagnosticEvent("consumer=RecentBooks action=previous_tab");
    }
#endif
    // Third button: always return to the Library, regardless of whether the
    // current shelf is Recent or Finished.
    activityManager.goToFileBrowser("/");
    return;
  }
  if (nextTab) {
#if NOOIR_BLE_DIAGNOSTICS
    if (mappedInput.bleWasReleased(MappedInputManager::Button::Right)) {
      bleinput::recordDiagnosticEvent("consumer=RecentBooks action=next_tab");
    }
#endif
    // Fourth button: switch directly between Recent and Finished.
    activeTab = activeTab == 2 ? 1 : 2;
    selectorIndex = 0;
    rebuildVisibleBooks();
    logRecentViewState("button_tab_switch");
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotSelectorIndex = SIZE_MAX;
    lastRenderedSelectorIndex = SIZE_MAX;
    lastRenderedPageStart = SIZE_MAX;
    lastFeaturedPath.clear();
    lastFeaturedCoverPath.clear();
    overlayFrameShown = false;
    initialRenderPending = true;
    requestUpdate();
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (carouselTheme) {
    const auto stackSlots = liteTheme
                                ? (activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL
                                       ? CoverStackGeometry::layoutThree(renderer.getScreenWidth(), visibleBookCount,
                                                                         selectorIndex,
                                                                         liteCarouselLayout.centerHeight)
                                       : CoverStackGeometry::layout(renderer.getScreenWidth(), visibleBookCount,
                                                                    selectorIndex,
                                                                    liteCarouselLayout.centerHeight))
                                : CoverStackGeometry::layoutFolioShelf(
                                      renderer.getScreenWidth(), layout.gridTop, layout.gridHeight, visibleBookCount,
                                      selectorIndex,
                                      activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        logRecentViewState("touch_tab_switch");
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (int slot = static_cast<int>(stackSlots.size()) - 1; slot >= 0; --slot) {
        const auto& stackSlot = stackSlots[static_cast<size_t>(slot)];
        if (!stackSlot.valid) continue;
        const auto& rect = stackSlot.rect;
        if (touchX >= rect.x && touchX < rect.x + rect.width && touchY >= rect.y &&
            touchY < rect.y + rect.height) {
          if (selectorIndex != stackSlot.itemIndex) {
            selectorIndex = stackSlot.itemIndex;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (int slot = static_cast<int>(stackSlots.size()) - 1; slot >= 0; --slot) {
      const auto& stackSlot = stackSlots[static_cast<size_t>(slot)];
      if (!stackSlot.valid) continue;
      const auto& rect = stackSlot.rect;
      if (mappedInput.wasTapInRect(rect.x, rect.y, rect.width, rect.height)) {
        selectorIndex = stackSlot.itemIndex;
        const size_t recentIndex = recentIndexForVisibleIndex(selectorIndex);
        if (recentIndex == SIZE_MAX) return;
        logCbzPath("recent-book-selection", recentBooks[recentIndex].path);
        onSelectBook(recentBooks[recentIndex].path);
        return;
      }
    }
  } else if (spineLayout) {
    const SpineShelfPlanner::Rect spineBounds = spineShelfBounds(renderer.getScreenWidth(), layout);
    const auto& spineWindow = spinePageForSelection(spineBounds);
    const auto& page = spineWindow.page;
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        logRecentViewState("touch_tab_switch");
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
        if (page.slots[slotIndex].rect.contains(touchX, touchY)) {
          const size_t index = page.slots[slotIndex].itemIndex;
          if (selectorIndex != index) {
            selectorIndex = index;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
      const auto& rect = page.slots[slotIndex].rect;
      if (mappedInput.wasTapInRect(rect.x, rect.y, rect.width, rect.height)) {
        selectorIndex = page.slots[slotIndex].itemIndex;
        const size_t recentIndex = recentIndexForVisibleIndex(selectorIndex);
        if (recentIndex == SIZE_MAX) return;
        logCbzPath("recent-book-selection", recentBooks[recentIndex].path);
        onSelectBook(recentBooks[recentIndex].path);
        return;
      }
    }
  } else if (twoRowSpineLayout) {
    const SpineShelfPlanner::Rect bounds = twoRowSpineBounds(renderer.getScreenWidth(), layout);
    const auto& spineWindow = twoRowSpinePageForSelection(bounds);
    const auto& page = spineWindow.page;
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        logRecentViewState("touch_tab_switch");
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
        if (page.slots[slotIndex].rect.contains(touchX, touchY)) {
          const size_t index = page.slots[slotIndex].itemIndex;
          if (selectorIndex != index) {
            selectorIndex = index;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
      const auto& rect = page.slots[slotIndex].rect;
      if (mappedInput.wasTapInRect(rect.x, rect.y, rect.width, rect.height)) {
        selectorIndex = page.slots[slotIndex].itemIndex;
        const size_t recentIndex = recentIndexForVisibleIndex(selectorIndex);
        if (recentIndex == SIZE_MAX) return;
        logCbzPath("recent-book-selection", recentBooks[recentIndex].path);
        onSelectBook(recentBooks[recentIndex].path);
        return;
      }
    }
  } else if (threeRowSpineLayout) {
    const SpineShelfPlanner::Rect bounds = threeRowSpineBounds(renderer.getScreenWidth(), layout);
    const auto& spineWindow = threeRowSpinePageForSelection(bounds);
    const auto& page = spineWindow.page;
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        logRecentViewState("touch_tab_switch");
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
        if (page.slots[slotIndex].rect.contains(touchX, touchY)) {
          const size_t index = page.slots[slotIndex].itemIndex;
          if (selectorIndex != index) {
            selectorIndex = index;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (size_t slotIndex = 0; slotIndex < page.itemCount; ++slotIndex) {
      const auto& rect = page.slots[slotIndex].rect;
      if (mappedInput.wasTapInRect(rect.x, rect.y, rect.width, rect.height)) {
        selectorIndex = page.slots[slotIndex].itemIndex;
        const size_t recentIndex = recentIndexForVisibleIndex(selectorIndex);
        if (recentIndex == SIZE_MAX) return;
        logCbzPath("recent-book-selection", recentBooks[recentIndex].path);
        onSelectBook(recentBooks[recentIndex].path);
        return;
      }
    }
  } else {
    const int columns = threeCoverGrid ? 3 : layout.columns;
    const int gap = threeCoverGrid ? 10 : layout.gridGap;
    const int cardWidth = threeCoverGrid ? (renderer.getScreenWidth() - gap * (columns + 1)) / columns : layout.cardWidth;
    const int gridTop = layout.gridTop;
    const int cardHeight = threeCoverGrid ? layout.gridHeight : layout.cardHeight;
    const int pageItems = threeCoverGrid ? 3 : BOOKS_PER_PAGE;
    const size_t pageStart = (selectorIndex / pageItems) * pageItems;
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        logRecentViewState("touch_tab_switch");
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (int slot = 0; slot < pageItems; ++slot) {
        const size_t index = pageStart + static_cast<size_t>(slot);
        if (index >= visibleBookCount) break;
        const int cardX = gap + (slot % columns) * (cardWidth + gap);
        const int cardY = gridTop + (slot / columns) * cardHeight;
        if (touchX >= cardX && touchX < cardX + cardWidth && touchY >= cardY && touchY < cardY + cardHeight) {
          if (selectorIndex != index) {
            selectorIndex = index;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (int slot = 0; slot < pageItems; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= visibleBookCount) break;
      const int cardX = gap + (slot % columns) * (cardWidth + gap);
      const int cardY = gridTop + (slot / columns) * cardHeight;
      if (mappedInput.wasTapInRect(cardX, cardY, cardWidth, cardHeight)) {
        selectorIndex = index;
        const size_t recentIndex = recentIndexForVisibleIndex(index);
        if (recentIndex == SIZE_MAX) return;
        logCbzPath("recent-book-selection", recentBooks[recentIndex].path);
        onSelectBook(recentBooks[recentIndex].path);
        return;
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (isFolioShelfTheme(SETTINGS.uiTheme) || carouselTheme)
      showMenu();
    else
      onGoHome();
  }

  int listSize = visibleBookCount;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = carouselTheme
                        ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                        : (threeRowSpineLayout
                                ? moveThreeRowSpinePage(layout, true)
                           : (spineLayout
                                ? moveSpinePage(layout, true)
                               : (twoRowSpineLayout
                                      ? moveTwoRowSpinePage(layout, true)
                               : ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems))));
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = carouselTheme
                        ? ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize)
                        : (threeRowSpineLayout
                                ? moveThreeRowSpinePage(layout, false)
                           : (spineLayout
                                ? moveSpinePage(layout, false)
                               : (twoRowSpineLayout
                                      ? moveTwoRowSpinePage(layout, false)
                               : ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems))));
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
#if NOOIR_BLE_DIAGNOSTICS
    if (mappedInput.bleWasNavigationReleased(MappedInputManager::Button::NavNext)) {
      bleinput::recordDiagnosticEvent("consumer=RecentBooks action=next selection=%u",
                                      static_cast<unsigned>(selectorIndex));
    }
#endif
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
#if NOOIR_BLE_DIAGNOSTICS
    if (mappedInput.bleWasNavigationReleased(MappedInputManager::Button::NavPrevious)) {
      bleinput::recordDiagnosticEvent("consumer=RecentBooks action=previous selection=%u",
                                      static_cast<unsigned>(selectorIndex));
    }
#endif
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems, carouselTheme, spineLayout, twoRowSpineLayout,
                                    threeRowSpineLayout] {
#if NOOIR_BLE_DIAGNOSTICS
    if (mappedInput.bleIsNavigationPressed(MappedInputManager::Button::NavNext)) {
      bleinput::recordDiagnosticEvent("consumer=RecentBooks action=next_repeat selection=%u",
                                      static_cast<unsigned>(selectorIndex));
    }
#endif
    // A held Spine control repeats the same logical selection step as a tap.
    // The normal page-window render moves to the next shelf when that step
    // crosses a page boundary; skipping directly to page starts loses books.
    selectorIndex = (carouselTheme || spineLayout || twoRowSpineLayout || threeRowSpineLayout)
                        ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                        : ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems, carouselTheme, spineLayout, twoRowSpineLayout,
                                        threeRowSpineLayout] {
#if NOOIR_BLE_DIAGNOSTICS
    if (mappedInput.bleIsNavigationPressed(MappedInputManager::Button::NavPrevious)) {
      bleinput::recordDiagnosticEvent("consumer=RecentBooks action=previous_repeat selection=%u",
                                      static_cast<unsigned>(selectorIndex));
    }
#endif
    selectorIndex = (carouselTheme || spineLayout || twoRowSpineLayout || threeRowSpineLayout)
                        ? ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize)
                        : ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  if (coverGenerationRequested) generateNextCover();
}

void RecentBooksActivity::promptRemoveBook(const std::string& path, const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("RBA", "Remove from recents cancelled");
      return;
    }
    if (RECENT_BOOKS.removeByPath(path)) {
      LOG_DBG("RBA", "Removed from recents: %s", path.c_str());
      loadRecentBooks();
      rebuildVisibleBooks();
      if (recentBooks.empty()) {
        selectorIndex = 0;
      } else if (selectorIndex >= visibleBookCount) {
        selectorIndex = visibleBookCount > 0 ? visibleBookCount - 1 : 0;
      }
      requestUpdate(true);
    }
  };

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

#if 0
void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOKSHELF));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Four-card bookshelf. Only cached BMP thumbnails are decoded; missing
  // thumbnails are generated one at a time from loop(), never inside render().
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    constexpr int columns = 2;
    constexpr int gap = 10;
    const int cardWidth = (pageWidth - gap * 3) / columns;
    const int cardHeight = contentHeight / 2;
    const size_t pageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    for (int slot = 0; slot < BOOKS_PER_PAGE; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= recentBooks.size()) break;
      const RecentBook& book = recentBooks[index];
      const int col = slot % columns;
      const int row = slot / columns;
      const int cardX = gap + col * (cardWidth + gap);
      const int cardY = contentTop + row * cardHeight;
      if (index == selectorIndex) renderer.drawRect(cardX, cardY, cardWidth, cardHeight - gap);

      const int coverHeight = std::min(BOOKSHELF_COVER_HEIGHT, cardHeight - 67);
      int coverWidth = coverHeight * 2 / 3;
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
      HalFile file;
      bool drewCover = false;
      if (!book.coverBmpPath.empty() && Storage.openFileForRead("SHELF", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          coverWidth = coverHeight * bitmap.getWidth() / bitmap.getHeight();
          coverWidth = std::min(coverWidth, cardWidth - 24);
          renderer.drawBitmap(bitmap, cardX + (cardWidth - coverWidth) / 2, cardY + 5, coverWidth, coverHeight);
          drewCover = true;
        }
      }
      if (!drewCover) {
        renderer.drawRect(cardX + (cardWidth - coverWidth) / 2, cardY + 5, coverWidth, coverHeight);
        const std::string placeholderTitle =
            renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), coverWidth - 12);
        const int placeholderX = cardX + (cardWidth - renderer.getTextWidth(UI_10_FONT_ID, placeholderTitle.c_str())) / 2;
        renderer.drawText(UI_10_FONT_ID, placeholderX, cardY + coverHeight / 2, placeholderTitle.c_str());
      }

      const int textY = cardY + coverHeight + 9;
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), cardWidth - 12);
      renderer.drawText(UI_10_FONT_ID, cardX + 6, textY, title.c_str(), true);
      char progress[24];
      snprintf(progress, sizeof(progress), "%u%% · %s", book.progressPercent,
               book.progressPercent >= 100 ? tr(STR_COMPLETE) : (book.progressPercent > 0 ? tr(STR_ONGOING) : tr(STR_NEW)));
      renderer.drawText(UI_10_FONT_ID, cardX + 6, textY + renderer.getLineHeight(UI_10_FONT_ID), progress);
      const int barY = cardY + cardHeight - gap - 7;
      renderer.drawRect(cardX + 6, barY, cardWidth - 12, 4);
      const int fillWidth = (cardWidth - 14) * book.progressPercent / 100;
      if (fillWidth > 0) renderer.fillRect(cardX + 7, barY + 1, fillWidth, 2);
    }
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH, renderDiagnosticReason);
}
#endif

bool RecentBooksActivity::renderCarousel(const bool threeCover) {
  const uint32_t renderStartedUs = micros();
  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  static const CarouselTheme carouselPresentation;
  // Carousel keeps Folio Nooir's existing header, battery, summary strip, and
  // controls. The presentation object is stateless; it is not the
  // active UITheme instance and therefore does not depend on an unsafe cast.
  static const FolioNooirTheme folioPresentation;
  const FolioShelfLayout folioLayout = folioPresentation.shelfLayout(renderer, metrics);
  const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  const bool carouselLayoutCacheHit = liteCarouselLayoutCacheValid &&
                                      liteCarouselLayoutCacheWidth == pageWidth &&
                                      liteCarouselLayoutCacheHeight == renderer.getScreenHeight() &&
                                      liteCarouselLayoutCacheStatsTop == folioLayout.statsTop &&
                                      liteCarouselLayoutCacheLineHeight == synopsisLineHeight;
  const uint32_t layoutStartedUs = micros();
  const CarouselLayout carouselLayout = carouselLayoutCacheHit
                                            ? liteCarouselLayoutCache
                                            : liteCarouselPageLayout(renderer, carouselPresentation, folioLayout);
  const uint32_t layoutDurationUs = micros() - layoutStartedUs;
  liteCarouselLayoutCache = carouselLayout;
  liteCarouselLayoutCacheValid = true;
  liteCarouselLayoutCacheWidth = pageWidth;
  liteCarouselLayoutCacheHeight = renderer.getScreenHeight();
  liteCarouselLayoutCacheStatsTop = folioLayout.statsTop;
  liteCarouselLayoutCacheLineHeight = synopsisLineHeight;
  const FolioShelfLayout displayLayout =
      liteBookDisplayLayout(folioLayout, carouselLayout, pageWidth);
  auto displayLiteHomeFrame = [&](const uint32_t detailsUs, const uint32_t shelfUs, const uint32_t statsUs) {
    const uint32_t elapsedUs = micros() - renderStartedUs;
    static uint32_t lastSelectionDiagnosticMs = 0;
    const uint32_t nowMs = millis();
    const bool initialFrame = renderDiagnosticReason != nullptr &&
                              (strcmp(renderDiagnosticReason, "initial") == 0 ||
                               strcmp(renderDiagnosticReason, "resume") == 0);
    const bool layoutFrame = renderDiagnosticReason != nullptr &&
                             strcmp(renderDiagnosticReason, "layout") == 0;
    const bool selectionFrame = renderDiagnosticReason != nullptr &&
                                strcmp(renderDiagnosticReason, "selection") == 0;
    const bool selectionSample = selectionFrame &&
                                 (lastSelectionDiagnosticMs == 0 || nowMs - lastSelectionDiagnosticMs >= 1000);
    if (elapsedUs >= 100000 || initialFrame || layoutFrame || selectionSample) {
      LOG_INF("SHELFPERF",
              "theme=Lite layout=%u reason=%s draw_us=%lu geometry_us=%lu geometry_cache=%u details_us=%lu "
              "shelf_us=%lu stats_us=%lu",
              static_cast<unsigned>(activeBookLayout()), renderDiagnosticReason ? renderDiagnosticReason : "unknown",
              static_cast<unsigned long>(elapsedUs), static_cast<unsigned long>(layoutDurationUs),
              carouselLayoutCacheHit ? 1U : 0U, static_cast<unsigned long>(detailsUs),
              static_cast<unsigned long>(shelfUs), static_cast<unsigned long>(statsUs));
      if (selectionSample) lastSelectionDiagnosticMs = nowMs;
    }
    displayHomeFrame(renderer, renderDiagnosticReason, renderStartedUs, activeBookLayout());
  };
  auto drawLiteShelfStats = [&] {
    const bool accumulated = !halClock.isAvailable();
    const uint32_t today = halClock.getDateKey();
    uint32_t middleSeconds = 0;
    uint16_t finishedCount = 0;
    for (const auto& book : recentBooks) {
      const uint32_t seconds = accumulated ? book.readingSeconds
                                           : (today != 0 && book.dailyReadingDateKey == today
                                                  ? book.dailyReadingSeconds
                                                  : 0);
      middleSeconds += std::min(seconds, UINT32_MAX - middleSeconds);
      if (book.progressPercent >= 100 && finishedCount < UINT16_MAX) ++finishedCount;
    }
    const uint32_t lastMinutes = recentBooks.empty() ? 0 : (recentBooks.front().lastSessionSeconds + 30) / 60;
    folioPresentation.drawShelfStats(renderer, folioLayout, lastMinutes, (middleSeconds + 30) / 60,
                                     finishedCount, accumulated);
  };

  const bool liteSpineMode = isFolioNooirLiteTheme(SETTINGS.uiTheme) &&
                             (usesSpineLayout() || usesTwoRowSpineLayout() || usesThreeRowSpineLayout());
  const uint8_t liteSpineLayout = liteSpineMode ? activeBookLayout() : 0xFF;
  size_t liteSpinePageStart = SIZE_MAX;
  const uint32_t spinePatchStartedUs = micros();
  if (liteSpineMode && visibleBookCount > 0) {
    if (usesSpineLayout()) {
      liteSpinePageStart = spinePageForSelection(spineShelfBounds(pageWidth, displayLayout)).page.firstIndex;
    } else if (usesTwoRowSpineLayout()) {
      liteSpinePageStart = twoRowSpinePageForSelection(twoRowSpineBounds(pageWidth, displayLayout)).page.firstIndex;
    } else {
      liteSpinePageStart =
          threeRowSpinePageForSelection(threeRowSpineBounds(pageWidth, displayLayout)).page.firstIndex;
    }
  }

  const bool previousLiteSpineFrameMatches =
      liteSpineMode && !initialRenderPending && lastRenderedSelectorIndex != SIZE_MAX &&
      lastRenderedLiteSpineLayout == liteSpineLayout && lastRenderedLiteSpinePageStart == liteSpinePageStart &&
      liteSpinePageStart != SIZE_MAX;
  const bool noOverlay = !overlayFrameShown && !menuPopup.isActive() && !bookActionsPopup.isActive();
  const bool noBackgroundWork = !retrievingBookCache && !manualSingleRefresh &&
                                !carouselHqPreparationActive && !carouselHqCompletionPopupPending &&
                                !spineShelfVisualsDirty;
  size_t restoredPageStart = SIZE_MAX;
  size_t restoredPageItems = 0;
  const bool restoredPageKnown = currentSnapshotPageInfo(restoredPageStart, restoredPageItems);
  const auto retainRenderedSnapshot = [this, restoredPageKnown, restoredPageStart, restoredPageItems] {
    if (!usesPersistentHomeSnapshot() || visibleBookCount == 0 || !restoredPageKnown) {
      snapshotRestored = false;
      return;
    }
    snapshotPageStart = restoredPageStart;
    snapshotPageItems = restoredPageItems;
    snapshotSelectorIndex = selectorIndex;
    snapshotPresentationKeyValue = snapshotPresentationKey();
    snapshotDynamicNeedsPatch = false;
    snapshotRewriteAfterRestore = false;
    captureSnapshotVisualState(restoredPageStart, restoredPageItems);
    snapshotRestored = true;
    snapshotWritePending = true;
    snapshotWriteRequestedMs = millis();
  };
  const auto restoreAction = !snapshotRestored
                                 ? HomeSnapshotPolicy::RestoreAction::Rebuild
                                 : snapshotDynamicNeedsPatch ? HomeSnapshotPolicy::RestoreAction::PatchDynamic
                                                             : HomeSnapshotPolicy::RestoreAction::Restore;
  const bool restoredPageMatches = restoredPageKnown && snapshotPageStart == restoredPageStart &&
                                   snapshotPageItems == restoredPageItems;
  // Lite three-row faces can patch progress and status when saved identities
  // and packed geometry still match; a structural move rebuilds the shelf.
  if (usesPersistentHomeSnapshot() && initialRenderPending && !snapshotFastPathUsed && visibleBookCount > 0 &&
      HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(restoreAction, restoredPageMatches, snapshotWritePending,
                                                          !noOverlay || !noBackgroundWork, liteSpineMode)) {
    // The SD snapshot already contains the selected cover for carousel layouts.
    // Grid and spine layouts save a clean base and need only the small focus
    // marker before the panel refresh.
    const uint32_t shelfStartedUs = micros();
    if (!usesCarouselLayout()) drawSnapshotSelectionOutline(displayLayout, selectorIndex, true);
    uint32_t shelfUs = micros() - shelfStartedUs;
    uint32_t detailsUs = 0;
    uint32_t statsUs = 0;
    if (snapshotDynamicNeedsPatch) {
      bool threeRowShelfPatched = false;
      const uint32_t coverPatchStartedUs = micros();
      if (liteSpineMode && usesThreeRowSpineLayout() &&
          HomeSnapshotPolicy::threeRowProgressIsDynamic(isFolioNooirLiteTheme(SETTINGS.uiTheme), true)) {
        const auto bounds = threeRowSpineBounds(pageWidth, displayLayout);
        const auto& spineWindow = threeRowSpinePageForSelection(bounds);
        std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentPathKeys{};
        std::array<uint64_t, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentVisualKeys{};
        std::array<HomeSnapshotPolicy::SnapshotRect, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> currentRects{};
        std::array<bool, SpineShelfPlanner::THREE_ROW_SLOT_CAPACITY> changedSlots{};
        for (size_t slotIndex = 0; slotIndex < spineWindow.page.itemCount; ++slotIndex) {
          const auto& slot = spineWindow.page.slots[slotIndex];
          if (slot.itemIndex < spineWindow.page.firstIndex) continue;
          const size_t localIndex = slot.itemIndex - spineWindow.page.firstIndex;
          if (localIndex >= snapshotBookVisualKeys.size() || localIndex >= spineWindow.books.size()) continue;
          const size_t recentIndex = recentIndexForVisibleIndex(slot.itemIndex);
          if (recentIndex == SIZE_MAX) continue;
          const auto& rect = slot.rect;
          const auto snapshotRect = HomeSnapshotPolicy::SnapshotRect{
              static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
              static_cast<int16_t>(rect.width), static_cast<int16_t>(rect.height)};
          currentPathKeys[localIndex] = hashText(recentBooks[recentIndex].path);
          const uint64_t visualKey = bookCoverVisualKey(recentBooks[recentIndex]);
          currentVisualKeys[localIndex] = visualKey;
          currentRects[localIndex] = snapshotRect;
          changedSlots[localIndex] = snapshotBookGeometryAvailable
                                         ? HomeSnapshotPolicy::slotNeedsPatch(
                                               snapshotBookPathKeys[localIndex], currentPathKeys[localIndex],
                                               snapshotBookVisualKeys[localIndex], visualKey,
                                               snapshotBookRects[localIndex], snapshotRect)
                                         : snapshotBookPathKeys[localIndex] == currentPathKeys[localIndex] &&
                                               snapshotBookVisualKeys[localIndex] != visualKey;
        }
        if (snapshotBookGeometryAvailable) {
          for (size_t slot = spineWindow.page.itemCount; slot < snapshotBookPathKeys.size(); ++slot) {
            changedSlots[slot] = snapshotBookPathKeys[slot] != 0;
          }
          // Clear every old face before drawing any moved faces. This prevents
          // an old slot from erasing a replacement whose new rectangle shares
          // its former position.
          for (size_t slot = 0; slot < changedSlots.size(); ++slot) {
            if (!changedSlots[slot] || !snapshotBookRects[slot].valid()) continue;
            const auto& oldRect = snapshotBookRects[slot];
            renderer.fillRect(oldRect.x, oldRect.y, oldRect.width, oldRect.height, false);
          }
        }
        for (size_t slotIndex = 0; slotIndex < spineWindow.page.itemCount; ++slotIndex) {
          const auto& slot = spineWindow.page.slots[slotIndex];
          if (slot.itemIndex < spineWindow.page.firstIndex) continue;
          const size_t localIndex = slot.itemIndex - spineWindow.page.firstIndex;
          if (localIndex >= changedSlots.size() || !changedSlots[localIndex] ||
              localIndex >= spineWindow.books.size()) continue;
          const size_t recentIndex = recentIndexForVisibleIndex(slot.itemIndex);
          if (recentIndex == SIZE_MAX) continue;
          drawSpineBook(renderer, spineWindow.books[localIndex], slot.rect, false, true,
                        spineWindow.thumbnailUnavailable[localIndex], this,
                        &RecentBooksActivity::spineThumbnailResolver, true);
          threeRowShelfPatched = true;
        }
        if (snapshotBookGeometryAvailable) {
          snapshotBookPathKeys = currentPathKeys;
          snapshotBookVisualKeys = currentVisualKeys;
          snapshotBookRects = currentRects;
          snapshotBookGeometryAvailable = true;
        } else {
          // Version 10 snapshots can still patch progress when every identity
          // and page position matched during restore; their current rectangles
          // are therefore the same as the saved ones.
          for (size_t slot = 0; slot < spineWindow.page.itemCount; ++slot) {
            if (currentPathKeys[slot] == snapshotBookPathKeys[slot]) {
              snapshotBookVisualKeys[slot] = currentVisualKeys[slot];
            }
          }
        }
      }
      if (threeRowShelfPatched) {
        // A selected cover redraw replaces its focus marker; restore the
        // small marker after all changed faces have been patched.
        drawSnapshotSelectionOutline(displayLayout, selectorIndex, true);
      }
      shelfUs += micros() - coverPatchStartedUs;
      const uint32_t detailsStartedUs = micros();
      drawLiteSelectedBookDetails(renderer, folioPresentation, carouselLayout,
                                  recentBooks[selectedRecentIndex()], true);
      detailsUs = micros() - detailsStartedUs;
      const uint32_t statsStartedUs = micros();
      drawLiteShelfStats();
      statsUs = micros() - statsStartedUs;
      snapshotDynamicNeedsPatch = false;
      if (threeRowShelfPatched) {
        snapshotWritePending = true;
        snapshotWriteRequestedMs = millis();
      }
    }
    displayLiteHomeFrame(detailsUs, shelfUs, statsUs);
    if (snapshotRewriteAfterRestore) {
      // Persist repaired slot pixels and current Recent state through the
      // existing idle-only writer; the just-restored frame stays eligible for
      // the fast path and does not wait behind a 48 KB SD write.
      snapshotWritePending = true;
      snapshotWriteRequestedMs = millis();
      snapshotRewriteAfterRestore = false;
    }
    markStatsReturnFrameReady();
    snapshotFastPathUsed = true;
    initialRenderPending = false;
    lastRenderedSelectorIndex = selectorIndex;
    lastRenderedPageStart = restoredPageStart;
    lastRenderedTab = activeTab;
    lastRenderedLiteLayout = activeBookLayout();
    lastRenderedLiteSpineLayout = liteSpineLayout;
    lastRenderedLiteSpinePageStart = liteSpinePageStart;
    if (visibleBookCount > 0) {
      const RecentBook& selected = recentBooks[selectedRecentIndex()];
      lastFeaturedPath = selected.path;
      lastFeaturedCoverPath = selected.coverBmpPath;
      lastFeaturedCoverVisible = SETTINGS.featuredShowCover != 0;
    }
    snapshotSelectorIndex = selectorIndex;
    snapshotPresentationKeyValue = snapshotPresentationKey();
    liteSelectedDetailsDirty = false;
    return true;
  }

  const bool selectionChanged = selectorIndex != lastRenderedSelectorIndex;
  const bool detailsNeedPatch = selectionChanged || liteSelectedDetailsDirty;
  const bool sameLiteSpineFrame = previousLiteSpineFrameMatches && activeTab == lastRenderedTab &&
                                  noOverlay && noBackgroundWork;
  const bool unchangedQueuedFrame =
      renderDiagnosticReason != nullptr && strcmp(renderDiagnosticReason, "queued") == 0 &&
      sameLiteSpineFrame && !selectionChanged && !liteSelectedDetailsDirty;
  if (unchangedQueuedFrame) {
    const uint32_t elapsedUs = micros() - renderStartedUs;
    LOG_INF("SHELFPERF", "theme=Lite layout=%u reason=queued_reuse draw_us=%lu shelf_us=0",
            static_cast<unsigned>(activeBookLayout()), static_cast<unsigned long>(elapsedUs));
    return true;
  }

  const bool popupActive = menuPopup.isActive() || bookActionsPopup.isActive();
  const bool retainedFrameIsShelf = !overlayFrameShown || litePopupFrameRetainsShelf;
  const bool sameLiteShelfFrame = !initialRenderPending && selectorIndex == lastRenderedSelectorIndex &&
                                  activeTab == lastRenderedTab && activeBookLayout() == lastRenderedLiteLayout &&
                                  liteSpineLayout == lastRenderedLiteSpineLayout &&
                                  liteSpinePageStart == lastRenderedLiteSpinePageStart;
  const bool noPopupBackgroundWork = noBackgroundWork && !liteSelectedDetailsDirty;
  if (FolioLiteLayoutPolicy::canDrawPopupOverRetainedShelf(popupActive, sameLiteShelfFrame,
                                                          noPopupBackgroundWork, retainedFrameIsShelf)) {
    // The completed Lite shelf is still in the renderer framebuffer. Paint
    // only the popup; render() forces one clean shelf rebuild after it closes.
    if (menuPopup.processRender(renderer, mappedInput) || bookActionsPopup.processRender(renderer, mappedInput)) {
      overlayFrameShown = true;
      litePopupFrameRetainsShelf = true;
      return false;
    }
  }

  // Any full Lite repaint replaces the retained frame. A subsequent option
  // popup may mark it reusable again after the complete shelf has been drawn.
  litePopupFrameRetainsShelf = false;

  if (visibleBookCount > 0 && FolioLiteLayoutPolicy::canPatchSpineSelection(
                                  previousLiteSpineFrameMatches,
                                  detailsNeedPatch,
                                  activeTab == lastRenderedTab, noOverlay, noBackgroundWork)) {
    const size_t previousIndex = lastRenderedSelectorIndex;
    if (selectionChanged) {
      if (usesSpineLayout()) {
        const auto& window = spinePageForSelection(spineShelfBounds(pageWidth, displayLayout));
        drawSpinePageSelectionMarker(renderer, window.page, previousIndex, false, renderedSpineFocus);
        drawSpinePageSelectionMarker(renderer, window.page, selectorIndex, true, renderedSpineFocus);
      } else if (usesTwoRowSpineLayout()) {
        const auto& window = twoRowSpinePageForSelection(twoRowSpineBounds(pageWidth, displayLayout));
        drawTwoRowSpinePageSelectionMarker(renderer, window.page, previousIndex, false, renderedSpineFocus);
        drawTwoRowSpinePageSelectionMarker(renderer, window.page, selectorIndex, true, renderedSpineFocus);
      } else {
        const auto& window = threeRowSpinePageForSelection(threeRowSpineBounds(pageWidth, displayLayout));
        drawThreeRowSpinePageSelectionMarker(renderer, window.page, previousIndex, false, renderedSpineFocus);
        drawThreeRowSpinePageSelectionMarker(renderer, window.page, selectorIndex, true, renderedSpineFocus);
      }
    }
    const uint32_t focusPatchUs = micros() - spinePatchStartedUs;
    const uint32_t detailsStartedUs = micros();
    drawLiteSelectedBookDetails(renderer, folioPresentation, carouselLayout,
                                recentBooks[selectedRecentIndex()], true);
    displayLiteHomeFrame(micros() - detailsStartedUs, focusPatchUs, 0);
    initialRenderPending = false;
    lastRenderedSelectorIndex = selectorIndex;
    lastRenderedPageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    lastRenderedTab = activeTab;
    lastRenderedLiteLayout = activeBookLayout();
    liteSelectedDetailsDirty = false;
    retainRenderedSnapshot();
    return true;
  }

  // Keep the existing full-frame path for layout changes, page changes,
  // overlays and background metadata work. Only same-page Lite spine selection
  // changes take the small label/focus update above.
  renderer.clearScreen();
  renderedSpineFocus.clear();
  folioPresentation.drawShelfTabs(renderer, folioLayout, activeTab);
  folioPresentation.drawShelfBattery(renderer, folioLayout, metrics);

  ThreeRowShelfRenderProfile threeRowProfile;
  bool profiledThreeRowShelf = false;
  uint32_t detailsDurationUs = 0;
  uint32_t shelfDurationUs = 0;
  if (visibleBookCount == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, carouselLayout.carouselRegion.y + 45,
                      tr(STR_NO_RECENT_BOOKS));
  } else {
    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    const uint32_t detailsStartedUs = micros();
    drawLiteSelectedBookDetails(renderer, folioPresentation, carouselLayout, selected, false);
    detailsDurationUs = micros() - detailsStartedUs;

    const auto markUnavailableThumbnails = [this](auto& window) {
      for (size_t slotIndex = 0; slotIndex < window.page.itemCount; ++slotIndex) {
        const auto& slot = window.page.slots[slotIndex];
        if (slot.itemIndex < window.page.firstIndex) continue;
        const size_t localIndex = slot.itemIndex - window.page.firstIndex;
        if (localIndex >= window.thumbnailUnavailable.size()) continue;
        if (normalThumbnailMisses.contains(window.recentIndexes[localIndex])) {
          window.thumbnailUnavailable[localIndex] = true;
        }
      }
    };
    const auto rememberUnavailableThumbnails = [this](auto& window) {
      for (size_t slotIndex = 0; slotIndex < window.page.itemCount; ++slotIndex) {
        const auto& slot = window.page.slots[slotIndex];
        if (slot.itemIndex < window.page.firstIndex) continue;
        const size_t localIndex = slot.itemIndex - window.page.firstIndex;
        if (localIndex < window.thumbnailUnavailable.size() && window.thumbnailUnavailable[localIndex]) {
          normalThumbnailMisses.markMissing(window.recentIndexes[localIndex]);
        }
      }
    };

    const uint32_t shelfStartedUs = micros();
    if (usesSpineLayout()) {
      const auto bounds = spineShelfBounds(pageWidth, displayLayout);
      const auto& spineWindow = spinePageForSelection(bounds);
      markUnavailableThumbnails(spineWindow);
      drawSpineShelf(renderer, spineWindow.page, spineWindow.books.data(), selectorIndex, bounds,
                     spineWindow.thumbnailUnavailable.data(), this, &RecentBooksActivity::spineThumbnailResolver, true);
      rememberUnavailableThumbnails(spineWindow);
      drawSpinePageSelectionMarker(renderer, spineWindow.page, selectorIndex, true, renderedSpineFocus);
      drawLitePageIndicator(renderer, carouselLayout, spineWindow.pageNumber, spineWindow.pageCount);
    } else if (usesTwoRowSpineLayout()) {
      const auto bounds = twoRowSpineBounds(pageWidth, displayLayout);
      const auto& spineWindow = twoRowSpinePageForSelection(bounds);
      markUnavailableThumbnails(spineWindow);
      drawTwoRowSpineShelf(renderer, spineWindow.page, spineWindow.books.data(), selectorIndex, bounds,
                           spineWindow.thumbnailUnavailable.data(), this, &RecentBooksActivity::spineThumbnailResolver, true);
      rememberUnavailableThumbnails(spineWindow);
      drawTwoRowSpinePageSelectionMarker(renderer, spineWindow.page, selectorIndex, true, renderedSpineFocus);
      drawLitePageIndicator(renderer, carouselLayout, spineWindow.pageNumber, spineWindow.pageCount);
    } else if (usesThreeRowSpineLayout()) {
      const auto bounds = threeRowSpineBounds(pageWidth, displayLayout);
      const auto& spineWindow = threeRowSpinePageForSelection(bounds);
      markUnavailableThumbnails(spineWindow);
      profiledThreeRowShelf = true;
      activeThreeRowShelfProfile = &threeRowProfile;
      drawThreeRowSpineShelf(renderer, spineWindow.page, spineWindow.books.data(), bounds,
                             spineWindow.thumbnailUnavailable.data(), this, &RecentBooksActivity::spineThumbnailResolver,
                             true, &threeRowProfile);
      activeThreeRowShelfProfile = nullptr;
      rememberUnavailableThumbnails(spineWindow);
      drawThreeRowSpinePageSelectionMarker(renderer, spineWindow.page, selectorIndex, true, renderedSpineFocus);
      drawLitePageIndicator(renderer, carouselLayout, spineWindow.pageNumber, spineWindow.pageCount);
    } else if (usesFourByTwoGrid()) {
      constexpr size_t pageItems = HomeSnapshotPolicy::FOUR_BY_TWO_PAGE_ITEMS;
      const size_t pageStart = (selectorIndex / pageItems) * pageItems;
      for (size_t slot = 0; slot < pageItems; ++slot) {
        const size_t index = pageStart + slot;
        if (index >= visibleBookCount) break;
        const size_t recentIndex = recentIndexForVisibleIndex(index);
        if (recentIndex == SIZE_MAX) continue;
        const RecentBook& book = recentBooks[recentIndex];
        const int cardX = displayLayout.gridGap + static_cast<int>(slot % 4) *
                                                     (displayLayout.cardWidth + displayLayout.gridGap);
        const int cardY = displayLayout.gridTop + static_cast<int>(slot / 4) * displayLayout.cardHeight;
        const int cardBottom = std::min(displayLayout.gridTop + displayLayout.gridHeight,
                                        cardY + displayLayout.cardHeight);
        const int labelHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
        const int coverBoxHeight = std::max(1, cardBottom - cardY - labelHeight - 5);
        const int coverBoxWidth = std::max(1, displayLayout.cardWidth - 8);
        const std::string thumbnail = UITheme::getCoverThumbPath(book.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);
        bool drewCover = false;
        if (!thumbnail.empty() && !normalThumbnailMisses.contains(recentIndex)) {
          HalFile file;
          if (Storage.openFileForRead("SHELF", thumbnail, file)) {
            Bitmap bitmap(file);
            if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
              const float scale = std::min(static_cast<float>(coverBoxWidth) / bitmap.getWidth(),
                                           static_cast<float>(coverBoxHeight) / bitmap.getHeight());
              const int coverWidth = std::max(1, static_cast<int>(bitmap.getWidth() * scale));
              const int coverHeight = std::max(1, static_cast<int>(bitmap.getHeight() * scale));
              renderer.drawBitmap(bitmap, cardX + (displayLayout.cardWidth - coverWidth) / 2, cardY,
                                  coverWidth, coverHeight);
              drewCover = true;
            }
          }
          if (!drewCover) normalThumbnailMisses.markMissing(recentIndex);
        }
        if (!drewCover) {
          const int placeholderHeight = std::min(coverBoxHeight, coverBoxWidth * 3 / 2);
          renderer.drawRect(cardX + (displayLayout.cardWidth - coverBoxWidth) / 2, cardY,
                            coverBoxWidth, placeholderHeight);
          const std::string shortTitle = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(),
                                                                std::max(1, coverBoxWidth - 8));
          renderer.drawCenteredText(UI_10_FONT_ID, cardY + placeholderHeight / 2, shortTitle.c_str());
        }
        const int coverHeight = std::min(coverBoxHeight, coverBoxWidth * 3 / 2);
        BookCoverTreatment::drawFrame(renderer, cardX + (displayLayout.cardWidth - coverBoxWidth) / 2,
                                      cardY, coverBoxWidth, coverHeight, false);
        if (SETTINGS.coverProgressStyle != CrossPointSettings::COVER_PROGRESS_NONE) {
          BookCoverTreatment::drawProgress(renderer, cardX + (displayLayout.cardWidth - coverBoxWidth) / 2,
                                           cardY, coverBoxWidth, coverHeight, book.progressPercent, false);
        }
        const std::string cardTitle = renderer.truncatedText(SMALL_FONT_ID, book.title.c_str(),
                                                              displayLayout.cardWidth - 4);
        renderer.drawText(SMALL_FONT_ID, cardX + 2, cardBottom - labelHeight, cardTitle.c_str());
        if (index == selectorIndex) {
          renderer.drawRect(cardX - 2, cardY - 2, displayLayout.cardWidth + 4,
                            std::max(1, cardBottom - cardY) + 4);
        }
      }
    } else {
      auto drawCoverOutline = [this](const CoverStackSlot& stackSlot) {
        const auto& rect = stackSlot.rect;
        const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
        const int rightX = rect.x + rect.width - 1;
        const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
        const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
        const int leftBottom = leftTop + stackSlot.leftHeight - 1;
        const int rightBottom = rightTop + stackSlot.rightHeight - 1;
        renderer.drawLine(rect.x, leftTop, rightX, rightTop);
        renderer.drawLine(rightX, rightTop, rightX, rightBottom);
        renderer.drawLine(rightX, rightBottom, rect.x, leftBottom);
        renderer.drawLine(rect.x, leftBottom, rect.x, leftTop);
      };

      CarouselSourceFrame frame;
      auto drawCover = [this, &drawCoverOutline, &frame](const RecentBook& book, const CoverStackSlot& stackSlot,
                                                         const size_t slotIndex) {
        const auto& rect = stackSlot.rect;
        const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
        const int fallbackWidth = std::min(rect.width, maxHeight * 2 / 3);
        const auto& planned = frame.slots[slotIndex];
        auto drawBitmap = [this, &rect, &stackSlot, &frame](const std::string& path) {
          CarouselSourceTiming sourceTiming;
          auto* source = getCarouselSource(path, sourceTiming, &frame);
          if (source == nullptr || source->bitmap == nullptr) return CarouselBitmapDrawResult::Unavailable;

          if (sourceTiming.reused) {
            const bool rewound = source->bitmap->rewindToData() == BmpReaderError::Ok;
            if (!rewound) {
              invalidateCarouselSourceLocked(path);
              return CarouselBitmapDrawResult::Failed;
            }
          }
          const CarouselBitmapDrawResult result =
              renderer.drawPerspectiveBitmap(*source->bitmap, rect.x, rect.y, rect.width, stackSlot.leftHeight,
                                             stackSlot.rightHeight)
                  ? CarouselBitmapDrawResult::Drawn
                  : CarouselBitmapDrawResult::Failed;
          if (result == CarouselBitmapDrawResult::Failed) invalidateCarouselSourceLocked(path);
          return result;
        };

        if (!planned.resolvedPath.empty()) {
          const auto primaryResult = drawBitmap(planned.resolvedPath);
          if (primaryResult == CarouselBitmapDrawResult::Drawn) {
            drawCoverOutline(stackSlot);
            return;
          }
          if (planned.resolvedHq && primaryResult == CarouselBitmapDrawResult::Unavailable) {
            carouselHqProbe(book).unavailable = true;
          }
          // Preserve the existing behavior if the HQ source opens correctly
          // but the actual row render fails: retry the normal source only then.
          if (planned.resolvedHq && drawBitmap(planned.fallbackPath) == CarouselBitmapDrawResult::Drawn) {
            drawCoverOutline(stackSlot);
            return;
          }
        }

        const int rightX = rect.x + rect.width - 1;
        const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
        const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
        const int leftBottom = leftTop + stackSlot.leftHeight - 1;
        const int rightBottom = rightTop + stackSlot.rightHeight - 1;
        const int silhouetteX[] = {rect.x, rightX, rightX, rect.x};
        const int silhouetteY[] = {leftTop, rightTop, rightBottom, leftBottom};
        renderer.fillPolygon(silhouetteX, silhouetteY, 4, false);
        drawCoverOutline(stackSlot);
        const int coverX = rect.x + (rect.width - fallbackWidth) / 2;
        const std::string placeholder = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), fallbackWidth - 10);
        renderer.drawText(UI_10_FONT_ID, coverX + 5, rect.y + maxHeight / 2, placeholder.c_str());
      };

      const auto stackSlots =
          threeCover
              ? CoverStackGeometry::layoutThree(pageWidth, visibleBookCount, selectorIndex, carouselLayout.centerHeight)
              : CoverStackGeometry::layout(pageWidth, visibleBookCount, selectorIndex, carouselLayout.centerHeight);
      prepareCarouselSourceFrame(stackSlots, frame);
      // Slots are returned in far-to-near order so a rear cover and its ribbon
      // can never paint over a nearer cover.
      for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
        const auto& stackSlot = stackSlots[slotIndex];
        if (!stackSlot.valid) continue;
        const size_t recentIndex = recentIndexForVisibleIndex(stackSlot.itemIndex);
        if (recentIndex == SIZE_MAX) continue;
        const RecentBook& book = recentBooks[recentIndex];
        const auto& rect = stackSlot.rect;
        drawCover(book, stackSlot, slotIndex);
        const bool selected = stackSlot.itemIndex == selectorIndex;
        BookCoverTreatment::drawPerspectiveFrame(renderer, rect.x, rect.y, rect.width, rect.height,
                                                 stackSlot.leftHeight, stackSlot.rightHeight, selected);
        BookCoverTreatment::drawPerspectiveProgress(renderer, rect.x, rect.y, rect.width, rect.height,
                                                    stackSlot.leftHeight, stackSlot.rightHeight, book.progressPercent,
                                                    selected);
      }
      const auto& center = stackSlots.back();
      if (center.valid) {
        renderer.drawRect(center.rect.x - 3, center.rect.y - 3, center.rect.width + 6, center.rect.height + 6);
      }
    }
    shelfDurationUs = micros() - shelfStartedUs;
    if (profiledThreeRowShelf && shelfDurationUs >= 100000) {
      LOG_INF("SHELFPERF",
              "theme=Lite layout=%u reason=%s shelf_us=%lu details_us=%lu furniture_us=%lu "
              "resolve_us=%lu sd_open_us=%lu bmp_header_us=%lu bitmap_draw_us=%lu slot_us=%lu "
              "slots=%u source_cache_hits=%u",
              static_cast<unsigned>(activeBookLayout()), renderDiagnosticReason ? renderDiagnosticReason : "unknown",
              static_cast<unsigned long>(shelfDurationUs), static_cast<unsigned long>(detailsDurationUs),
              static_cast<unsigned long>(threeRowProfile.furnitureUs),
              static_cast<unsigned long>(threeRowProfile.resolveUs),
              static_cast<unsigned long>(threeRowProfile.sourceOpenUs),
              static_cast<unsigned long>(threeRowProfile.headerParseUs),
              static_cast<unsigned long>(threeRowProfile.bitmapDrawUs),
              static_cast<unsigned long>(threeRowProfile.slotUs), static_cast<unsigned>(threeRowProfile.slots),
              static_cast<unsigned>(threeRowProfile.sourceCacheHits));
    }
  }

  const uint32_t statsStartedUs = micros();
  drawLiteShelfStats();
  const uint32_t statsDurationUs = micros() - statsStartedUs;

  if (menuPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    litePopupFrameRetainsShelf = true;
    return false;
  }
  if (bookActionsPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    litePopupFrameRetainsShelf = true;
    return false;
  }
  if (carouselHqPreparationActive) {
    const size_t total = recentBooks.size();
    const int progress = total == 0
                             ? 100
                             : std::min(99, static_cast<int>((carouselHqScanIndex * 100) / total));
    const std::string message = std::string(tr(STR_PREPARE_CAROUSEL_COVERS)) + "\n" +
                                std::to_string(carouselHqPreparedCount) + " / " + std::to_string(total);
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, progress);
    const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
    displayLiteHomeFrame(detailsDurationUs, shelfDurationUs, statsDurationUs);
    carouselHqPopupRendered = true;
    overlayFrameShown = true;
    litePopupFrameRetainsShelf = false;
    return false;
  }
  if (carouselHqCompletionPopupPending) {
    GUI.drawPopup(renderer, "All carousel covers are ready");
    displayLiteHomeFrame(detailsDurationUs, shelfDurationUs, statsDurationUs);
    carouselHqCompletionPopupPending = false;
    overlayFrameShown = true;
    litePopupFrameRetainsShelf = false;
    return false;
  }
  if (retrievingBookCache && retrievingBookCacheIndex == selectorIndex) {
    std::string bookName = "book";
    if (visibleBookCount > 0 && retrievingBookCacheIndex < visibleBookCount) {
      const RecentBook& book = recentBooks[selectedRecentIndex()];
      bookName = book.title.empty() ? book.path : book.title;
    }
    const std::string prefix = std::string(tr(STR_RETRIEVING_BOOK_DETAILS)) + ": ";
    const int nameWidth = std::max(40, renderer.getScreenWidth() -
                                           renderer.getTextWidth(UI_10_FONT_ID, prefix.c_str()) - 100);
    const std::string message = prefix + "\n" + renderer.truncatedText(UI_10_FONT_ID, bookName.c_str(), nameWidth) +
                                "\nProgress: " + std::to_string(retrievingBookCacheProgress) + "%";
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, retrievingBookCacheProgress);
    const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
    displayLiteHomeFrame(detailsDurationUs, shelfDurationUs, statsDurationUs);
    retrievingBookCachePopupRendered = true;
    overlayFrameShown = true;
    litePopupFrameRetainsShelf = false;
    return false;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_MENU), tr(STR_OPEN), tr(STR_LIBRARY),
                                            activeTab == 1 ? tr(STR_FINISHED) : tr(STR_RECENT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  displayLiteHomeFrame(detailsDurationUs, shelfDurationUs, statsDurationUs);
  if (manualSingleRefresh && !retrievingBookCache) manualSingleRefresh = false;
  initialRenderPending = false;
  lastRenderedSelectorIndex = selectorIndex;
  lastRenderedPageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
  lastRenderedTab = activeTab;
  lastRenderedLiteSpineLayout = liteSpineMode ? liteSpineLayout : 0xFF;
  lastRenderedLiteLayout = activeBookLayout();
  lastRenderedLiteSpinePageStart = liteSpineMode ? liteSpinePageStart : SIZE_MAX;
  spineShelfVisualsDirty = false;
  liteSelectedDetailsDirty = false;
  retainRenderedSnapshot();
  return true;
}

void RecentBooksActivity::renderFolioCarouselShelf(const int shelfTop, const int shelfHeight,
                                                    const bool threeCover) {
  if (shelfHeight <= 0) return;

  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, shelfTop, pageWidth, shelfHeight, false);
  if (visibleBookCount == 0) return;

  // This renderer deliberately uses the Folio shelf bounds only. The
  // independent Carousel theme keeps its own full-screen title, synopsis,
  // status, and button layout in renderCarousel().
  static const FolioNooirTheme folioPresentation;
  const auto stackSlots = CoverStackGeometry::layoutFolioShelf(pageWidth, shelfTop, shelfHeight,
                                                                visibleBookCount, selectorIndex, threeCover);
  CarouselSourceFrame frame;
  prepareCarouselSourceFrame(stackSlots, frame);

  auto drawCoverOutline = [this](const CoverStackSlot& stackSlot) {
    const auto& rect = stackSlot.rect;
    const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
    const int rightX = rect.x + rect.width - 1;
    const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
    const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
    const int leftBottom = leftTop + stackSlot.leftHeight - 1;
    const int rightBottom = rightTop + stackSlot.rightHeight - 1;
    renderer.drawLine(rect.x, leftTop, rightX, rightTop);
    renderer.drawLine(rightX, rightTop, rightX, rightBottom);
    renderer.drawLine(rightX, rightBottom, rect.x, leftBottom);
    renderer.drawLine(rect.x, leftBottom, rect.x, leftTop);
  };

  auto drawCover = [this, &drawCoverOutline, &frame](const RecentBook& book, const CoverStackSlot& stackSlot,
                                                    const size_t slotIndex) {
    const auto& rect = stackSlot.rect;
    const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
    const int fallbackWidth = std::max(1, std::min(rect.width, maxHeight * 2 / 3));
    const auto& planned = frame.slots[slotIndex];
    auto drawBitmap = [this, &rect, &stackSlot, &frame](const std::string& path) {
      CarouselSourceTiming sourceTiming;
      auto* source = getCarouselSource(path, sourceTiming, &frame);
      if (source == nullptr || source->bitmap == nullptr) return CarouselBitmapDrawResult::Unavailable;

      if (sourceTiming.reused) {
        const bool rewound = source->bitmap->rewindToData() == BmpReaderError::Ok;
        if (!rewound) {
          invalidateCarouselSourceLocked(path);
          return CarouselBitmapDrawResult::Failed;
        }
      }
      const CarouselBitmapDrawResult result =
          renderer.drawPerspectiveBitmap(*source->bitmap, rect.x, rect.y, rect.width, stackSlot.leftHeight,
                                         stackSlot.rightHeight)
              ? CarouselBitmapDrawResult::Drawn
              : CarouselBitmapDrawResult::Failed;
      if (result == CarouselBitmapDrawResult::Failed) invalidateCarouselSourceLocked(path);
      return result;
    };

    if (!planned.resolvedPath.empty()) {
      const auto primaryResult = drawBitmap(planned.resolvedPath);
      if (primaryResult == CarouselBitmapDrawResult::Drawn) {
        drawCoverOutline(stackSlot);
        return;
      }
      if (planned.resolvedHq && primaryResult == CarouselBitmapDrawResult::Unavailable) {
        carouselHqProbe(book).unavailable = true;
      }
      // Preserve the existing behavior if the HQ source opens correctly but
      // its row render fails: retry the normal source only then.
      if (planned.resolvedHq && drawBitmap(planned.fallbackPath) == CarouselBitmapDrawResult::Drawn) {
        drawCoverOutline(stackSlot);
        return;
      }
    }

    const int rightX = rect.x + rect.width - 1;
    const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
    const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
    const int leftBottom = leftTop + stackSlot.leftHeight - 1;
    const int rightBottom = rightTop + stackSlot.rightHeight - 1;
    const int silhouetteX[] = {rect.x, rightX, rightX, rect.x};
    const int silhouetteY[] = {leftTop, rightTop, rightBottom, leftBottom};
    renderer.fillPolygon(silhouetteX, silhouetteY, 4, false);
    drawCoverOutline(stackSlot);
    const int coverX = rect.x + (rect.width - fallbackWidth) / 2;
    const std::string placeholder = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(),
                                                            std::max(1, fallbackWidth - 10));
    renderer.drawText(UI_10_FONT_ID, coverX + 5, rect.y + maxHeight / 2, placeholder.c_str());
  };

  // CoverStackGeometry returns rear-to-front order; drawing in that order
  // keeps every nearer silhouette/bitmap opaque over the covers behind it.
  for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
    const auto& stackSlot = stackSlots[slotIndex];
    if (!stackSlot.valid) continue;
    const size_t recentIndex = recentIndexForVisibleIndex(stackSlot.itemIndex);
    if (recentIndex == SIZE_MAX) continue;
    const RecentBook& book = recentBooks[recentIndex];
    drawCover(book, stackSlot, slotIndex);
    const bool selected = stackSlot.itemIndex == selectorIndex;
    BookCoverTreatment::drawPerspectiveFrame(renderer, stackSlot.rect.x, stackSlot.rect.y,
                                             stackSlot.rect.width, stackSlot.rect.height,
                                             stackSlot.leftHeight, stackSlot.rightHeight, selected);
    BookCoverTreatment::drawPerspectiveProgress(renderer, stackSlot.rect.x, stackSlot.rect.y,
                                                stackSlot.rect.width, stackSlot.rect.height,
                                                stackSlot.leftHeight, stackSlot.rightHeight,
                                                book.progressPercent, selected);
  }

  const auto& center = stackSlots.back();
  if (center.valid) {
    renderer.drawRect(center.rect.x - 3, center.rect.y - 3, center.rect.width + 6, center.rect.height + 6);
  }
}

void RecentBooksActivity::render(RenderLock&&) {
  const uint32_t renderStartedUs = micros();
  renderer.beginDiagnosticFrame();
  if (!isFolioNooirLiteTheme(SETTINGS.uiTheme)) {
    lastRenderedLiteSpineLayout = 0xFF;
    lastRenderedLiteLayout = 0xFF;
    lastRenderedLiteSpinePageStart = SIZE_MAX;
  }
  // Keep the Folio bookshelf's featured book and 4x2 grid typography fixed;
  // UI Scale applies to the surrounding application screens instead.
  renderer.setUiScaleTextEnabled(false);
  if (isFolioNooirLiteTheme(SETTINGS.uiTheme)) {
    if (overlayFrameShown && !menuPopup.isActive() && !bookActionsPopup.isActive() && !retrievingBookCache &&
        !carouselHqPreparationActive) {
      overlayFrameShown = false;
      litePopupFrameRetainsShelf = false;
      // Popup pixels replaced part of the clean Home frame. Reload the saved
      // base into the existing framebuffer, then redraw only current details
      // and focus; decode the shelf only if that snapshot is invalid.
      initialRenderPending = true;
      lastRenderedLiteSpineLayout = 0xFF;
      lastRenderedLiteSpinePageStart = SIZE_MAX;
      snapshotFastPathUsed = false;
      snapshotRestored = usesPersistentHomeSnapshot() && restoreSnapshotAfterOverlayDismissal();
    }
    renderDiagnosticReason = (menuPopup.isActive() || bookActionsPopup.isActive()) ? "popup"
                              : initialRenderPending ? (snapshotRestored ? "resume" : "initial")
                              : activeTab != lastRenderedTab ? "tab"
                              : activeBookLayout() != lastRenderedLiteLayout ? "layout"
                              : selectorIndex != lastRenderedSelectorIndex ? "selection"
                              : manualSingleRefresh ? "manual_refresh"
                                                    : "queued";
    const bool frameRendered =
        renderCarousel(activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
    if (frameRendered) markStatsReturnFrameReady();
    return;
  }
  // Popups are drawn over the shelf framebuffer. On dismissal, the verified
  // clean snapshot is restored before any fast path can reuse those pixels.
  if (overlayFrameShown && !menuPopup.isActive() && !bookActionsPopup.isActive() && !retrievingBookCache &&
      !carouselHqPreparationActive) {
    overlayFrameShown = false;
    litePopupFrameRetainsShelf = false;
    snapshotFastPathUsed = false;
    snapshotRestored = usesPersistentHomeSnapshot() && restoreSnapshotAfterOverlayDismissal();
    if (!snapshotRestored) {
      lastRenderedSelectorIndex = SIZE_MAX;
      lastRenderedPageStart = SIZE_MAX;
      snapshotPageStart = SIZE_MAX;
      snapshotPageItems = 0;
    }
    initialRenderPending = true;
  }

  // A child screen can change visual settings while this shelf remains on the
  // activity stack. Compare only the compact in-memory presentation key here;
  // the full book/snapshot key remains reserved for the SD restore path.
  const bool spineLayoutForKey = usesSpineLayout() || usesTwoRowSpineLayout();
  const bool selectionMoved = selectorIndex != snapshotSelectorIndex && activeTab == lastRenderedTab &&
                              (usesPersistentHomeSnapshot() || spineLayoutForKey);
  if (snapshotRestored && snapshotPresentationKeyValue != snapshotPresentationKey() && !selectionMoved) {
    snapshotRestored = false;
    snapshotFastPathUsed = false;
    snapshotDynamicNeedsPatch = false;
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    lastRenderedSelectorIndex = SIZE_MAX;
    lastRenderedPageStart = SIZE_MAX;
    initialRenderPending = true;
  }

  const bool snapshotDynamicPatchPending = snapshotDynamicNeedsPatch;
  renderDiagnosticReason =
      (menuPopup.isActive() || bookActionsPopup.isActive()) ? "popup"
      : initialRenderPending                              ? (snapshotRestored ? "resume" : "initial")
      : activeTab != lastRenderedTab                      ? "tab"
      : selectorIndex != lastRenderedSelectorIndex        ? "selection"
      : manualSingleRefresh                               ? "manual_refresh"
      : snapshotDynamicPatchPending                       ? "state_patch"
                                                           : "queued";

  const bool threeCoverGrid = usesThreeCoverGrid();
  const bool fourByTwoGrid = usesFourByTwoGrid();
  const bool spineLayout = usesSpineLayout();
  const bool twoRowSpineLayout = usesTwoRowSpineLayout();
  const bool gridSnapshotLayout = fourByTwoGrid || threeCoverGrid;
  const bool snapshotLayout = usesPersistentHomeSnapshot();
  const bool frameReuseLayout = snapshotLayout;
  bool renderedFromSnapshot = frameReuseLayout && snapshotRestored;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int gridPageItems = usesCarouselLayout() ? static_cast<int>(HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS)
                          : threeCoverGrid ? static_cast<int>(HomeSnapshotPolicy::THREE_COVER_PAGE_ITEMS)
                                           : static_cast<int>(HomeSnapshotPolicy::FOUR_BY_TWO_PAGE_ITEMS);
  size_t currentPageStart = (selectorIndex / gridPageItems) * gridPageItems;
  size_t currentPageItems = static_cast<size_t>(gridPageItems);
  if ((usesCarouselLayout() || spineLayout || twoRowSpineLayout || usesThreeRowSpineLayout()) &&
      !currentSnapshotPageInfo(currentPageStart, currentPageItems)) {
    currentPageStart = selectorIndex;
    currentPageItems = 0;
  }
  // Settings and other non-library screens do not change the shelf data. If
  // the persisted frame matches the current book state, show that frame once
  // and add only the focus outline. This avoids a second full shelf rebuild
  // (cover decoding, synopsis wrapping, and grid geometry) on return.
  // Structural changes require a rebuild; dynamic progress and reading-state
  // changes take the targeted patch path before this fast return is allowed.
  if (snapshotLayout && renderedFromSnapshot &&
      initialRenderPending && !snapshotFastPathUsed && !overlayFrameShown && !menuPopup.isActive() &&
      !bookActionsPopup.isActive() && !retrievingBookCache && !manualSingleRefresh &&
      !snapshotDynamicNeedsPatch && snapshotPageStart == currentPageStart && snapshotPageItems == currentPageItems &&
      visibleBookCount > 0) {
    const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
    const FolioShelfLayout layout = homeShelfLayout(folioTheme, renderer, metrics);
    drawSnapshotSelectionOutline(layout, selectorIndex, true);
    displayHomeFrame(renderer, renderDiagnosticReason, renderStartedUs, activeBookLayout());
    markStatsReturnFrameReady();
    snapshotFastPathUsed = true;
    initialRenderPending = false;
    lastRenderedSelectorIndex = selectorIndex;
    lastRenderedPageStart = currentPageStart;
    lastRenderedTab = activeTab;
    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    lastFeaturedPath = selected.path;
    lastFeaturedCoverPath = selected.coverBmpPath;
    snapshotRestored = true;
    snapshotPageStart = currentPageStart;
    snapshotPageItems = currentPageItems;
    snapshotSelectorIndex = selectorIndex;
    return;
  }

  // A deferred request can arrive after a popup or cache task has already
  // completed. If the same Folio shelf frame is still on the panel, avoid
  // rebuilding all synopsis text and cover geometry and avoid another e-ink
  // refresh. Explicit cache refreshes and tab/selection changes invalidate
  // this fast path below.
  if (frameReuseLayout && !initialRenderPending &&
      !overlayFrameShown && !menuPopup.isActive() && !bookActionsPopup.isActive() && !retrievingBookCache &&
      !manualSingleRefresh && lastRenderedSelectorIndex == selectorIndex &&
      lastRenderedPageStart == currentPageStart && lastRenderedTab == activeTab && snapshotRestored &&
      snapshotPageItems == currentPageItems) {
    const bool sameFeatured = visibleBookCount == 0 || recentBooks[selectedRecentIndex()].path == lastFeaturedPath;
    if (sameFeatured) return;
  }

  // The shelf frame is already on the panel and in the renderer framebuffer
  // when a menu/long-press popup opens. Do not redraw all covers, synopsis
  // lines, statistics, and progress badges just to place the dialog on top.
  // OptionPopup::processRender() performs the single required display update.
  const bool popupOnlyRender =
      (menuPopup.isActive() || bookActionsPopup.isActive()) && frameReuseLayout && snapshotRestored && !initialRenderPending &&
      lastRenderedSelectorIndex == selectorIndex && lastRenderedPageStart == currentPageStart &&
      snapshotPageItems == currentPageItems;
  if (popupOnlyRender) {
    if (menuPopup.processRender(renderer, mappedInput) || bookActionsPopup.processRender(renderer, mappedInput)) {
      overlayFrameShown = true;
      return;
    }
  }

  // The snapshot is restored once in onEnter. Keep the framebuffer in RAM
  // while moving the selection; re-reading the full 48 KB snapshot from SD on
  // every button press was the source of the 1–2 second Recent/Finished lag.
  if (renderedFromSnapshot &&
      (snapshotPageStart != currentPageStart || snapshotPageItems != currentPageItems)) {
    renderedFromSnapshot = false;
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
  }
  if (!renderedFromSnapshot) {
    renderer.clearScreen();
    renderedSpineFocus.clear();
  }

  // The cover bookshelf is exclusive to the Folio Nooir UI theme. Other
  // themes retain CrossPoint's compact Recent Books list.
  if (!isFolioShelfTheme(SETTINGS.uiTheme)) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   tr(STR_MENU_RECENT_BOOKS));
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
    if (recentBooks.empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, top + 20, tr(STR_NO_RECENT_BOOKS));
    } else {
      GUI.drawList(renderer, Rect{0, top, pageWidth, height}, recentBooks.size(), selectorIndex,
                   [this](int index) { return recentBooks[index].title; },
                   [this](int index) { return recentBooks[index].author; },
                   [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); });
    }
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    displayHomeFrame(renderer, renderDiagnosticReason, renderStartedUs, activeBookLayout());
    markStatsReturnFrameReady();
    return;
  }

  const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = homeShelfLayout(folioTheme, renderer, metrics);
  folioTheme.drawShelfTabs(renderer, layout, activeTab);
  folioTheme.drawShelfBattery(renderer, layout, metrics);

  const int contentTop = layout.contentTop;
  const int contentHeight = layout.contentHeight;
  const int detailHeight = layout.detailHeight;
  auto drawStats = [&] {
    const bool accumulated = !halClock.isAvailable();
    const uint32_t today = halClock.getDateKey();
    uint32_t middleSeconds = 0;
    uint16_t finishedCount = 0;
    for (const auto& book : recentBooks) {
      const uint32_t seconds = accumulated ? book.readingSeconds
                                           : (today != 0 && book.dailyReadingDateKey == today
                                                  ? book.dailyReadingSeconds
                                                  : 0);
      middleSeconds += std::min(seconds, UINT32_MAX - middleSeconds);
      if (book.progressPercent >= 100 && finishedCount < UINT16_MAX) ++finishedCount;
    }
    const uint32_t lastMinutes = recentBooks.empty() ? 0 : (recentBooks.front().lastSessionSeconds + 30) / 60;
    folioTheme.drawShelfStats(renderer, layout, lastMinutes, (middleSeconds + 30) / 60, finishedCount, accumulated);
  };
  if (visibleBookCount == 0) {
    renderer.fillRect(0, contentTop, pageWidth, contentHeight, false);
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + detailHeight / 2, tr(STR_NO_RECENT_BOOKS), true);
  } else {
    auto drawCover = [this](const RecentBook& book, const size_t recentIndex, int x, int y, int maxWidth,
                            int maxHeight, bool drawBitmap, bool preferCarouselHq) {
      if (!drawBitmap) return;
      int width = std::min(maxWidth, maxHeight * 2 / 3);
      const std::string normalPath = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
      auto drawBitmapFromPath = [this, &book, &normalPath, recentIndex, x, y, maxWidth,
                                 maxHeight](const std::string& path) -> CarouselBitmapDrawResult {
        if (path.empty()) return CarouselBitmapDrawResult::Unavailable;
        auto& probe = carouselHqProbe(book);
        const bool normalThumbnail = path == normalPath;
        if (normalThumbnail &&
            (normalThumbnailMisses.contains(recentIndex) || probe.normalUnavailable)) {
          return CarouselBitmapDrawResult::Unavailable;
        }
        auto rememberNormalMiss = [this, &probe, normalThumbnail, recentIndex] {
          if (normalThumbnail) {
            probe.normalUnavailable = true;
            normalThumbnailMisses.markMissing(recentIndex);
          }
        };
        HalFile file;
        std::unique_ptr<Bitmap> ownedBitmap;
        Bitmap* bitmap = nullptr;
        if (usesSpineLayout() || usesTwoRowSpineLayout()) {
          CarouselSourceTiming sourceTiming;
          auto* source = getCarouselSource(path, sourceTiming);
          if (source == nullptr || source->bitmap == nullptr) {
            rememberNormalMiss();
            return CarouselBitmapDrawResult::Unavailable;
          }
          if (sourceTiming.reused && source->bitmap->rewindToData() != BmpReaderError::Ok) {
            invalidateCarouselSourceLocked(path);
            rememberNormalMiss();
            return CarouselBitmapDrawResult::Failed;
          }
          bitmap = source->bitmap.get();
        } else {
          if (!Storage.openFileForRead("SHELF", path, file)) {
            rememberNormalMiss();
            return CarouselBitmapDrawResult::Unavailable;
          }
          ownedBitmap.reset(new Bitmap(file));
          if (ownedBitmap->parseHeaders() != BmpReaderError::Ok) {
            rememberNormalMiss();
            return CarouselBitmapDrawResult::Unavailable;
          }
          bitmap = ownedBitmap.get();
        }
        if (bitmap->getWidth() > 0 && bitmap->getHeight() > 0) {
          // Fit inside the slot without distorting the cover, then center it
          // so the largest possible portion remains visible.
          const int sourceWidth = bitmap->getWidth();
          const int sourceHeight = bitmap->getHeight();
          int drawWidth = sourceWidth;
          int drawHeight = sourceHeight;
          if (sourceWidth > maxWidth || sourceHeight > maxHeight) {
            if (static_cast<long long>(sourceWidth) * maxHeight >
                static_cast<long long>(sourceHeight) * maxWidth) {
              drawWidth = maxWidth;
              drawHeight = std::max(1, sourceHeight * maxWidth / sourceWidth);
            } else {
              drawHeight = maxHeight;
              drawWidth = std::max(1, sourceWidth * maxHeight / sourceHeight);
            }
          }
          const int drawX = x + (maxWidth - drawWidth) / 2;
          const int drawY = y + (maxHeight - drawHeight) / 2;
          renderer.drawBitmap(*bitmap, drawX, drawY, drawWidth, drawHeight);
          renderer.drawRect(x, y, maxWidth, maxHeight);
          return CarouselBitmapDrawResult::Drawn;
        }
        rememberNormalMiss();
        return CarouselBitmapDrawResult::Unavailable;
      };

      if (preferCarouselHq && !book.coverBmpPath.empty()) {
        const std::string hqPath = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
        auto& hqProbe = carouselHqProbe(book);
        if (!hqProbe.unavailable) {
          const auto hqResult = drawBitmapFromPath(hqPath);
          if (hqResult == CarouselBitmapDrawResult::Drawn) return;
          if (hqResult == CarouselBitmapDrawResult::Unavailable) hqProbe.unavailable = true;
        }
      }
      if (drawBitmapFromPath(normalPath) == CarouselBitmapDrawResult::Drawn) return;

      const int coverX = x + (maxWidth - width) / 2;
      renderer.drawRect(coverX, y, width, maxHeight);
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), width - 10);
      renderer.drawText(UI_10_FONT_ID, coverX + 5, y + maxHeight / 2, title.c_str());
    };

    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    constexpr int detailPadding = 12;
    // Keep the Featured cover close to the Folio 3-Cover card width.  The
    // same 10px gap and three-column calculation are used by the 3-Cover
    // shelf below, so this scales with X3/X4 instead of using an X4 width.
    constexpr int threeCoverGap = 10;
    const int threeCoverCardWidth = (pageWidth - threeCoverGap * 4) / 3;
    const int detailCoverWidth = std::max(1, threeCoverCardWidth);
    const int detailCoverHeight = detailHeight - 20;
    const bool showFeaturedCover = SETTINGS.featuredShowCover != 0;
    const int detailX = showFeaturedCover ? detailPadding * 2 + detailCoverWidth : detailPadding;
    const int detailWidth = std::max(1, pageWidth - detailX - detailPadding);
    renderer.fillRect(detailX, contentTop + 1, detailWidth, detailHeight - 2, false);
    const size_t selectedSlot = selectorIndex % static_cast<size_t>(gridPageItems);
    const bool selectedCoverProgressChanged = gridSnapshotLayout && renderedFromSnapshot &&
                                              selectedSlot < snapshotBookVisualKeys.size() &&
                                              snapshotBookVisualKeys[selectedSlot] != bookCoverVisualKey(selected);
    const bool featuredCoverCached = !manualSingleRefresh && renderedFromSnapshot && lastFeaturedPath == selected.path &&
                                     lastFeaturedCoverPath == selected.coverBmpPath &&
                                     lastFeaturedCoverVisible == showFeaturedCover &&
                                     !selectedCoverProgressChanged;
    if (showFeaturedCover && !featuredCoverCached) {
      renderer.fillRect(detailPadding, contentTop + 1, detailCoverWidth, detailHeight - 2, false);
    }
    // Use the same 220px source as the Folio shelf cards.  The standalone
    // Carousel still owns the 360px preference; using it here would make the
    // same cover look darker after the separate HQ thumbnail is downsampled.
    if (showFeaturedCover && !featuredCoverCached) {
      drawCover(selected, selectedRecentIndex(), detailPadding, contentTop + 7, detailCoverWidth, detailCoverHeight,
                !featuredCoverCached, false);
      const unsigned long decorationStartedUs = micros();
      BookCoverTreatment::drawFrame(renderer, detailPadding, contentTop + 7, detailCoverWidth, detailCoverHeight,
                                    false, true);
      if (SETTINGS.coverProgressStyle != CrossPointSettings::COVER_PROGRESS_NONE) {
        BookCoverTreatment::drawProgress(renderer, detailPadding, contentTop + 7, detailCoverWidth, detailCoverHeight,
                                         selected.progressPercent, false, true);
      }
      LOG_DBG("PERF", "Home featured cover decorations=%luus", static_cast<unsigned long>(micros() - decorationStartedUs));
    }
#if NOOIR_EPUB_DIAGNOSTICS
    if (!lifecycleHomeCoverRecorded) {
      lifecycleHomeCoverRecorded = true;
      EpubDiagnostics::phaseRecord("boot_cover_ready");
    }
#endif
    lastFeaturedPath = selected.path;
    lastFeaturedCoverPath = selected.coverBmpPath;
    lastFeaturedCoverVisible = showFeaturedCover;
    if (showFeaturedCover) {
      renderer.drawLine(detailX - detailPadding / 2, contentTop + 10, detailX - detailPadding / 2,
                        contentTop + detailHeight - 10);
    }
    const BookMetadataOverride* metadataOverride = BOOK_METADATA_OVERRIDES.find(selected.path);
    const std::string& featuredTitle = metadataOverride && !metadataOverride->title.empty()
                                           ? metadataOverride->title
                                           : selected.title;
    const std::string& featuredAuthor = metadataOverride ? metadataOverride->author : selected.author;
    const std::string& featuredSynopsis = metadataOverride ? metadataOverride->synopsis : selected.synopsis;
    const BookState* selectedState = BOOK_STATES.find(selected.path);
    const uint8_t featuredProgress = std::min<uint8_t>(
        100, std::max<uint8_t>(selected.progressPercent, selectedState ? selectedState->progressPercent : 0));
    const uint32_t featuredReadingSeconds =
        std::max<uint32_t>(selected.readingSeconds, selectedState ? selectedState->readingSeconds : 0);
    const uint32_t featuredReadingMinutes = (featuredReadingSeconds + 30) / 60;
    const uint16_t featuredReadingSessions =
        std::max<uint16_t>(selected.readingSessions, selectedState ? selectedState->readingSessions : 0);
    const uint32_t featuredStartedDate = selectedState ? selectedState->startDate : 0;
    const uint32_t featuredFinishedDate = selectedState ? selectedState->finishDate : 0;
    StrId featuredStatusId = featuredProgress >= 100 ? StrId::STR_COMPLETE
                              : featuredProgress > 0 ? StrId::STR_ONGOING
                                                     : StrId::STR_NEW;
    if (selectedState != nullptr) {
      switch (selectedState->status) {
        case BookStatus::Finished: featuredStatusId = StrId::STR_COMPLETE; break;
        case BookStatus::OnHold: featuredStatusId = StrId::STR_ON_HOLD; break;
        case BookStatus::Reading: featuredStatusId = StrId::STR_ONGOING; break;
        case BookStatus::New: featuredStatusId = featuredProgress > 0 ? StrId::STR_ONGOING : StrId::STR_NEW; break;
      }
    }
    const uint16_t compactStatusKey = SETTINGS.featuredShowStatus ? static_cast<uint16_t>(featuredStatusId) : 0;
    const uint8_t compactProgressKey = SETTINGS.featuredShowProgress ? featuredProgress : 0;
    const uint32_t compactReadingMinutesKey = SETTINGS.featuredShowReadingTime ? featuredReadingMinutes : 0;
    const uint16_t compactSessionsKey = SETTINGS.featuredShowSessions ? featuredReadingSessions : 0;
    const uint64_t featuredPathHash = hashText(selected.path);
    const uint64_t featuredTitleHash = hashText(featuredTitle);
    const uint64_t featuredAuthorHash = SETTINGS.featuredShowAuthor ? hashText(featuredAuthor) : 0;
    const uint64_t featuredSynopsisHash = SETTINGS.featuredShowSynopsis ? hashText(featuredSynopsis) : 0;
    const uint32_t featuredStartedDateKey = SETTINGS.featuredShowStartedDate ? featuredStartedDate : 0;
    const uint32_t featuredFinishedDateKey = SETTINGS.featuredShowFinishedDate ? featuredFinishedDate : 0;

    uint16_t fieldMask = 0;
    const uint8_t fieldValues[] = {SETTINGS.featuredShowCover, SETTINGS.featuredShowFileType,
                                   SETTINGS.featuredShowAuthor, SETTINGS.featuredShowSynopsis, SETTINGS.featuredShowStatus,
                                   SETTINGS.featuredShowProgress, SETTINGS.featuredShowStartedDate,
                                   SETTINGS.featuredShowFinishedDate, SETTINGS.featuredShowReadingTime,
                                   SETTINGS.featuredShowSessions};
    for (size_t i = 0; i < std::size(fieldValues); ++i) {
      if (fieldValues[i]) fieldMask |= static_cast<uint16_t>(1u << i);
    }
    auto& featuredLayout = featuredTextLayout;
    const bool featuredLayoutChanged = !featuredLayout.valid || featuredLayout.pathHash != featuredPathHash ||
                                       featuredLayout.titleHash != featuredTitleHash ||
                                       featuredLayout.authorHash != featuredAuthorHash ||
                                       featuredLayout.synopsisHash != featuredSynopsisHash ||
                                       featuredLayout.status != compactStatusKey ||
                                       featuredLayout.progress != compactProgressKey ||
                                       featuredLayout.readingMinutes != compactReadingMinutesKey ||
                                       featuredLayout.sessions != compactSessionsKey ||
                                       featuredLayout.startedDate != featuredStartedDateKey ||
                                       featuredLayout.finishedDate != featuredFinishedDateKey ||
                                       featuredLayout.fields != fieldMask ||
                                       featuredLayout.language != SETTINGS.language ||
                                       featuredLayout.width != detailWidth || featuredLayout.height != detailHeight;
    if (featuredLayoutChanged) {
      const bool sameFeaturedPath = featuredLayout.valid && featuredLayout.pathHash == featuredPathHash;
      const bool cachedFileTypeResolved = sameFeaturedPath && featuredLayout.fileTypeResolved;
      const char* cachedFileTypeLabel = cachedFileTypeResolved ? featuredLayout.fileTypeLabel : nullptr;
      const int cachedFileTypeBadgeWidth = cachedFileTypeResolved ? featuredLayout.fileTypeBadgeWidth : 0;
      featuredLayout = {};
      featuredLayout.valid = true;
      featuredLayout.pathHash = featuredPathHash;
      featuredLayout.titleHash = featuredTitleHash;
      featuredLayout.authorHash = featuredAuthorHash;
      featuredLayout.synopsisHash = featuredSynopsisHash;
      featuredLayout.status = compactStatusKey;
      featuredLayout.progress = compactProgressKey;
      featuredLayout.readingMinutes = compactReadingMinutesKey;
      featuredLayout.sessions = compactSessionsKey;
      featuredLayout.startedDate = featuredStartedDateKey;
      featuredLayout.finishedDate = featuredFinishedDateKey;
      featuredLayout.fields = fieldMask;
      featuredLayout.language = SETTINGS.language;
      featuredLayout.width = detailWidth;
      featuredLayout.height = detailHeight;
      if (cachedFileTypeResolved) {
        featuredLayout.fileTypeResolved = true;
        featuredLayout.fileTypeLabel = cachedFileTypeLabel;
        featuredLayout.fileTypeBadgeWidth = cachedFileTypeBadgeWidth;
      } else if (SETTINGS.featuredShowFileType) {
        featuredLayout.fileTypeResolved = true;
        featuredLayout.fileTypeLabel = BookFormat::labelForPath(selected.path);
        featuredLayout.fileTypeBadgeWidth =
            FolioNooirTheme::featuredFormatBadgeLabelWidth(renderer, featuredLayout.fileTypeLabel);
      }

      const int titleLineHeight = std::max(1, renderer.getLineHeight(UI_12_FONT_ID));
      const int smallLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
      const int textTop = contentTop + 13;
      const int panelBottom = contentTop + detailHeight - 10;
      const int usableTextHeight = std::max(0, panelBottom - textTop);
      const bool showAuthor = SETTINGS.featuredShowAuthor != 0 && !featuredAuthor.empty();
      const bool showSynopsis = SETTINGS.featuredShowSynopsis != 0;
      const bool showCompactMetadata = SETTINGS.featuredShowStatus || SETTINGS.featuredShowProgress ||
                                       SETTINGS.featuredShowReadingTime || SETTINGS.featuredShowSessions;
      const int compactMetadataHeight = showCompactMetadata
                                            ? smallLineHeight + 1 + (SETTINGS.featuredShowProgress ? 10 : 0)
                                            : 0;

      int startedYear = 0;
      int startedMonth = 0;
      int startedDay = 0;
      int finishedYear = 0;
      int finishedMonth = 0;
      int finishedDay = 0;
      const bool showStartedDate = SETTINGS.featuredShowStartedDate &&
                                   StatisticsDate::split(featuredStartedDate, startedYear, startedMonth, startedDay);
      const bool showFinishedDate = SETTINGS.featuredShowFinishedDate &&
                                    StatisticsDate::split(featuredFinishedDate, finishedYear, finishedMonth,
                                                          finishedDay);
      const bool showDates = showStartedDate || showFinishedDate;
      const int dateLineHeight = showDates ? smallLineHeight + 1 : 0;
      const int authorHeight = showAuthor ? smallLineHeight + 3 : 0;
      const int supportBelowSynopsisHeight = compactMetadataHeight + dateLineHeight;
      const bool titleOnly = !showAuthor && !showSynopsis && !showCompactMetadata && !showDates;
      featuredLayout.titleOnly = titleOnly;
      const int titleAvailableHeight =
          std::max(0, usableTextHeight - authorHeight - supportBelowSynopsisHeight -
                           (showSynopsis ? smallLineHeight : 0));
      const int titleAvailableLines = titleAvailableHeight / titleLineHeight;
      const int titleMaxLines = titleOnly ? std::max(1, usableTextHeight / titleLineHeight)
                                 : showSynopsis ? std::max(1, std::min(2, titleAvailableLines))
                                                : std::max(1, titleAvailableLines);
      const int visibleFileTypeBadgeWidth =
          SETTINGS.featuredShowFileType ? featuredLayout.fileTypeBadgeWidth : 0;
      const int titleWidth = std::max(1, detailWidth - visibleFileTypeBadgeWidth -
                                                (visibleFileTypeBadgeWidth > 0 ? 8 : 0));
      featuredLayout.titleLines = renderer.wrappedText(UI_12_FONT_ID, featuredTitle.c_str(), titleWidth, titleMaxLines);
      if (featuredLayout.titleLines.empty()) featuredLayout.titleLines.emplace_back(tr(STR_BOOKSHELF));

      if (showAuthor && !featuredAuthor.empty()) {
        featuredLayout.authorLine = renderer.truncatedText(UI_10_FONT_ID, featuredAuthor.c_str(), detailWidth);
      }

      if (showCompactMetadata) {
        char metadata[128] = {};
        size_t metadataLength = 0;
        const auto appendComponent = [&metadata, &metadataLength](const char* component) {
          if (component == nullptr || component[0] == '\0' || metadataLength >= sizeof(metadata) - 1) return;
          const size_t remaining = sizeof(metadata) - metadataLength;
          const int written = snprintf(metadata + metadataLength, remaining, "%s%s",
                                       metadataLength > 0 ? " · " : "", component);
          if (written > 0) metadataLength += std::min(static_cast<size_t>(written), remaining - 1);
        };
        if (SETTINGS.featuredShowStatus) appendComponent(I18N.get(featuredStatusId));
        char component[32];
        if (SETTINGS.featuredShowProgress) {
          snprintf(component, sizeof(component), "%u%%", featuredProgress);
          appendComponent(component);
        }
        if (SETTINGS.featuredShowReadingTime) {
          snprintf(component, sizeof(component), "%lu %s", static_cast<unsigned long>(featuredReadingMinutes),
                   tr(STR_MIN_SHORT));
          appendComponent(component);
        }
        if (SETTINGS.featuredShowSessions) {
          const char* sessionLabel = featuredReadingSessions == 1 ? tr(STR_SESSION_SINGULAR) : tr(STR_SESSION_PLURAL);
          snprintf(component, sizeof(component), "%u %s", featuredReadingSessions, sessionLabel);
          appendComponent(component);
        }
        if (metadataLength > 0) {
          featuredLayout.compactMetadataLine = renderer.truncatedText(SMALL_FONT_ID, metadata, detailWidth);
        }
      }

      if (showSynopsis) {
        const int titleHeight = static_cast<int>(featuredLayout.titleLines.size()) * titleLineHeight;
        const int synopsisTop = textTop + titleHeight + authorHeight;
        const int synopsisAvailableHeight =
            std::max(0, panelBottom - synopsisTop - supportBelowSynopsisHeight);
        const int synopsisMaxLines = synopsisAvailableHeight >= smallLineHeight
                                         ? 1 + (synopsisAvailableHeight - smallLineHeight) / (smallLineHeight + 1)
                                         : 0;
        const std::string synopsisPreview = SynopsisPreview::firstWords(featuredSynopsis);
        const char* synopsisText = synopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : synopsisPreview.c_str();
        if (synopsisMaxLines > 0) {
          featuredLayout.synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsisText, detailWidth,
                                                               synopsisMaxLines);
        }
      }

      if (showDates) {
        char dates[72];
        char startText[16] = "-";
        char finishText[16] = "-";
        if (showStartedDate)
          snprintf(startText, sizeof(startText), "%04d-%02d-%02d", startedYear, startedMonth, startedDay);
        if (showFinishedDate)
          snprintf(finishText, sizeof(finishText), "%04d-%02d-%02d", finishedYear, finishedMonth, finishedDay);
        if (showStartedDate && showFinishedDate)
          snprintf(dates, sizeof(dates), "%s %s · %s %s", tr(STR_STARTED_DATE), startText,
                   tr(STR_STATS_FINISHED_DATE), finishText);
        else if (showStartedDate)
          snprintf(dates, sizeof(dates), "%s %s", tr(STR_STARTED_DATE), startText);
        else
          snprintf(dates, sizeof(dates), "%s %s", tr(STR_STATS_FINISHED_DATE), finishText);
        featuredLayout.dateLine = renderer.truncatedText(SMALL_FONT_ID, dates, detailWidth);
      }
    }

    const int titleLineHeight = std::max(1, renderer.getLineHeight(UI_12_FONT_ID));
    const int smallLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
    const int panelBottom = contentTop + detailHeight - 10;
    const int textTop = contentTop + 13;
    const int usableTextHeight = std::max(0, panelBottom - textTop);
    const int titleBlockHeight = static_cast<int>(featuredLayout.titleLines.size()) * titleLineHeight;
    int textY = featuredLayout.titleOnly ? textTop + (usableTextHeight - titleBlockHeight) / 2 : textTop;
    const int titleTop = textY;
    for (const auto& line : featuredLayout.titleLines) {
      if (textY + titleLineHeight > panelBottom) break;
      renderer.drawText(UI_12_FONT_ID, detailX, textY, line.c_str(), true, EpdFontFamily::BOLD);
      textY += titleLineHeight;
    }
    if (SETTINGS.featuredShowFileType && featuredLayout.fileTypeBadgeWidth > 0 &&
        titleTop + titleLineHeight <= panelBottom) {
      FolioNooirTheme::drawFeaturedFormatBadgeLabel(renderer, featuredLayout.fileTypeLabel,
                                                   detailX + detailWidth, titleTop - 3);
    }
    if (!featuredLayout.authorLine.empty() && textY + smallLineHeight <= panelBottom) {
      renderer.drawText(UI_10_FONT_ID, detailX, textY + 2, featuredLayout.authorLine.c_str());
      textY += smallLineHeight + 3;
    }
    if (SETTINGS.featuredShowSynopsis) {
      for (const auto& line : featuredLayout.synopsisLines) {
        if (textY + smallLineHeight > panelBottom) break;
        renderer.drawText(SMALL_FONT_ID, detailX, textY, line.c_str());
        textY += smallLineHeight + 1;
      }
    }

    if (!featuredLayout.compactMetadataLine.empty() && textY + smallLineHeight <= panelBottom) {
      renderer.drawText(SMALL_FONT_ID, detailX, textY, featuredLayout.compactMetadataLine.c_str());
      textY += smallLineHeight + 1;
      if (SETTINGS.featuredShowProgress && textY + 7 <= panelBottom) {
        const int progressY = textY + 1;
        renderer.drawRect(detailX, progressY, detailWidth, 7);
        const int fill = (detailWidth - 2) * featuredProgress / 100;
        if (fill > 0) renderer.fillRect(detailX + 1, progressY + 1, fill, 5);
        textY = progressY + 9;
      }
    }
    if (!featuredLayout.dateLine.empty() && textY + smallLineHeight <= panelBottom) {
      renderer.drawText(SMALL_FONT_ID, detailX, textY, featuredLayout.dateLine.c_str());
      textY += smallLineHeight + 1;
    }
    renderer.drawLine(0, contentTop + detailHeight - 1, pageWidth - 1, contentTop + detailHeight - 1);
    if (usesCarouselLayout()) {
      renderFolioCarouselShelf(layout.gridTop, layout.gridHeight,
                               activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
    } else if (spineLayout) {
      if (!renderedFromSnapshot) {
        const SpineShelfPlanner::Rect bounds = spineShelfBounds(pageWidth, layout);
        const auto& spineWindow = spinePageForSelection(bounds);
        for (size_t slotIndex = 0; slotIndex < spineWindow.page.itemCount; ++slotIndex) {
          const auto& slot = spineWindow.page.slots[slotIndex];
          if (slot.itemIndex < spineWindow.page.firstIndex) continue;
          const size_t localIndex = slot.itemIndex - spineWindow.page.firstIndex;
          if (localIndex >= spineWindow.thumbnailUnavailable.size()) continue;
          const size_t recentIndex = spineWindow.recentIndexes[localIndex];
          if (normalThumbnailMisses.contains(recentIndex)) {
            spineWindow.thumbnailUnavailable[localIndex] = true;
          }
        }
        drawSpineShelf(renderer, spineWindow.page, spineWindow.books.data(), selectorIndex, bounds,
                       spineWindow.thumbnailUnavailable.data(), this, &RecentBooksActivity::spineThumbnailResolver);
        for (size_t slotIndex = 0; slotIndex < spineWindow.page.itemCount; ++slotIndex) {
          const auto& slot = spineWindow.page.slots[slotIndex];
          if (slot.itemIndex < spineWindow.page.firstIndex) continue;
          const size_t localIndex = slot.itemIndex - spineWindow.page.firstIndex;
          if (localIndex >= spineWindow.thumbnailUnavailable.size() ||
              !spineWindow.thumbnailUnavailable[localIndex]) {
            continue;
          }
          const size_t recentIndex = spineWindow.recentIndexes[localIndex];
          normalThumbnailMisses.markMissing(recentIndex);
        }
        folioTheme.drawPageIndicator(renderer, layout, spineWindow.pageNumber, spineWindow.pageCount);
      }
    } else if (twoRowSpineLayout) {
      if (!renderedFromSnapshot) {
        const SpineShelfPlanner::Rect bounds = twoRowSpineBounds(pageWidth, layout);
        const auto& spineWindow = twoRowSpinePageForSelection(bounds);
        for (size_t slotIndex = 0; slotIndex < spineWindow.page.itemCount; ++slotIndex) {
          const auto& slot = spineWindow.page.slots[slotIndex];
          if (slot.itemIndex < spineWindow.page.firstIndex) continue;
          const size_t localIndex = slot.itemIndex - spineWindow.page.firstIndex;
          if (localIndex >= spineWindow.thumbnailUnavailable.size()) continue;
          const size_t recentIndex = spineWindow.recentIndexes[localIndex];
          if (normalThumbnailMisses.contains(recentIndex)) {
            spineWindow.thumbnailUnavailable[localIndex] = true;
          }
        }
        drawTwoRowSpineShelf(renderer, spineWindow.page, spineWindow.books.data(), selectorIndex, bounds,
                             spineWindow.thumbnailUnavailable.data(), this, &RecentBooksActivity::spineThumbnailResolver);
        for (size_t slotIndex = 0; slotIndex < spineWindow.page.itemCount; ++slotIndex) {
          const auto& slot = spineWindow.page.slots[slotIndex];
          if (slot.itemIndex < spineWindow.page.firstIndex) continue;
          const size_t localIndex = slot.itemIndex - spineWindow.page.firstIndex;
          if (localIndex >= spineWindow.thumbnailUnavailable.size() ||
              !spineWindow.thumbnailUnavailable[localIndex]) {
            continue;
          }
          const size_t recentIndex = spineWindow.recentIndexes[localIndex];
          normalThumbnailMisses.markMissing(recentIndex);
        }
        folioTheme.drawPageIndicator(renderer, layout, spineWindow.pageNumber, spineWindow.pageCount);
      }
    } else {
      const int columns = threeCoverGrid ? 3 : layout.columns;
      const int gap = threeCoverGrid ? 10 : layout.gridGap;
      const int gridTop = layout.gridTop;
      const int gridHeight = layout.gridHeight;
      const int cardWidth = threeCoverGrid ? (pageWidth - gap * (columns + 1)) / columns : layout.cardWidth;
      const int cardHeight = threeCoverGrid ? gridHeight : layout.cardHeight;
      const size_t pageStart = (selectorIndex / gridPageItems) * gridPageItems;
      unsigned long coverDecorationUs = 0;
      size_t decoratedCardCount = 0;
      for (int slot = 0; slot < gridPageItems; ++slot) {
        const size_t index = pageStart + static_cast<size_t>(slot);
        if (index >= visibleBookCount) break;
        const size_t recentIndex = recentIndexForVisibleIndex(index);
        if (recentIndex == SIZE_MAX) continue;
        const RecentBook& book = recentBooks[recentIndex];
        const int x = gap + (slot % columns) * (cardWidth + gap);
        const int y = threeCoverGrid
                          ? gridTop + std::max(0, (gridHeight - std::min(gridHeight - 20, cardWidth * 3 / 2)) / 2)
                          : gridTop + (slot / columns) * cardHeight;
        const int coverHeight = threeCoverGrid
                                    ? std::min(gridHeight - 20, cardWidth * 3 / 2)
                                    : std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
        if (!threeCoverGrid) {
          renderer.fillRect(x, y + coverHeight, cardWidth, std::max(1, cardHeight - coverHeight), false);
        }
        const uint64_t coverVisualKey = bookCoverVisualKey(book);
        const bool dynamicCoverChanged = gridSnapshotLayout && renderedFromSnapshot &&
                                         snapshotBookVisualKeys[static_cast<size_t>(slot)] != coverVisualKey;
        const bool refreshCard = !renderedFromSnapshot || (manualSingleRefresh && index == selectorIndex) ||
                                 dynamicCoverChanged;
        if (refreshCard) {
          renderer.fillRect(x, y, cardWidth, coverHeight, false);
          drawCover(book, recentIndex, x, y, cardWidth, coverHeight, true, false);
          const unsigned long decorationStartedUs = micros();
          BookCoverTreatment::drawFrame(renderer, x, y, cardWidth, coverHeight, false);
          if (SETTINGS.coverProgressStyle != CrossPointSettings::COVER_PROGRESS_NONE) {
            BookCoverTreatment::drawProgress(renderer, x, y, cardWidth, coverHeight, book.progressPercent, false);
          }
          coverDecorationUs += static_cast<unsigned long>(micros() - decorationStartedUs);
          ++decoratedCardCount;
        }
        if (gridSnapshotLayout) snapshotBookVisualKeys[static_cast<size_t>(slot)] = coverVisualKey;
      }
      if (decoratedCardCount > 0) {
        LOG_DBG("PERF", "Home grid cover decorations cards=%u total=%luus",
                static_cast<unsigned>(decoratedCardCount), coverDecorationUs);
      }
      const size_t pageCount = (visibleBookCount + gridPageItems - 1) / gridPageItems;
      folioTheme.drawPageIndicator(renderer, layout, selectorIndex / gridPageItems + 1, pageCount);
    }

  }
  drawStats();

  // The base frame is kept in RAM while navigating within a page. Erase only
  // the previous focus outline instead of restoring the whole SD snapshot.
  if (snapshotLayout && renderedFromSnapshot && lastRenderedSelectorIndex != SIZE_MAX &&
      lastRenderedSelectorIndex != selectorIndex && lastRenderedPageStart == currentPageStart) {
    drawSnapshotSelectionOutline(layout, lastRenderedSelectorIndex, false);
  }

  // Persist the base frame before adding the current selection outline. The
  // next button press restores this clean frame and draws one outline only.
  if (snapshotLayout && !renderedFromSnapshot && (initialRenderPending || snapshotPageStart != currentPageStart)) {
    // Persist after the frame has become interactive; see the idle write in
    // loop(). Keeping the old snapshot invalid until then prevents a stale
    // page from being treated as the current one during rapid navigation.
    snapshotWritePending = true;
    snapshotWriteRequestedMs = millis();
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
  }
  if (snapshotLayout && visibleBookCount > 0) drawSnapshotSelectionOutline(layout, selectorIndex, true);

  if (menuPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    return;
  }
  if (bookActionsPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    return;
  }
  if (carouselHqCompletionPopupPending) {
    GUI.drawPopup(renderer, "All carousel covers are ready");
    displayHomeFrame(renderer, renderDiagnosticReason, renderStartedUs, activeBookLayout());
    carouselHqCompletionPopupPending = false;
    overlayFrameShown = true;
    return;
  }
  if (retrievingBookCache && retrievingBookCacheIndex == selectorIndex) {
    std::string bookName = "book";
    if (visibleBookCount > 0 && retrievingBookCacheIndex < visibleBookCount) {
      const RecentBook& book = recentBooks[selectedRecentIndex()];
      bookName = book.title.empty() ? book.path : book.title;
    }
    const std::string prefix = std::string(tr(STR_RETRIEVING_BOOK_DETAILS)) + ": ";
    const int nameWidth = std::max(40, renderer.getScreenWidth() -
                                           renderer.getTextWidth(UI_10_FONT_ID, prefix.c_str()) - 100);
    const std::string message = prefix + "\n" + renderer.truncatedText(UI_10_FONT_ID, bookName.c_str(), nameWidth) +
                                "\nProgress: " + std::to_string(retrievingBookCacheProgress) + "%";
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, retrievingBookCacheProgress);
    const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
    displayHomeFrame(renderer, renderDiagnosticReason, renderStartedUs, activeBookLayout());
    retrievingBookCachePopupRendered = true;
    overlayFrameShown = true;
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_MENU), tr(STR_OPEN), tr(STR_LIBRARY),
                                            activeTab == 1 ? tr(STR_FINISHED) : tr(STR_RECENT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // Spine navigation retains the shelf in the framebuffer and updates only
  // the focus strokes. Reassert the selected frame after all content and hint
  // cleanup so no later paint can cover one side before the full-panel update.
  if ((spineLayout || twoRowSpineLayout) && visibleBookCount > 0) {
    drawSnapshotSelectionOutline(layout, selectorIndex, true);
  }
  displayHomeFrame(renderer, renderDiagnosticReason, renderStartedUs, activeBookLayout());
  markStatsReturnFrameReady();
  if (manualSingleRefresh && !retrievingBookCache) manualSingleRefresh = false;
  if (initialRenderPending) {
    initialRenderPending = false;
  }
  lastRenderedSelectorIndex = selectorIndex;
  lastRenderedPageStart = currentPageStart;
  lastRenderedTab = activeTab;
  if (snapshotLayout && visibleBookCount > 0 && (!renderedFromSnapshot || snapshotDynamicPatchPending)) {
    captureSnapshotVisualState(currentPageStart, currentPageItems);
  }
  snapshotPresentationKeyValue = snapshotPresentationKey();
  if (snapshotLayout && snapshotDynamicPatchPending) {
    // A dynamic-only patch has updated the frame already. Refresh the SD copy
    // after the user pauses so the next activity return will not patch it again.
    snapshotWritePending = true;
    snapshotWriteRequestedMs = millis();
  }
  // The framebuffer is already a valid in-memory base frame even when its
  // optional SD snapshot is still waiting for the idle write. Keep using it
  // for immediate button navigation; otherwise the first button press after
  // boot would decode all eight covers again before the deferred write.
  if (frameReuseLayout && visibleBookCount > 0) {
    snapshotRestored = true;
    snapshotPageStart = currentPageStart;
    snapshotPageItems = currentPageItems;
    snapshotSelectorIndex = selectorIndex;
    if (spineLayout || twoRowSpineLayout) snapshotPresentationKeyValue = snapshotPresentationKey();
  } else {
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotPageItems = 0;
    snapshotSelectorIndex = SIZE_MAX;
  }
}
