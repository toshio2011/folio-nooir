/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <climits>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "BleInput.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/home/ReadingStatsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "BookStateStore.h"
#include "ReadingStatsStore.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "XtcReaderBookmarksActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkFile.h"
#include "util/QuickActions.h"
#include "util/ScreenshotUtil.h"
#include "activities/reader/XtcRenderMemoryPolicy.h"
#include <XtcFrameGeometry.h>

namespace {
enum class XtchRenderPass { Base, Lsb, Msb };

// XTCH stores two 2-bit planes in column-major order. Keep the page itself out
// of heap memory, but use a larger SD chunk and preserve the B/W framebuffer in
// GfxRenderer's non-contiguous 8 KB chunks so the base plane is not reread.
bool mappedPixelRect(const xtc_frame::Mapping& mapping, const uint16_t sourceX, const uint16_t sourceY,
                     xtc_frame::Rect& destination) {
  const auto& source = mapping.source;
  if (!mapping.valid || sourceX < source.x || sourceY < source.y ||
      static_cast<uint32_t>(sourceX) >= static_cast<uint32_t>(source.x) + source.width ||
      static_cast<uint32_t>(sourceY) >= static_cast<uint32_t>(source.y) + source.height) {
    return false;
  }
  if (source.x == 0 && source.y == 0 && mapping.destination.width == source.width &&
      mapping.destination.height == source.height) {
    destination = {static_cast<uint16_t>(mapping.destination.x + sourceX),
                   static_cast<uint16_t>(mapping.destination.y + sourceY), 1, 1};
    return true;
  }
  const uint32_t relativeX = sourceX - source.x;
  const uint32_t relativeY = sourceY - source.y;
  const uint32_t left = mapping.destination.x + relativeX * mapping.destination.width / source.width;
  const uint32_t right = mapping.destination.x + (relativeX + 1) * mapping.destination.width / source.width;
  const uint32_t top = mapping.destination.y + relativeY * mapping.destination.height / source.height;
  const uint32_t bottom = mapping.destination.y + (relativeY + 1) * mapping.destination.height / source.height;
  if (right <= left || bottom <= top || right > UINT16_MAX || bottom > UINT16_MAX) return false;
  destination = {static_cast<uint16_t>(left), static_cast<uint16_t>(top), static_cast<uint16_t>(right - left),
                 static_cast<uint16_t>(bottom - top)};
  return true;
}

void fillMappedCell(GfxRenderer& renderer, const xtc_frame::Rect& destination, const bool state) {
  // Most source pixels remain 1:1 even when a narrow crop or small fit margin
  // makes the overall mapping non-identity. Keep those writes on drawPixel's
  // hot path instead of paying fillRect's clipping/setup cost per source pixel.
  if (destination.width == 1 && destination.height == 1) {
    renderer.drawPixel(destination.x, destination.y, state);
  } else {
    renderer.fillRect(destination.x, destination.y, destination.width, destination.height, state);
  }
}

bool streamXtchRenderPass(const Xtc& xtc, const uint32_t pageIndex, const uint16_t pageWidth,
                          const uint16_t pageHeight, const xtc_frame::Mapping& mapping, GfxRenderer& renderer,
                          const XtchRenderPass pass, const bool darkMode) {
  const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
  const size_t colBytes = (pageHeight + 7) / 8;
  const xtc::XtcError error = xtc.loadPageStreaming(
      pageIndex,
      [&](const uint8_t* data, const size_t size, const size_t offset) {
        for (size_t i = 0; i < size; ++i) {
          const size_t absoluteOffset = offset + i;
          const bool secondPlane = absoluteOffset >= planeSize;
          const size_t planeOffset = secondPlane ? absoluteOffset - planeSize : absoluteOffset;
          const size_t column = planeOffset / colBytes;
          if (column >= pageWidth) continue;

          const uint16_t x = static_cast<uint16_t>(pageWidth - 1 - column);
          const uint16_t yBase = static_cast<uint16_t>((planeOffset % colBytes) * 8);
          for (uint8_t bit = 0; bit < 8 && yBase + bit < pageHeight; ++bit) {
            const uint16_t y = static_cast<uint16_t>(yBase + bit);
            const bool rawBitSet = ((data[i] >> (7 - bit)) & 1U) != 0;
            const bool bitSet = darkMode ? !rawBitSet : rawBitSet;
            xtc_frame::Rect destination;
            if (!mappedPixelRect(mapping, x, y, destination)) continue;
            switch (pass) {
              case XtchRenderPass::Base:
                if (darkMode) {
                  if (!secondPlane && !rawBitSet) {
                    fillMappedCell(renderer, destination, true);
                  } else if (secondPlane && rawBitSet) {
                    fillMappedCell(renderer, destination, false);
                  }
                } else if (rawBitSet) {
                  fillMappedCell(renderer, destination, true);
                }
                break;
              case XtchRenderPass::Lsb:
                if (!secondPlane) {
                  if (!bitSet) fillMappedCell(renderer, destination, false);
                } else if (!bitSet) {
                  fillMappedCell(renderer, destination, true);
                }
                break;
              case XtchRenderPass::Msb:
                if (!secondPlane && bitSet) {
                  fillMappedCell(renderer, destination, false);
                } else if (secondPlane && bitSet) {
                  for (uint16_t dy = destination.y; dy < destination.y + destination.height; ++dy) {
                    for (uint16_t dx = destination.x; dx < destination.x + destination.width; ++dx) {
                      renderer.drawPixel(dx, dy, !renderer.isPixelBlack(dx, dy));
                    }
                  }
                }
                break;
            }
          }
        }
      },
      8192);
  if (error == xtc::XtcError::OK) return true;
  LOG_ERR("XTR", "Failed to stream XTCH page %lu: %s", pageIndex, xtc::errorToString(error));
  return false;
}
}  // namespace

void XtcReaderActivity::requestExitToHome() {
  if (exitHomePending) return;
  exitHomePending = true;
  exitPopupShown = false;
  requestUpdate(true);
}

void XtcReaderActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderMappingMode(true);
  readingSessionStartedMs = millis();
  exitHomePending = false;
  exitPopupShown = false;

  if (!xtc) {
    return;
  }

  xtc->setupCacheDir();

  // Load saved progress
  loadProgress();
  if (initialBookmarkPage && xtc->getPageCount() > 0) {
    setCurrentPage(*initialBookmarkPage < xtc->getPageCount() ? *initialBookmarkPage : xtc->getPageCount() - 1);
  }
  initialBookmarkPage.reset();

  // Save current XTC as last opened book and add to recent books
  APP_STATE.openEpubPath = xtc->getPath();
  APP_STATE.saveToFile();
  // Reuse a checked Recent metadata result before touching the optional XTC
  // trailer. Otherwise resolve it once from the already-open parser and let
  // Recent persist the result for Home/Spine and the next reader visit.
  std::string xtcSynopsis;
  std::string xtcSeries;
  bool xtcMetadataChecked = false;
  {
    const auto recentBooks = RECENT_BOOKS.getBooks();
    const auto cached = std::find_if(recentBooks.begin(), recentBooks.end(), [this](const RecentBook& book) {
      return book.path == xtc->getPath();
    });
    if (cached != recentBooks.end() && cached->xtcMetadataChecked) {
      xtcSynopsis = cached->synopsis;
      xtcSeries = cached->series;
      xtcMetadataChecked = true;
    }
  }
  if (!xtcMetadataChecked) {
    xtcSynopsis = xtc->getDescription();
    xtcSeries = xtc->getSeries();
    xtcMetadataChecked = xtc->hasLoadedExtendedMetadata();
  }
  RECENT_BOOKS.addBook(xtc->getPath(), xtc->getTitle(), xtc->getAuthor(), xtc->getThumbBmpPath(), xtcSynopsis,
                       xtcSeries, xtcMetadataChecked);

  // Trigger first update
  requestUpdate();
}

void XtcReaderActivity::onExit() {
  Activity::onExit();
  mappedInput.setReaderMappingMode(false);
  renderer.setDarkMode(false);

  if (xtc) {
    const ScreenshotInfo info = getScreenshotInfo();
    const uint32_t elapsedSeconds = (millis() - readingSessionStartedMs) / 1000UL;
    if (!progressLoadUncertain || progressPositionChangedSinceLoad) {
      RECENT_BOOKS.recordReading(xtc->getPath(), static_cast<uint8_t>(info.progressPercent), elapsedSeconds,
                                 sessionPagesTurned);
      BOOK_STATES.recordReading(xtc->getPath(), static_cast<uint8_t>(info.progressPercent), elapsedSeconds,
                                sessionPagesTurned);
    } else {
      LOG_ERR("XTR", "Skipping progress-derived Recent/BookState update after uncertain progress load");
    }
    READING_STATS.recordSession(halClock.getDateKey(), elapsedSeconds, sessionPagesTurned);
  }

  ReaderUtils::clearGhostingOnExit(renderer);

  if (APP_STATE.readerActivityLoadCount != 0) {
    APP_STATE.readerActivityLoadCount = 0;
    APP_STATE.saveToFile();
  }
  // Flush only if the visible position changed since the last progress write.
  saveProgress();
  xtc.reset();
}

void XtcReaderActivity::openReaderMenu() {
  if (!xtc) return;
  std::vector<BookmarkEntry> bookmarks;
  BookmarkFile::load(xtc->getPath(), bookmarks);
  const uint32_t pageCount = xtc->getPageCount();
  const int menuPageCount = static_cast<int>(std::min<uint32_t>(pageCount, static_cast<uint32_t>(INT_MAX)));
  const int menuCurrentPage = pageCount == 0
                                  ? 0
                                  : static_cast<int>(std::min<uint64_t>(
                                        static_cast<uint64_t>(currentPage) + 1, menuPageCount));
  const uint8_t currentProgress = pageCount > 1
                                      ? static_cast<uint8_t>(std::min<uint64_t>(100, static_cast<uint64_t>(currentPage) * 100 / (pageCount - 1)))
                                      : 0;
  const auto quickActionContext = QuickActions::Context{false, false, KOREADER_STORE.hasCredentials(), true, false,
                                                        FREEINK_CAP_BLE_HID_HOST != 0};
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, xtc->getTitle(),
                                               menuCurrentPage, menuPageCount, currentProgress, SETTINGS.orientation,
                                               false, !bookmarks.empty(), false, quickActionContext, true,
                                               xtc->hasChapters() && !xtc->getChapters().empty(),
                                               SETTINGS.xtcReadingDirection, SETTINGS.xtcPageFitMode, autoTurnOption,
                                               KOREADER_STORE.hasCredentials()),
      [this](const ActivityResult& result) {
        if (!xtc || !std::holds_alternative<MenuResult>(result.data)) return;
        const auto& menu = std::get<MenuResult>(result.data);
        const uint8_t orientation = menu.orientation % CrossPointSettings::ORIENTATION_COUNT;
        const uint8_t readingDirection = menu.xtcReadingDirection <= 1 ? menu.xtcReadingDirection : 0;
        const uint8_t pageFitMode = menu.xtcPageFitMode <= 1 ? menu.xtcPageFitMode : 0;
        const bool displaySettingsChanged = SETTINGS.orientation != orientation ||
                                            SETTINGS.xtcReadingDirection != readingDirection ||
                                            SETTINGS.xtcPageFitMode != pageFitMode;
        SETTINGS.orientation = orientation;
        SETTINGS.xtcReadingDirection = readingDirection;
        SETTINGS.xtcPageFitMode = pageFitMode;
        if (displaySettingsChanged) SETTINGS.saveToFile();
        if (displaySettingsChanged) pagesUntilFullRefresh = 1;
        ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
        updateAutoPageTurn(menu.pageTurnOption);
        if (!result.isCancelled) {
          if (menu.action == static_cast<int>(EpubReaderMenuActivity::MenuAction::QUICK_ACTIONS)) {
            executeQuickAction(menu.quickAction);
          } else {
            onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action), menu.quickAction);
          }
        }
        requestUpdate();
      });
}

void XtcReaderActivity::updateAutoPageTurn(const uint8_t pageTurnOption) {
  autoTurnOption = std::min<uint8_t>(pageTurnOption, 4);
  static constexpr uint8_t pagesPerMinute[] = {0, 1, 3, 6, 12};
  const uint8_t rate = pagesPerMinute[autoTurnOption];
  nextAutoTurnMs = rate == 0 ? 0 : millis() + (60000UL / rate);
}

void XtcReaderActivity::toggleCurrentBookmark() {
  if (!xtc || xtc->getPageCount() == 0) return;
  const uint32_t page = std::min(currentPage, xtc->getPageCount() - 1);
  std::vector<BookmarkEntry> bookmarks;
  BookmarkFile::load(xtc->getPath(), bookmarks);
  const std::string position = "xtc-page:" + std::to_string(page);
  const auto found = std::find_if(bookmarks.begin(), bookmarks.end(), [&position](const BookmarkEntry& bookmark) {
    return bookmark.xpath == position;
  });
  if (found != bookmarks.end()) {
    bookmarks.erase(found);
  } else {
    BookmarkEntry bookmark;
    bookmark.xpath = position;
    bookmark.summary = std::string(tr(STR_PAGE)) + " " + std::to_string(page + 1);
    bookmark.percentage = xtc->getPageCount() > 1
                              ? static_cast<float>(page) / static_cast<float>(xtc->getPageCount() - 1)
                              : 0.0f;
    bookmarks.push_back(std::move(bookmark));
  }
  if (!BookmarkFile::save(xtc->getPath(), bookmarks)) {
    LOG_ERR("XTR", "Failed to save XTC bookmark for page %lu", page + 1);
  }
  requestUpdate();
}

void XtcReaderActivity::onReaderMenuConfirm(const EpubReaderMenuActivity::MenuAction action, const int quickAction) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER:
      if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
        startActivityForResult(
            std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) setCurrentPage(std::get<PageResult>(result.data).page);
              requestUpdate();
            });
      }
      break;
    case EpubReaderMenuActivity::MenuAction::READING_DIRECTION:
    case EpubReaderMenuActivity::MenuAction::PAGE_FIT:
      break;  // Values were committed when the menu returned.
    case EpubReaderMenuActivity::MenuAction::GO_TO_PAGE:
      if (xtc && xtc->getPageCount() > 0) {
        const int maximum = static_cast<int>(std::min<uint32_t>(xtc->getPageCount() - 1, INT_MAX));
        startActivityForResult(
            std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput,
                                                                 static_cast<int>(std::min<uint32_t>(currentPage, maximum)),
                                                                 maximum, true),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) setCurrentPage(std::get<PageResult>(result.data).page);
              requestUpdate();
            });
      }
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT:
      if (xtc && xtc->getPageCount() > 0) {
        const int initialPercent = xtc->getPageCount() > 1
                                      ? static_cast<int>(std::min<uint64_t>(100, static_cast<uint64_t>(currentPage) * 100 / (xtc->getPageCount() - 1)))
                                      : 0;
        startActivityForResult(
            std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
            [this](const ActivityResult& result) {
              if (!result.isCancelled && xtc && xtc->getPageCount() > 0) {
                const int percent = std::clamp(std::get<PercentResult>(result.data).percent, 0, 100);
                setCurrentPage(static_cast<uint32_t>(
                    (static_cast<uint64_t>(xtc->getPageCount() - 1) * percent + 50) / 100));
              }
              requestUpdate();
            });
      }
      break;
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS:
      if (xtc) {
        startActivityForResult(
            std::make_unique<XtcReaderBookmarksActivity>(renderer, mappedInput, xtc->getPath()),
            [this](const ActivityResult& result) {
              if (!result.isCancelled && xtc && xtc->getPageCount() > 0) {
                setCurrentPage(std::min(std::get<PageResult>(result.data).page, xtc->getPageCount() - 1));
              }
              requestUpdate();
            });
      }
      break;
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK:
      toggleCurrentBookmark();
      break;
    case EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
      break;  // The selected option is committed when the menu returns.
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      RenderLock lock(*this);
      ScreenshotUtil::takeScreenshot(renderer);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME:
      requestExitToHome();
      break;
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE:
      if (xtc && !xtc->clearCoverCache()) LOG_ERR("XTR", "Failed to clear XTC cover cache");
      break;
    case EpubReaderMenuActivity::MenuAction::SYNC:
      launchKOReaderSync();
      break;
    case EpubReaderMenuActivity::MenuAction::QUICK_ACTIONS:
      executeQuickAction(quickAction);
      break;
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH:
      bleinput::toggleBluetooth();
      break;
    case EpubReaderMenuActivity::MenuAction::READER_OPTIONS:
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES:
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR:
    case EpubReaderMenuActivity::MenuAction::DICTIONARY:
    case EpubReaderMenuActivity::MenuAction::CLIP_TEXT:
    case EpubReaderMenuActivity::MenuAction::CLIPPINGS:
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
      break;
  }
}

void XtcReaderActivity::executeQuickAction(const int actionValue) {
  const auto action = static_cast<QuickActions::ActionId>(actionValue);
  const QuickActions::Context context{false, false, KOREADER_STORE.hasCredentials(), true, false,
                                      FREEINK_CAP_BLE_HID_HOST != 0};
  if (!QuickActions::isAvailable(action, context)) return;

  switch (action) {
    case QuickActions::ActionId::RefreshScreen:
      pagesUntilFullRefresh = 1;
      requestUpdate();
      break;
    case QuickActions::ActionId::ToggleDarkMode:
      SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
      SETTINGS.saveToFile();
      pagesUntilFullRefresh = 1;
      requestUpdate();
      break;
    case QuickActions::ActionId::ToggleBookmark:
      toggleCurrentBookmark();
      break;
    case QuickActions::ActionId::ReaderOptions:
      startActivityForResult(
          std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                 TextSettingsActivity::Tab::Size),
          [this](const ActivityResult&) {
            SETTINGS.saveToFile();
            skipNextButtonCheck = true;
            requestUpdate();
          });
      break;
    case QuickActions::ActionId::Orientation:
      SETTINGS.orientation = static_cast<uint8_t>((SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT);
      SETTINGS.saveToFile();
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      requestUpdate();
      break;
    case QuickActions::ActionId::Screenshot: {
      RenderLock lock(*this);
      ScreenshotUtil::takeScreenshot(renderer);
      break;
    }
    case QuickActions::ActionId::Home:
      requestExitToHome();
      break;
    case QuickActions::ActionId::Sleep:
      activityManager.requestSleep();
      break;
    case QuickActions::ActionId::ToggleBluetooth:
      bleinput::toggleBluetooth();
      break;
    case QuickActions::ActionId::Lookup:
      break;
    case QuickActions::ActionId::ReadingStats:
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, xtc->getPath()),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    case QuickActions::ActionId::Sync:
      launchKOReaderSync();
      break;
    case QuickActions::ActionId::None:
      break;
  }
}

void XtcReaderActivity::launchKOReaderSync() {
  if (!xtc || !KOREADER_STORE.hasCredentials()) return;
  if (!saveProgress()) {
    LOG_ERR("KOSync", "Aborting XTC sync because current progress could not be saved");
    requestUpdate();
    return;
  }

  const std::string documentPath = xtc->getPath();
  const std::string cachePath = xtc->getCachePath();
  const uint32_t pageCount = xtc->getPageCount();
  const uint32_t page = pageCount > 0 ? std::min(currentPage, pageCount - 1) : 0;
  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(renderer, mappedInput, documentPath,
                                                                         cachePath, page, pageCount));
}

void XtcReaderActivity::loop() {
  // Match EPUB's exit sequence: let the render task show the saving message before
  // the activity transition runs onExit() and persists the reading session.
  if (exitHomePending) {
    if (exitPopupShown) activityManager.goHome();
    return;
  }

  if (skipNextButtonCheck) {
    skipNextButtonCheck = false;
    return;
  }

  if (!xtc) {
    return;
  }

  static constexpr uint8_t pagesPerMinute[] = {0, 1, 3, 6, 12};
  const uint8_t turnRate = pagesPerMinute[std::min<uint8_t>(autoTurnOption, 4)];
  if (turnRate > 0 && currentPage < xtc->getPageCount() && nextAutoTurnMs != 0 &&
      static_cast<int32_t>(millis() - nextAutoTurnMs) >= 0) {
    setCurrentPage(currentPage + 1);
    if (sessionPagesTurned < UINT32_MAX) ++sessionPagesTurned;
    nextAutoTurnMs = millis() + (60000UL / turnRate);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) && longPowerShortcutFired) {
    longPowerShortcutFired = false;
    return;
  }
  if (SETTINGS.longPwrBtn != CrossPointSettings::LP_PWR_SLEEP &&
      SETTINGS.longPwrBtn != CrossPointSettings::LP_PWR_IGNORE && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() >= ReaderUtils::GO_HOME_MS && !longPowerShortcutFired) {
    longPowerShortcutFired = true;
    if (SETTINGS.longPwrBtn == CrossPointSettings::LP_PWR_READER_OPTIONS) {
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Size),
                             [this](const ActivityResult&) {
                               SETTINGS.saveToFile();
                               darkShortcutFired = false;
                               longPowerShortcutFired = false;
                               skipNextButtonCheck = true;
                               requestUpdate();
                             });
    } else if (SETTINGS.longPwrBtn == CrossPointSettings::LP_PWR_READING_STATS) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, xtc->getPath()),
                             [this](const ActivityResult&) { requestUpdate(); });
    } else if (SETTINGS.longPwrBtn == CrossPointSettings::LP_PWR_SCREENSHOT) {
      RenderLock lock(*this);
      ScreenshotUtil::takeScreenshot(renderer);
    } else if (SETTINGS.longPwrBtn == CrossPointSettings::LP_PWR_DARK_MODE) {
      SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
      SETTINGS.saveToFile();
      pagesUntilFullRefresh = 1;
      darkShortcutFired = true;
      requestUpdate();
    } else if (SETTINGS.longPwrBtn == CrossPointSettings::LP_PWR_TOGGLE_BLUETOOTH) {
      // The shared main-loop handler performs this action exactly once.
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && darkShortcutFired) {
    darkShortcutFired = false;
    return;
  }
  if (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_READER_OPTIONS &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !darkShortcutFired) {
    darkShortcutFired = true;
    startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                  TextSettingsActivity::Tab::Size),
                           [this](const ActivityResult&) {
                             SETTINGS.saveToFile();
                             darkShortcutFired = false;
                             longPowerShortcutFired = false;
                             skipNextButtonCheck = true;
                             requestUpdate();
                           });
    return;
  }
  if (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_DARK_MODE &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !darkShortcutFired) {
    SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
    SETTINGS.saveToFile();
    pagesUntilFullRefresh = 1;
    darkShortcutFired = true;
    requestUpdate();
    return;
  }
  if ((SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_SLEEP ||
       SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_READING_STATS ||
       SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_SCREENSHOT ||
       SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_TOGGLE_BLUETOOTH) &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !darkShortcutFired) {
    darkShortcutFired = true;
    if (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_SLEEP) {
      activityManager.requestSleep();
    } else if (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_READING_STATS) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, xtc->getPath()),
                             [this](const ActivityResult&) {
                               darkShortcutFired = false;
                               skipNextButtonCheck = true;
                               requestUpdate();
                             });
    } else if (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_SCREENSHOT) {
      RenderLock lock(*this);
      ScreenshotUtil::takeScreenshot(renderer);
    } else {
      bleinput::toggleBluetooth();
    }
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  const bool atEndOfBook = currentPage >= xtc->getPageCount();

  // While the end screen suggestion menu is showing it owns Confirm/Back/navigation
  // input. Anything it doesn't handle (e.g. long-press Back to the file browser) falls
  // through to the regular handlers below; page turns are absorbed by the end-of-book
  // block.
  if (atEndOfBook && endOfBookOptions.menuActive()) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        requestExitToHome();
        return;
      case EndOfBookOptions::Action::LastPage:
        setCurrentPage(xtc->getPageCount() > 0 ? xtc->getPageCount() - 1 : 0);
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  // Enter chapter selection activity
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || ReaderUtils::isTouchMenuGesture(mappedInput)) {
    openReaderMenu();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, xtc ? xtc->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<XtcReaderActivity*>(ctx)->requestExitToHome(); }})) {
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt, fromSide] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (SETTINGS.xtcReadingDirection == 1) std::swap(prevTriggered, nextTriggered);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // At end of the book with no suggestion menu, forward button goes home and back
  // button returns to last page
  if (currentPage >= xtc->getPageCount()) {
    if (endOfBookOptions.menuActive()) {
      // Selection movement was handled above; absorb leftover page-turn triggers so
      // e.g. "previous" at the top of the list doesn't jump back into the book
      return;
    }
    if (nextTriggered) {
      requestExitToHome();
    } else {
      setCurrentPage(xtc->getPageCount() - 1);
      requestUpdate();
    }
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  if (!fromTilt && fromSide && heldMs > ReaderUtils::SKIP_HOLD_MS &&
      SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_TOGGLE_BLUETOOTH) {
    bleinput::toggleBluetooth();
    return;
  }
  const auto longPressBehavior = fromSide
                                     ? (SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP
                                            ? CrossPointSettings::CHAPTER_SKIP
                                        : SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_ORIENTATION
                                            ? CrossPointSettings::ORIENTATION_CHANGE
                                            : CrossPointSettings::OFF)
                                     : SETTINGS.longPressButtonBehavior;
  const bool skipPages =
      !fromTilt && longPressBehavior == SETTINGS.CHAPTER_SKIP && heldMs > ReaderUtils::SKIP_HOLD_MS;
  if (!fromTilt && longPressBehavior == CrossPointSettings::ORIENTATION_CHANGE &&
      heldMs > ReaderUtils::SKIP_HOLD_MS) {
    SETTINGS.orientation = nextTriggered
                               ? static_cast<uint8_t>((SETTINGS.orientation - 1 + CrossPointSettings::ORIENTATION_COUNT) %
                                                      CrossPointSettings::ORIENTATION_COUNT)
                               : static_cast<uint8_t>((SETTINGS.orientation + 1) % CrossPointSettings::ORIENTATION_COUNT);
    SETTINGS.saveToFile();
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    requestUpdate();
    return;
  }
  if (!fromTilt && !fromSide && longPressBehavior == CrossPointSettings::CHANGE_FONT_SIZE &&
      heldMs > ReaderUtils::SKIP_HOLD_MS) {
    SETTINGS.fontSize = static_cast<uint8_t>((SETTINGS.fontSize + 1) % CrossPointSettings::FONT_SIZE_COUNT);
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }
  const int skipAmount = skipPages ? 10 : 1;
  const uint32_t oldPage = currentPage;

  if (prevTriggered) {
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      setCurrentPage(currentPage - static_cast<uint32_t>(skipAmount));
    } else {
      setCurrentPage(0);
    }
    requestUpdate();
  } else if (nextTriggered) {
    const uint64_t nextPage = static_cast<uint64_t>(currentPage) + static_cast<uint32_t>(skipAmount);
    setCurrentPage(static_cast<uint32_t>(std::min<uint64_t>(nextPage, xtc->getPageCount())));
    requestUpdate();
  }
  if (currentPage != oldPage && sessionPagesTurned < UINT32_MAX) ++sessionPagesTurned;
}

void XtcReaderActivity::render(RenderLock&&) {
  if (!xtc) {
    return;
  }

  renderer.setDarkMode(SETTINGS.readerDarkMode != 0);
  renderer.setRenderMode(GfxRenderer::BW);

  // Keep the current page visible while onExit() persists Recent, per-book,
  // and daily reading data, matching the EPUB reader's exit feedback.
  if (exitHomePending) {
    if (!exitPopupShown) {
      GUI.drawPopup(renderer, tr(STR_SAVING_READING_DATA), true);
      exitPopupShown = true;
    }
    return;
  }

  // Bounds check
  if (currentPage >= xtc->getPageCount()) {
    // Show end of book screen. Sole load site: runs on the render task (serialized by
    // RenderLock); the main task only reads the suggestions once the flag is published.
    endOfBookOptions.loadOnce(xtc->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    return;
  }

  renderPage();
  saveProgress();
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo() const {
  const auto sb = SETTINGS.statusBarSpec();
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(currentPage) + 1;
  std::string title = sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE ? xtc->getTitle() : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return currentPage >= chapter.startPage && currentPage <= chapter.endPage;
  });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(currentPage - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, std::move(title)};
}

void XtcReaderActivity::renderStatusBarOverlay(const StatusBarOverlayPosition position) const {
  const auto sb = SETTINGS.statusBarSpec();
  const bool drawBottom = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_BOTTOM &&
                          position == StatusBarOverlayPosition::Bottom;
  const bool drawTop = sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP &&
                       position == StatusBarOverlayPosition::Top;
  if (!drawBottom && !drawTop) {
    return;
  }

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  int clearY;
  int paddingBottom = 0;
  if (position == StatusBarOverlayPosition::Bottom) {
    clearY = renderer.getScreenHeight() - orientedMarginBottom - statusBarHeight - 4;
    if (clearY < 0) {
      clearY = 0;
    }
  } else {
    clearY = orientedMarginTop;
    paddingBottom = renderer.getScreenHeight() - statusBarHeight - orientedMarginBottom - orientedMarginTop - 4;
  }
  const int clearHeight = position == StatusBarOverlayPosition::Bottom
                              ? renderer.getScreenHeight() - orientedMarginBottom - clearY
                              : statusBarHeight + 4;
  if (clearHeight > 0) {
    renderer.fillRect(0, clearY, renderer.getScreenWidth(), clearHeight, false);
  }

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(currentPage) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto pageInfo = getStatusBarInfo();
  GUI.drawStatusBar(renderer, progress, pageInfo.currentPage, pageInfo.pageCount, pageInfo.title, paddingBottom);
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();
  const bool darkMode = renderer.isDarkMode();

  xtc_frame::Mapping mapping;
  const auto fitMode = SETTINGS.xtcPageFitMode == CrossPointSettings::XTC_CROP_FILL
                           ? xtc_frame::Mode::CropFill
                           : xtc_frame::Mode::FitContain;
  if (!xtc_frame::calculateMapping(pageWidth, pageHeight, 0, 0,
                                   static_cast<uint16_t>(renderer.getScreenWidth()),
                                   static_cast<uint16_t>(renderer.getScreenHeight()), fitMode, mapping)) {
    LOG_ERR("XTR", "Invalid page mapping for %ux%u source", pageWidth, pageHeight);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  auto showStreamError = [&]() {
    renderer.clearScreen();
    const char* message = xtc->getLastError() == xtc::XtcError::MEMORY_ERROR
                              ? tr(STR_MEMORY_ERROR)
                              : tr(STR_PAGE_LOAD_ERROR);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, message, true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
  };

  if (bitDepth == 2) {
    // XTCH stays out of page-sized heap buffers. The parser supplies bounded chunks
    // while each grayscale plane is reconstructed directly into renderer storage.
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    if (!streamXtchRenderPass(*xtc, currentPage, pageWidth, pageHeight, mapping, renderer,
                              XtchRenderPass::Base, darkMode)) {
      showStreamError();
      return;
    }

    if (pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
      pagesUntilFullRefresh = ReaderUtils::refreshCadence(renderer);
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    // Preserve the B/W frame in non-contiguous chunks when heap shape permits.
    // Low-heap devices can reconstruct that base with one additional SD pass.
    const size_t freeBeforeSnapshot = static_cast<size_t>(ESP.getFreeHeap());
    const size_t largestBeforeSnapshot = static_cast<size_t>(ESP.getMaxAllocHeap());
    const size_t bwBufferSize = renderer.getBufferSize();
    const bool hasBwSnapshotHeadroom = xtc_memory::canPreserveBwFrame(
        freeBeforeSnapshot, largestBeforeSnapshot, bwBufferSize);
    const bool storedBw = hasBwSnapshotHeadroom && renderer.storeBwBuffer();
#if defined(FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS) && FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS
    const size_t freeAfterSnapshot = static_cast<size_t>(ESP.getFreeHeap());
    const size_t largestAfterSnapshot = static_cast<size_t>(ESP.getMaxAllocHeap());
    LOG_INF("XTR", "XTCH bw_snapshot phase=store page=%lu owner=xtch_bw_snapshot bytes=%u result=%s free=%u->%u "
                    "largest=%u->%u",
            static_cast<unsigned long>(currentPage + 1), static_cast<unsigned>(bwBufferSize),
            storedBw ? "stored" : hasBwSnapshotHeadroom ? "alloc_failed" : "headroom_skip",
            static_cast<unsigned>(freeBeforeSnapshot), static_cast<unsigned>(freeAfterSnapshot),
            static_cast<unsigned>(largestBeforeSnapshot), static_cast<unsigned>(largestAfterSnapshot));
#endif
    const auto restoreBwSnapshot = [&](const char* stage) {
      if (!storedBw) return;
#if defined(FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS) && FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS
      const size_t freeBeforeRestore = static_cast<size_t>(ESP.getFreeHeap());
      const size_t largestBeforeRestore = static_cast<size_t>(ESP.getMaxAllocHeap());
#endif
      renderer.restoreBwBuffer();
#if defined(FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS) && FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS
      LOG_INF("XTR", "XTCH bw_snapshot phase=release page=%lu owner=xtch_bw_snapshot stage=%s bytes=%u "
                      "free=%u->%u largest=%u->%u",
              static_cast<unsigned long>(currentPage + 1), stage ? stage : "?", static_cast<unsigned>(bwBufferSize),
              static_cast<unsigned>(freeBeforeRestore), static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(largestBeforeRestore), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#else
      (void)stage;
#endif
    };
    if (!storedBw) {
      LOG_DBG("XTR", "XTCH bw_snapshot=unavailable reason=%s free=%u largest=%u bytes=%u fallback=stream",
              hasBwSnapshotHeadroom ? "allocation_failed" : "heap_floor", static_cast<unsigned>(freeBeforeSnapshot),
              static_cast<unsigned>(largestBeforeSnapshot), static_cast<unsigned>(bwBufferSize));
    }

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (!streamXtchRenderPass(*xtc, currentPage, pageWidth, pageHeight, mapping, renderer,
                              XtchRenderPass::Lsb, darkMode)) {
      restoreBwSnapshot("lsb_stream_error");
      showStreamError();
      return;
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    if (!streamXtchRenderPass(*xtc, currentPage, pageWidth, pageHeight, mapping, renderer,
                              XtchRenderPass::Msb, darkMode)) {
      restoreBwSnapshot("msb_stream_error");
      showStreamError();
      return;
    }
    renderer.copyGrayscaleMsbBuffers();
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    if (storedBw) {
      restoreBwSnapshot("render_complete");
    } else {
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.clearScreen();
      const bool restoredFromStream = streamXtchRenderPass(*xtc, currentPage, pageWidth, pageHeight, mapping, renderer,
                                                           XtchRenderPass::Base, darkMode);
      if (restoredFromStream) {
        renderer.cleanupGrayscaleWithFrameBuffer();
      } else {
        LOG_ERR("XTR", "XTCH bw_snapshot_fallback=failed page=%lu error=%s",
                currentPage + 1, xtc::errorToString(xtc->getLastError()));
      }
    }
    return;
  }

  // XTG is row-major and uses the same bounded streaming path. One byte supplies
  // up to eight horizontal pixels, so no full-page allocation is needed on X3.
  renderer.clearScreen();
  const size_t sourceRowBytes = (static_cast<size_t>(pageWidth) + 7) / 8;
  const bool identityMapping = mapping.source.x == 0 && mapping.source.y == 0 &&
                               mapping.source.width == pageWidth && mapping.source.height == pageHeight &&
                               mapping.destination.width == pageWidth && mapping.destination.height == pageHeight;
  const auto streamError = xtc->loadPageStreaming(
      currentPage,
      [&](const uint8_t* data, const size_t size, const size_t offset) {
        for (size_t i = 0; i < size; ++i) {
          const size_t absoluteOffset = offset + i;
          const size_t sourceY = absoluteOffset / sourceRowBytes;
          if (sourceY >= pageHeight) continue;
          const size_t byteInRow = absoluteOffset % sourceRowBytes;
          const size_t sourceXBase = byteInRow * 8;
          for (uint8_t bit = 0; bit < 8 && sourceXBase + bit < pageWidth; ++bit) {
            const bool sourceBlack = ((data[i] >> (7 - bit)) & 1U) == 0;
            const bool shouldPaint = darkMode ? !sourceBlack : sourceBlack;
            if (!shouldPaint) continue;
            xtc_frame::Rect destination;
            if (!mappedPixelRect(mapping, static_cast<uint16_t>(sourceXBase + bit),
                                 static_cast<uint16_t>(sourceY), destination)) {
              continue;
            }
            // Keep the common 1:1 path as cheap as the old renderer. Scaled
            // cells use explicit fill colors, while drawPixel preserves the
            // renderer's established dark-mode polarity.
            if (identityMapping) {
              renderer.drawPixel(destination.x, destination.y, true);
            } else {
              fillMappedCell(renderer, destination, true);
            }
          }
        }
      },
      8192);
  if (streamError != xtc::XtcError::OK) {
    LOG_ERR("XTR", "Failed to stream page %lu: %s", currentPage, xtc::errorToString(streamError));
    showStreamError();
    return;
  }

  if (SETTINGS.statusBarSpec().xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP) {
    renderStatusBarOverlay(StatusBarOverlayPosition::Top);
  } else {
    renderStatusBarOverlay(StatusBarOverlayPosition::Bottom);
  }
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);
}
bool XtcReaderActivity::saveProgress() {
  if (!xtc) return false;
  if (progressLoadUncertain && !progressPositionChangedSinceLoad) {
    LOG_ERR("XTR", "Skipping progress save because saved progress could not be read safely");
    return false;
  }
  const uint32_t pageCount = xtc->getPageCount();
  const uint32_t pageToPersist = pageCount > 0 && currentPage >= pageCount ? pageCount - 1 : currentPage;
  if (hasPersistedProgressPage && lastPersistedPage == pageToPersist) return true;
  uint8_t data[4];
  data[0] = pageToPersist & 0xFF;
  data[1] = (pageToPersist >> 8) & 0xFF;
  data[2] = (pageToPersist >> 16) & 0xFF;
  data[3] = (pageToPersist >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTR", "Failed to save progress: page %lu", pageToPersist);
    return false;
  }
  lastPersistedPage = pageToPersist;
  hasPersistedProgressPage = true;
  progressLoadUncertain = false;
  progressPositionChangedSinceLoad = false;
  return true;
}

void XtcReaderActivity::loadProgress() {
  hasPersistedProgressPage = false;
  progressLoadUncertain = false;
  progressPositionChangedSinceLoad = false;
  uint8_t data[4]{};
  const ProgressFile::ExactReadResult result = ProgressFile::readExact(xtc->getCachePath(), data, sizeof(data));
  if (result == ProgressFile::ExactReadResult::Success || result == ProgressFile::ExactReadResult::Recovered) {
    const uint32_t savedPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                               (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
    if (savedPage < xtc->getPageCount()) {
      currentPage = savedPage;
      if (result == ProgressFile::ExactReadResult::Success) {
        lastPersistedPage = currentPage;
        hasPersistedProgressPage = true;
        LOG_DBG("XTR", "Loaded progress: page %lu", currentPage);
      } else {
        progressLoadUncertain = true;
        LOG_ERR("XTR", "Loaded recovery progress page %lu; source is not authoritative, preserving candidates",
                currentPage);
      }
      return;
    }
    progressLoadUncertain = true;
    LOG_ERR("XTR", "Saved progress page %lu is outside the book; preserving the saved file", savedPage);
    return;
  }

  if (result == ProgressFile::ExactReadResult::Missing) {
    currentPage = 0;
    LOG_DBG("XTR", "No saved progress; starting at page 0");
    return;
  }

  progressLoadUncertain = true;
  currentPage = 0;
  LOG_ERR("XTR", "Progress load uncertain result=%u; will not save page 0 unless position changes",
          static_cast<unsigned>(result));
}

void XtcReaderActivity::setCurrentPage(const uint32_t page) {
  if (currentPage == page) return;
  currentPage = page;
  progressPositionChangedSinceLoad = true;
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    // Clamp to last valid page to avoid sentinel value (currentPage == pageCount)
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
