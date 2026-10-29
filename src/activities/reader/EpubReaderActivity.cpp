#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SdCardFont.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>

#include "../../util/BookmarkFile.h"
#include "BookmarkEntry.h"
#include "ClippingEntry.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "ClipSelectionActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "BleInput.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingListActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "BookStateStore.h"
#include "ReadingStatsStore.h"
#include "SdCardFontSystem.h"
#include "activities/home/ReadingStatsActivity.h"
#if FREEINK_CAP_BLE_HID_HOST
#include "activities/settings/BluetoothSettingsActivity.h"
#endif
#include "activities/settings/TextSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkUtil.h"
#include "util/BleMemoryPolicy.h"
#include "util/ClipFile.h"
#include "util/EpubDiagnostics.h"
#include "util/ScreenshotUtil.h"

// A page-local, checked array is shared by BW and every grayscale pass. The
// old vector grew 128 -> 256 -> 512 words with the old and new arenas both
// resident during growth; that throwing allocation is the symbolicated panic.
struct ReaderHighlightWord {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  uint16_t row = 0;
  const char* text = nullptr;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  uint8_t focusBoundary = 0;
  uint16_t focusSuffixX = 0;
};

namespace {
using HighlightWord = ReaderHighlightWord;
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
// pages per minute, first item is 1 to prevent division by zero if accessed
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
// The strip renderer is bounded by panelWidthBytes * rows. Forty rows uses
// 8,000 bytes on the 1,600 px X4 panel and is reused across grayscale pages;
// the existing loop safely processes the additional bands.
constexpr int READER_GRAYSCALE_STRIP_ROWS = 40;
constexpr size_t initialBookmarkCacheCapacity = 16;
constexpr float bookmarkProgressEpsilon = 0.0001f;

#if FREEINK_CAP_BLE_HID_HOST
void logReaderHeapShape(const char* stage) {
  LOG_INF("HEAPSHAPE", "stage=%s free=%u largest=%u min=%u internal8=%u/%u dma8=%u/%u",
          stage ? stage : "unknown", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
}
#endif

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

// SD card folder finished books are moved into. Single source of truth for the path.
// constexpr ⇒ lives in flash .rodata, no DRAM cost.
constexpr char READ_FOLDER[] = "/read";

// True if path is inside READ_FOLDER (starts with "<READ_FOLDER>/"). Non-allocating so
// it is cheap to call from loop(), and avoids reintroducing a separate "/Read/" literal.
bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;  // length of "/Read" (excludes NUL)
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

// The clipping selector and the reader use the same selectable-word model.
// Keeping the small renderer-side copy here lets saved highlights be resolved
// without allocating a second Page or reparsing the EPUB.
bool isSelectableHighlightToken(const char* text) {
  if (!text) return false;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; p++) {
    if (*p < 0x80) {
      if (std::isalnum(*p)) return true;
    } else {
      return true;
    }
  }
  return false;
}

size_t countHighlightWords(const Page& page) {
  size_t count = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine*>(element.get())->getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i)
      if (isSelectableHighlightToken(block->wordText(i))) ++count;
  }
  return count;
}

void collectHighlightWords(const Page& page, GfxRenderer& renderer, const int fontId, const int marginLeft,
                           const int marginTop, std::span<HighlightWord> words) {
  size_t wordIndex = 0;
  uint16_t row = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!isSelectableHighlightToken(text)) continue;
      HighlightWord word;
      word.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      word.y = static_cast<int16_t>(line->yPos + marginTop);
      word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, text, block->wordStyle(i)));
      word.row = row;
      word.text = text;
      word.style = block->wordStyle(i);
      word.focusBoundary = block->focusBoundary(i);
      word.focusSuffixX = block->focusSuffixX(i);
      if (word.focusBoundary > 0) {
        word.width = static_cast<int16_t>(word.focusSuffixX +
                                         renderer.getTextAdvanceX(fontId, text + word.focusBoundary,
                                                                  word.style));
      }
      words[wordIndex++] = word;
      rowHasWords = true;
    }
    if (rowHasWords) ++row;
  }
}

std::string buildHighlightText(const std::span<const HighlightWord> words, const int first, const int last) {
  if (first < 0 || last < first || last >= static_cast<int>(words.size())) return {};
  std::string text;
  text.reserve(256);
  for (int i = first; i <= last; ++i) {
    if (i > first) {
      if (words[i].row != words[i - 1].row) {
        text.push_back('\n');
      } else if (words[i].x > words[i - 1].x + words[i - 1].width + 1) {
        text.push_back(' ');
      }
    }
    if (text.size() >= 1024) break;
    const size_t remaining = 1024 - text.size();
    text.append(words[i].text, std::min(remaining, strlen(words[i].text)));
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  return text;
}

// Font and line-spacing changes can move words between visual rows.  The saved
// clipping text intentionally retains the old wrapping for display/export, but
// that wrapping must not be part of the relocation key.  Collapse all ASCII
// whitespace to one separator while preserving word boundaries and UTF-8 bytes.
// This is only used on the annotation-recovery path; the normal exact-match
// path remains allocation-free after the word-range verification succeeds.
std::string collapseHighlightWhitespace(const std::string& text) {
  // ClipFile sanitizes unsupported punctuation (curly quotes, NBSP, symbol
  // ranges, and malformed UTF-8) before saving. Apply the same inexpensive
  // normalization to freshly rendered EPUB text, otherwise a saved clip such
  // as “quoted text” can never match the page's original UTF-8 bytes.
  const std::string sanitized = ClipFile::normalizeText(text);
  std::string collapsed;
  collapsed.reserve(sanitized.size());
  bool pendingSpace = false;
  for (const unsigned char byte : sanitized) {
    if (std::isspace(byte)) {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !collapsed.empty()) collapsed.push_back(' ');
    collapsed.push_back(static_cast<char>(byte));
    pendingSpace = false;
  }
  return collapsed;
}

// Build the same normalized, whitespace-collapsed representation for a page
// word without changing the source text used by normal EPUB rendering. This
// lets an older clipping whose unsupported punctuation was sanitized on save
// still match the current page during reflow.
void appendCollapsedHighlightWord(std::string& output, const char* text) {
  if (!text || *text == '\0') return;
  const std::string sanitized = ClipFile::normalizeText(text);
  bool pendingSpace = false;
  for (const unsigned char byte : sanitized) {
    if (std::isspace(byte)) {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !output.empty()) output.push_back(' ');
    output.push_back(static_cast<char>(byte));
    pendingSpace = false;
  }
}

bool findHighlightTextRange(const std::span<const HighlightWord> words, const std::string& text, int& first, int& last,
                            const bool ignoreWrapping = false) {
  if (text.empty() || words.empty()) return false;
  const std::string target = ignoreWrapping ? collapseHighlightWhitespace(text) : text;
  if (target.empty()) return false;

  if (ignoreWrapping) {
    // Build one normalized candidate per start and extend it in place.  This
    // avoids allocating/collapsing a temporary string for every (start,end)
    // pair when a page has many words, which matters on the X3.
    for (int start = 0; start < static_cast<int>(words.size()); ++start) {
      std::string candidate;
      candidate.reserve(std::min<size_t>(target.size() + 16, 1024));
      for (int end = start; end < static_cast<int>(words.size()); ++end) {
        if (end > start && (words[end].row != words[end - 1].row ||
                            words[end].x > words[end - 1].x + words[end - 1].width + 1)) {
          candidate.push_back(' ');
        }
        if (candidate.size() < 1024) {
          const size_t remaining = 1024 - candidate.size();
          appendCollapsedHighlightWord(candidate, words[end].text);
          if (candidate.size() > 1024) candidate.resize(1024);
        }
        if (candidate == target) {
          first = start;
          last = end;
          return true;
        }
        if (candidate.size() > target.size() + 16) break;
      }
    }
    return false;
  }

  for (int start = 0; start < static_cast<int>(words.size()); ++start) {
    for (int end = start; end < static_cast<int>(words.size()); ++end) {
      const std::string candidate = buildHighlightText(words, start, end);
      if (candidate == target) {
        first = start;
        last = end;
        return true;
      }
      // Once the assembled text is materially longer than the saved clip,
      // extending this candidate cannot produce a match.
      if (candidate.size() > target.size() + 16) break;
    }
  }
  return false;
}

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

// ClipSelectionActivity stores its percentage using page / pageCount (rather
// than the bookmark mapper's page / (pageCount - 1)). Keep a matching range so
// a clipping on the final page can still be relocated after reflow.
ProgressRange getClippingPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                            const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

// Pick a non-colliding destination path inside /Read/ for a finished book.
// Mirrors the suffixing scheme used elsewhere: "name.epub" -> "name (2).epub", etc.
std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

// Relocate a finished book and its cache dir into /read/, keep it in recents by
// repointing its entry to the new path, and repoint the resume pointer too.
// On rename failure: LOG_ERR and leave everything in place (no UI alert subsystem here).
void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    return;
  }

  // Cache dir is keyed by hash of the epub path (see Epub ctor), so it must be re-keyed.
  const std::string newCachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(dstPath));
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("ERS", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
    }
  }

  // Keep the book in recents (crossink behavior): repoint the entry to its new
  // location instead of dropping it. updatePath persists on success.
  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);
  BOOK_STATES.updatePath(srcPath, dstPath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
}

void drawStyledHighlightWord(GfxRenderer& renderer, const int fontId, const HighlightWord& word, const bool black,
                            const bool forceBold) {
  if (!word.text || *word.text == '\0') return;
  if (word.focusBoundary == 0) {
    const auto style = forceBold ? static_cast<EpdFontFamily::Style>(word.style | EpdFontFamily::BOLD) : word.style;
    renderer.drawText(fontId, word.x, word.y, word.text, black, style);
    return;
  }

  static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
  char boldPrefix[MAX_FOCUS_PREFIX_BYTES + 3]{};
  const size_t textLength = strlen(word.text);
  const size_t prefixLength = std::min<size_t>({static_cast<size_t>(word.focusBoundary), textLength,
                                                sizeof(boldPrefix) - 1});
  memcpy(boldPrefix, word.text, prefixLength);
  boldPrefix[prefixLength] = '\0';
  const auto boldStyle = static_cast<EpdFontFamily::Style>(word.style | EpdFontFamily::BOLD);
  renderer.drawText(fontId, word.x, word.y, boldPrefix, black, boldStyle);
  if (prefixLength < textLength) {
    renderer.drawText(fontId, word.x + word.focusSuffixX, word.y, word.text + prefixLength, black, word.style);
  }
}

uint8_t highlightStyleMask(const std::span<const HighlightWord> words, const int first, const int last) {
  uint8_t mask = 0;
  for (int i = first; i <= last && i >= 0 && i < static_cast<int>(words.size()); ++i) {
    mask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(words[i].style) & 0x03));
    if (words[i].focusBoundary > 0) mask |= static_cast<uint8_t>(1u << 1);
  }
  return mask;
}

}  // namespace

void EpubReaderActivity::onEnter() {
  EpubDiagnostics::Scope diagnostics("reader_enter_start", "reader_enter_end");
  Activity::onEnter();
  mappedInput.setReaderMappingMode(true);
  firstStableRenderComplete.store(false, std::memory_order_release);
  restartHeapSuspendedEmptyBuild.store(false, std::memory_order_release);
  sectionFailureKind = SectionFailureKind::None;
  failedSectionSpineIndex = -1;
  readerMemoryPressureTier = reader_memory::PressureTier::Normal;
  readerCacheBudgetInitialized = false;
  healthyReaderRenderCount = 0;
  readerRenderMinFreeBytes = 0;
  readerRecentMinFreeBytes = 0;
  readerLargestRenderRequestBytes = 0;
  readerLastRenderAllocationFailed = false;
  readerRenderWindowPressure = false;
  readerRenderImageOrGrayWork = false;
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  readerRenderWaitingForBleStop = false;
  readerDeepCacheRescueEligible = false;
  readerDeepCacheRescueReason = "reader_enter_reset";
  readerSurvivalStopRequested.store(false, std::memory_order_release);
  readerSurvivalReturnHeld.store(false, std::memory_order_release);
  readerSurvivalBackoffStartedMs = 0;
  readerSurvivalBackoffDurationMs = 0;
  readerSurvivalLastStopMs = 0;
  readerSurvivalRecentStopCount = 0;
  readerSurvivalSafeRenderCount = 0;
  readerSurvivalBookStopCount.store(0, std::memory_order_release);
  readerSurvivalBookAdmissionBlocked.store(false, std::memory_order_release);
  readerSurvivalBackoffRetryBlocked = false;
  cachedRenderObservedPeakBytes = 0;
  cachedRenderLargestRequestThisRender = 0;
  cachedRenderHighlightBytes = 0;
  cachedRenderTracking = false;
#endif
  readingSessionStartedMs = millis();
  loadingUiPending = false;
  loadingUiRenderArmed = false;

  if (!epub) {
    return;
  }

  ImageBlock::clearSessionRenderFailures();
  // Lazy image extraction: section builds only header-probe images, so the first
  // render of an image page pulls the file out of the EPUB through this hook.
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Reserve the tiled grayscale strip up front only when text AA needs it.
  // Image pages still allocate lazily after the normal Reader BLE preflight.
  if (renderer.supportsStripGrayscale() && SETTINGS.textAntiAliasing != 0) {
    const size_t scratchBytes =
        static_cast<size_t>(renderer.getDisplayWidthBytes()) * READER_GRAYSCALE_STRIP_ROWS;
    if (!renderer.reserveGrayscaleStripScratch(scratchBytes)) {
      LOG_ERR("ERS", "Unable to reserve grayscale strip scratch (%lu bytes)",
              static_cast<unsigned long>(scratchBytes));
    }
  }

  epub->setupCacheDir();

  HalFile f;
  if (ProgressFile::openForRead("ERS", epub->getCachePath(), f)) {
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        // UINT16_MAX is an in-memory navigation sentinel for "open previous
        // chapter on its last page". It should never be treated as persisted
        // resume state after sleep or reopen.
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // A bookmark selected from the home shelf carries its source book and the
  // computed chapter/page position.  Apply it after the normal resume logic
  // (and after the first-open text-reference adjustment) so it always wins,
  // while leaving regular reader launches untouched.
  if (initialBookmark && initialBookmark->hasSavedProgress) {
    const auto& bookmark = *initialBookmark;
    if (bookmark.spineIndex >= 0 && bookmark.spineIndex < epub->getSpineItemsCount()) {
      currentSpineIndex = bookmark.spineIndex;
      nextPageNumber = std::max(0, bookmark.page);
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = std::max(0, bookmark.totalPages);
      LOG_DBG("ERS", "Opening bookmark: spine %d, page %d/%d", currentSpineIndex, nextPageNumber,
              cachedChapterTotalPageCount);
    } else {
      LOG_ERR("ERS", "Ignoring bookmark with invalid spine index %d", bookmark.spineIndex);
    }
  }
  initialBookmark.reset();

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(),
                       epub->getDescription());

  prepareStablePages();

  loadCachedBookmarks();
  loadCachedClippings();

  // Trigger first update
  requestPageRender();
}

void EpubReaderActivity::requestPageRender(const bool immediate) {
  const bool readerWindowDemand = buildSuspendedAtReaderWindow && section && section->pageCount > 0 &&
                                  (section->currentPage >= static_cast<int>(section->pageCount) ||
                                   section->currentPage + BUILD_WINDOW_AHEAD >=
                                       static_cast<int>(section->pageCount));
  if (buildSuspendedAwaitingDemand && section && section->currentPage != buildSuspendedReaderPage &&
      (!buildSuspendedAtReaderWindow || readerWindowDemand)) {
    // A changed page/jump is the explicit demand that permits a new extension
    // episode. For a reader-window suspension, stay paused while the current
    // persisted watermark still covers the active reading window.
    buildSuspendedAwaitingDemand = false;
    buildSuspendedAtReaderWindow = false;
    partialRebuildStartFailed = false;
    buildHeapPaused = false;
    buildHeapFontReclaimAttempted = false;
    buildWaitReason = 0;
    buildWaitSpine = -1;
    buildWaitReaderPage = -1;
    buildWaitPageCount = -1;
    if (section->pageCount == 0) {
      // Section ownership belongs to the render lock, not the input loop.
      restartHeapSuspendedEmptyBuild.store(true, std::memory_order_release);
      nextPageNumber = 0;
    }
    LOG_INF("SCT", "section_resume spine=%d persisted_pages=%u reader_page=%d resume_reason=%s free=%u largest=%u",
            currentSpineIndex, static_cast<unsigned>(section->pageCount), section->currentPage,
            readerWindowDemand ? "reader_window_demand" : "reader_demand", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
  }
  // A page already laid out in the active section can proceed directly to the
  // normal render. Cold opens, chapter transitions, and targets beyond a
  // partial-build watermark retain the preliminary loading phase.
  const bool targetPageReady = section && section->pageCount > 0 && section->currentPage >= 0 &&
                               section->currentPage < static_cast<int>(section->pageCount);
  if (targetPageReady) {
    loadingUiPending = false;
  } else if (!loadingUiRenderArmed) {
    loadingUiPending = true;
  }
  requestUpdate(immediate);
}

void EpubReaderActivity::prepareStablePages() {
  EpubDiagnostics::Scope diagnostics("stable_pages_prepare_start", "stable_pages_prepare_end");
  stablePageIndex = {};
  stablePagesReady = false;
  if (!epub || SETTINGS.pageNumberMode != CrossPointSettings::STABLE_PAGES) return;

  StablePageCache::Index cached;
  if (StablePageCache::load(*epub, cached)) {
    stablePageIndex = std::move(cached);
    stablePagesReady = true;
    LOG_DBG("ERS", "Using stable page cache (%lu pages)", static_cast<unsigned long>(stablePageIndex.totalPages));
    return;
  }

  const unsigned long startedAt = millis();
  const auto result = StablePageCache::build(
      *epub, stablePageIndex,
      [this, startedAt](const uint16_t processed, const uint16_t total, const std::string& href,
                        const uint32_t totalPagesSoFar) {
        // Do not treat the button press that opened the book as a cancellation.
        if (millis() - startedAt > 500 && mappedInput.isPressed(MappedInputManager::Button::Back)) return false;

        std::string itemName = href;
        const size_t slash = itemName.find_last_of('/');
        if (slash != std::string::npos) itemName.erase(0, slash + 1);
        if (itemName.size() > 28) itemName = itemName.substr(0, 25) + "...";

        renderer.clearScreen();
        const std::string message = std::string(tr(STR_PREPARING_STABLE_PAGES)) + "\n" + itemName + "\n" +
                                    std::to_string(processed) + "/" + std::to_string(total) + " chapters\n" +
                                    "~" + std::to_string(totalPagesSoFar) + " pages\n" + std::string(tr(STR_STOP_FOR_NOW));
        const Rect layout = GUI.drawPopup(renderer, message.c_str(), true);
        GUI.fillPopupProgress(renderer, layout, total == 0 ? 0 : (processed * 100) / total);
        return true;
      });

  if (result == StablePageCache::BuildResult::Built || result == StablePageCache::BuildResult::AlreadyCached) {
    stablePagesReady = stablePageIndex.valid();
    LOG_INF("ERS", "Stable page map ready: %s", stablePagesReady ? "yes" : "no");
  } else if (result == StablePageCache::BuildResult::Cancelled) {
    stablePageIndex = {};
    LOG_INF("ERS", "Stable page preparation stopped by user");
  } else {
    stablePageIndex = {};
    LOG_ERR("ERS", "Stable page preparation failed after %lu ms", millis() - startedAt);
  }
}

void EpubReaderActivity::requestExitToHome(const HomeMenuItem item) {
  if (exitHomePending) return;
  pendingPageTurns.clear();
  exitHomePending = true;
  exitHomeRequestedAtMs = millis();
  exitPopupShown = false;
  exitHomeItem = item;
  // Let the main lifecycle stop an already-running host before Home starts
  // drawing. Cold BLE readmission is gated on the completed Home frame.
  bleinput::requestLifecycleReevaluation();
  requestUpdate(true);
}

void EpubReaderActivity::onExit() {
  const uint32_t onExitStartedUs = micros();
  const uint32_t requestToExitMs = exitHomeRequestedAtMs == 0 ? 0 : millis() - exitHomeRequestedAtMs;
#if NOOIR_EPUB_DIAGNOSTICS
  EpubDiagnostics::phaseRecord("reader_exit_begin");
#endif
#if FREEINK_CAP_BLE_HID_HOST
  bleinput::clearReaderBleReturnPending();
  const auto logExitHeap = [](const char* stage) {
    LOG_INF("MEM", "reader_exit stage=%s free=%u max=%u", stage,
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    logReaderHeapShape(stage);
  };
  logExitHeap("before_section_release");
#endif
  Activity::onExit();
  firstStableRenderComplete.store(false, std::memory_order_release);
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  readerRenderWaitingForBleStop = false;
  readerSurvivalStopRequested.store(false, std::memory_order_release);
  readerSurvivalReturnHeld.store(false, std::memory_order_release);
  readerSurvivalBackoffStartedMs = 0;
  readerSurvivalBackoffDurationMs = 0;
  readerSurvivalLastStopMs = 0;
  readerSurvivalRecentStopCount = 0;
  readerSurvivalSafeRenderCount = 0;
  readerSurvivalBookStopCount.store(0, std::memory_order_release);
  readerSurvivalBookAdmissionBlocked.store(false, std::memory_order_release);
  readerSurvivalBackoffRetryBlocked = false;
  readerIdleFontCachesReleased = false;
  readerBleReturnCleanupAttempted = false;
  readerBleReturnCleanupGeneration = 0;
  readerDeepCacheRescueEligible = false;
  readerDeepCacheRescueReason = "reader_exit_reset";
  cachedRenderTracking = false;
  cachedRenderObservedPeakBytes = 0;
  cachedRenderLargestRequestThisRender = 0;
  cachedRenderHighlightBytes = 0;
  cachedRenderStartFree = 0;
  cachedRenderLowestFree = 0;
#endif
  pendingPageTurns.clear();
  qualityRecoveryPending.store(false, std::memory_order_release);
  longOperationIndicator.cancel("reader_exit");
  loadingUiPending = false;
  loadingUiRenderArmed = false;
  mappedInput.setReaderMappingMode(false);
  renderer.setDarkMode(false);

  uint32_t persistenceUs = 0;
  uint32_t persistenceStartedUs = micros();
  if (epub) {
    const ScreenshotInfo info = getScreenshotInfo();
    const bool completed = pendingReadFolderMove ||
                           (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount());
    const uint8_t finalProgress = completed ? 100 : static_cast<uint8_t>(info.progressPercent);
    const uint32_t elapsedSeconds = (millis() - readingSessionStartedMs) / 1000UL;
    uint32_t saveStartedUs = micros();
    RECENT_BOOKS.recordReading(epub->getPath(), finalProgress, elapsedSeconds, sessionPagesTurned);
    LOG_INF("ERS", "reader_exit_save kind=recent us=%lu", static_cast<unsigned long>(micros() - saveStartedUs));
    saveStartedUs = micros();
    BOOK_STATES.recordReading(epub->getPath(), finalProgress, elapsedSeconds, sessionPagesTurned);
    LOG_INF("ERS", "reader_exit_save kind=book_state us=%lu", static_cast<unsigned long>(micros() - saveStartedUs));
    saveStartedUs = micros();
    READING_STATS.recordSession(halClock.getDateKey(), elapsedSeconds, sessionPagesTurned);
    LOG_INF("ERS", "reader_exit_save kind=reading_stats us=%lu", static_cast<unsigned long>(micros() - saveStartedUs));
  }
  persistenceUs += micros() - persistenceStartedUs;

  ReaderUtils::clearGhostingOnExit(renderer);

  // The extractor holds a raw pointer to this activity's epub; drop it before
  // the activity (and the shared_ptr) goes away.
  ImageBlock::setExtractor(nullptr, nullptr);

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Normal reader launches already have a zero crash-recovery counter. Avoid
  // rewriting the whole state JSON on every exit; only persist when boot
  // recovery had incremented the counter and it must be cleared.
  persistenceStartedUs = micros();
  if (APP_STATE.readerActivityLoadCount != 0) {
    APP_STATE.readerActivityLoadCount = 0;
    persistenceStartedUs = micros();
    APP_STATE.saveToFile();
    LOG_INF("ERS", "reader_exit_save kind=app_recovery_state us=%lu",
            static_cast<unsigned long>(micros() - persistenceStartedUs));
  }

  // Leaving mid-footnote loses the in-RAM return stack on deep sleep; persist the
  // pre-footnote position so the book reopens at the link origin, not the footnote.
  if (footnoteDepth > 0 && epub) {
    const SavedPosition& origin = savedPositions[0];
    persistenceStartedUs = micros();
    const bool progressSaved = saveProgress(origin.spineIndex, origin.pageNumber, 0);
    LOG_INF("ERS", "reader_exit_save kind=footnote_progress us=%lu result=%d",
            static_cast<unsigned long>(micros() - persistenceStartedUs), progressSaved ? 1 : 0);
  }

  persistenceUs += micros() - persistenceStartedUs;
  const uint32_t cleanupStartedUs = micros();
  uint32_t sectionReleaseUs = 0;
  uint32_t epubReleaseUs = 0;
  uint32_t finishedBookMoveUs = 0;
  uint32_t cacheReleaseUs = 0;
  uint32_t releaseStartedUs = micros();
  section.reset();
  sectionReleaseUs = micros() - releaseStartedUs;
#if FREEINK_CAP_BLE_HID_HOST
  logExitHeap("after_section_release");
#endif
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    releaseStartedUs = micros();
    epub.reset();  // release the Epub (and any open handles) before renaming on the SD card
    epubReleaseUs = micros() - releaseStartedUs;
    releaseStartedUs = micros();
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
    finishedBookMoveUs = micros() - releaseStartedUs;
  } else {
    releaseStartedUs = micros();
    epub.reset();
    epubReleaseUs = micros() - releaseStartedUs;
  }
#if FREEINK_CAP_BLE_HID_HOST
  logExitHeap("after_epub_release");
#endif
  // Section/parser and EPUB ownership are gone. Release only the existing
  // disposable glyph/page arenas; persistent SD-font layout metadata and UI
  // fallback ownership remain intact for the next screen/book.
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
#if FREEINK_CAP_BLE_HID_HOST
  logExitHeap("after_disposable_font_release");
#endif
  // The exit boundary is ordered deliberately: section/EPUB ownership and
  // disposable font arenas are gone before shared Reader render state is
  // released. The pixel-cache replay slot is global to ImageBlock rather than
  // owned by the page object, so release it defensively here as well.
  ImageBlock::releaseRenderCache();
  renderer.releaseGrayscaleStripScratch();
#if FREEINK_CAP_BLE_HID_HOST
  logExitHeap("after_renderer_shared_release");
#endif
  // Reader layout populates rebuildable advance and kern/ligature tables in
  // the global SD-font manager. Keep the loaded font/coverage registered, but
  // drop those session caches before RecentBooks is allowed to settle.
  sdFontSystem.releaseReaderCaches();
  // The selected Reader-size font's full coverage interval tables are global
  // heap owners that survive the EpubReaderActivity object. Keep separate
  // 8/10/12 pt CJK UI fallbacks registered for RecentBooks, but detach this
  // Reader-only size now; ensureLoaded() reconstructs it once at next entry.
  sdFontSystem.releaseReaderFontOwner(renderer);
#if FREEINK_CAP_BLE_HID_HOST
  logExitHeap("after_reader_shared_cache_release");
#endif
  auto* fontCache = renderer.getFontCacheManager();
  releaseStartedUs = micros();
  const size_t scanCapacityBeforeRelease = fontCache ? fontCache->releaseReaderPressureState() : 0;
  readerMemoryPressureTier = reader_memory::PressureTier::Normal;
  readerCacheBudgetInitialized = false;
  healthyReaderRenderCount = 0;
  readerRenderMinFreeBytes = 0;
  readerRecentMinFreeBytes = 0;
  readerLargestRenderRequestBytes = 0;
  readerLastRenderAllocationFailed = false;
  readerRenderWindowPressure = false;
  readerRenderImageOrGrayWork = false;
  cacheReleaseUs = micros() - releaseStartedUs;
  LOG_INF("MEM", "reader_exit stage=pressure_state_reset scan_capacity=%u free=%u largest=%u",
          static_cast<unsigned>(scanCapacityBeforeRelease), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  LOG_INF("ERS", "reader_exit_profile request_to_on_exit_ms=%u save_us=%lu section_us=%lu epub_us=%lu "
                  "folder_move_us=%lu cache_reset_us=%lu cleanup_us=%lu total_us=%lu",
          static_cast<unsigned>(requestToExitMs), static_cast<unsigned long>(persistenceUs),
          static_cast<unsigned long>(sectionReleaseUs), static_cast<unsigned long>(epubReleaseUs),
          static_cast<unsigned long>(finishedBookMoveUs), static_cast<unsigned long>(cacheReleaseUs),
          static_cast<unsigned long>(micros() - cleanupStartedUs),
          static_cast<unsigned long>(micros() - onExitStartedUs));
#if NOOIR_EPUB_DIAGNOSTICS
  EpubDiagnostics::phaseRecord("reader_exit_cleanup_complete");
#endif
}

void EpubReaderActivity::failSectionEpisode(const SectionFailureKind kind, const char* reason) {
  if (section && section->isBuilding()) section->abandonBuild();
  section.reset();
  pendingPageTurns.clear();
  qualityRecoveryPending.store(false, std::memory_order_release);
  loadingUiPending = false;
  loadingUiRenderArmed = false;
  buildPopupPending = false;
  buildHeapPaused = false;
  automaticPageTurnActive = false;
  sectionFailureKind = kind;
  failedSectionSpineIndex = currentSpineIndex;
  firstStableRenderComplete.store(false, std::memory_order_release);
  LOG_ERR("ERS", "section_episode_terminal reason=%.28s kind=%u spine=%d free=%u largest=%u",
          reason ? reason : "unknown", static_cast<unsigned>(kind), currentSpineIndex,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  bleinput::requestLifecycleReevaluation();
}

void EpubReaderActivity::sampleReaderFreeHeap() {
  const size_t freeHeap = ESP.getFreeHeap();
  if (readerRenderMinFreeBytes == 0 || freeHeap < readerRenderMinFreeBytes) readerRenderMinFreeBytes = freeHeap;
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (cachedRenderTracking) cachedRenderLowestFree = std::min(cachedRenderLowestFree, freeHeap);
#endif
}

void EpubReaderActivity::updateReaderMemoryPressure(const bool successfulRender) {
  const auto previousTier = readerMemoryPressureTier;
  const reader_memory::PressureObservation observation{
      ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
      successfulRender && readerRenderMinFreeBytes > 0 ? readerRenderMinFreeBytes : readerRecentMinFreeBytes,
      readerLargestRenderRequestBytes, readerRenderWindowPressure,
      readerLastRenderAllocationFailed, readerRenderImageOrGrayWork};
  const auto observedTier = reader_memory::classifyPressure(observation);
  if (successfulRender && static_cast<uint8_t>(observedTier) < static_cast<uint8_t>(readerMemoryPressureTier)) {
    if (healthyReaderRenderCount < UINT8_MAX) ++healthyReaderRenderCount;
  } else if (static_cast<uint8_t>(observedTier) >= static_cast<uint8_t>(readerMemoryPressureTier)) {
    healthyReaderRenderCount = 0;
  }
  const auto nextTier = reader_memory::advancePressureTier(
      readerMemoryPressureTier, observedTier, healthyReaderRenderCount);
  if (static_cast<uint8_t>(nextTier) < static_cast<uint8_t>(readerMemoryPressureTier)) {
    healthyReaderRenderCount = 0;
  }
  readerMemoryPressureTier = nextTier;

  if (successfulRender) {
    readerRecentMinFreeBytes = readerRenderMinFreeBytes > 0 ? readerRenderMinFreeBytes : ESP.getFreeHeap();
    readerRenderMinFreeBytes = 0;
  }

#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (readerSurvivalRecentStopCount > 0) {
    const bool safeRender = successfulRender &&
                            bleinput::readerBleCachedRenderHeadroomSafe(observation.freeBytes,
                                                                         observation.largestBlock) &&
                            observation.recentMinFreeBytes >= bleinput::kReaderBleRenderMinFreeHeap;
    if (safeRender) {
      if (readerSurvivalSafeRenderCount < UINT8_MAX) ++readerSurvivalSafeRenderCount;
    } else {
      readerSurvivalSafeRenderCount = 0;
    }
    if (bleinput::readerBleSafeRenderResetsSurvivalBackoff(
            readerSurvivalSafeRenderCount, observation.freeBytes, observation.largestBlock,
            observation.recentMinFreeBytes)) {
      readerSurvivalRecentStopCount = 0;
      readerSurvivalLastStopMs = 0;
      readerSurvivalSafeRenderCount = 0;
      readerSurvivalBackoffDurationMs.store(0, std::memory_order_release);
      const bool retryWasBlocked = readerSurvivalBackoffRetryBlocked.exchange(false, std::memory_order_acq_rel);
      if (retryWasBlocked && !BleHid.isRunning() && bleinput::readerBleReturnPending()) {
        bleinput::allowReaderBleReturnRetry();
        bleinput::signalReaderBleReturnLifecycleEvent("safe_headroom_reset");
      }
      LOG_INF("BLEMEM", "stage=reader_survival_backoff_reset reason=safe_headroom free=%u largest=%u render_min=%u",
              static_cast<unsigned>(observation.freeBytes), static_cast<unsigned>(observation.largestBlock),
              static_cast<unsigned>(observation.recentMinFreeBytes));
    }
  } else {
    readerSurvivalSafeRenderCount = 0;
  }
#endif

  if (!readerCacheBudgetInitialized || readerMemoryPressureTier != previousTier) {
    const auto budget = reader_memory::cacheBudgetFor(readerMemoryPressureTier);
    if (auto* fontCache = renderer.getFontCacheManager()) {
      fontCache->setReaderCacheBudget(budget.pageGlyphLimit, budget.scanTextBytesPerFont);
    }
    readerCacheBudgetInitialized = true;
    LOG_INF("BLEMEM", "stage=reader_pressure tier=%u observed=%u free=%u largest=%u recent_min=%u group_request=%u "
                      "window=%d allocation_failure=%d image_gray=%d cache_glyphs=%u scan_bytes=%u",
            static_cast<unsigned>(readerMemoryPressureTier), static_cast<unsigned>(observedTier),
            static_cast<unsigned>(observation.freeBytes), static_cast<unsigned>(observation.largestBlock),
            static_cast<unsigned>(observation.recentMinFreeBytes),
            static_cast<unsigned>(observation.largestRenderRequestBytes), observation.readerWindowPressure ? 1 : 0,
            observation.renderAllocationFailed ? 1 : 0, observation.imageOrGrayscaleWork ? 1 : 0,
            static_cast<unsigned>(budget.pageGlyphLimit), static_cast<unsigned>(budget.scanTextBytesPerFont));
  }
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (previousTier == reader_memory::PressureTier::Heavy &&
      readerMemoryPressureTier != reader_memory::PressureTier::Heavy &&
      !BleHid.isRunning() && bleinput::readerBleReturnPending()) {
    bleinput::signalReaderBleReturnLifecycleEvent("reader_pressure_recovered");
  }
#endif
}

#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
reader_memory::RenderBudget EpubReaderActivity::cachedRenderBudget(const size_t highlightBytes,
                                                                 const bool pageLoaded) const {
  const size_t stripBytes = renderer.supportsStripGrayscale()
      ? static_cast<size_t>(renderer.getDisplayWidthBytes()) * READER_GRAYSCALE_STRIP_ROWS : 0;
  const auto cacheBudget = reader_memory::cacheBudgetFor(readerMemoryPressureTier);
  return reader_memory::cachedRenderBudget(
      FontCacheManager::MAX_SCAN_FONTS * cacheBudget.scanTextBytesPerFont,
      SdCardFont::MAX_PAGE_GLYPHS * sizeof(uint32_t), stripBytes,
      cacheBudget.pageGlyphLimit * sizeof(EpdGlyph), highlightBytes,
      cachedRenderObservedPeakBytes, readerLargestRenderRequestBytes, pageLoaded);
}

void EpubReaderActivity::sampleCachedRenderHeap() {
  sampleReaderFreeHeap();
  if (cachedRenderTracking) {
    if (auto* fontCache = renderer.getFontCacheManager()) {
      cachedRenderLargestRequestThisRender =
          std::max(cachedRenderLargestRequestThisRender, static_cast<size_t>(fontCache->largestAllocationRequestBytes()));
    }
  }
}

bool EpubReaderActivity::admitReaderFontPrewarm(void* const context, const size_t requiredFreeBytes,
                                                 const size_t requiredLargestBlock) {
  auto* self = static_cast<EpubReaderActivity*>(context);
  if (!self) return false;
  const size_t request = requiredLargestBlock > reader_memory::kLargestBlockMargin
                            ? requiredLargestBlock - reader_memory::kLargestBlockMargin
                            : requiredLargestBlock;
  self->readerLargestRenderRequestBytes = std::max(self->readerLargestRenderRequestBytes, request);
  self->updateReaderMemoryPressure(false);
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largest = ESP.getMaxAllocHeap();
  const bool safe = freeHeap >= requiredFreeBytes && largest >= requiredLargestBlock;
  LOG_INF("HEAPSHAPE", "stage=font_group_preflight free=%u largest=%u request=%u required=%u/%u tier=%u action=%s",
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(largest), static_cast<unsigned>(request),
          static_cast<unsigned>(requiredFreeBytes), static_cast<unsigned>(requiredLargestBlock),
          static_cast<unsigned>(self->readerMemoryPressureTier),
          safe ? "prewarm" : (BleHid.isRunning() ? "yield_ble" : "fallback"));
  if (!safe && BleHid.isRunning()) {
    self->requestReaderSurvivalStop("font_group_preflight", true);
    return false;
  }
  return true;
}

void EpubReaderActivity::requestReaderSurvivalStop(const char* reason, const bool replayRender) {
  // The render task publishes a stop request; only the existing main-thread
  // lifecycle performs NimBLE deinit. A survival stop is not an immediate
  // return-cleanup opportunity. A later genuine heavy boundary clears the hold.
  readerSurvivalReturnHeld.store(true, std::memory_order_release);
  // Stop requests are serialized on the render task. Publish the request only
  // after its timer state so the main lifecycle cannot observe an old zero
  // delay while handling this new stop.
  const bool firstRequestForStop = !readerSurvivalStopRequested.load(std::memory_order_acquire);
  if (firstRequestForStop) {
    const uint8_t bookStopCount = bleinput::nextReaderBleBookSurvivalStopCount(
        readerSurvivalBookStopCount.load(std::memory_order_relaxed));
    readerSurvivalBookStopCount.store(bookStopCount, std::memory_order_release);
    if (bleinput::readerBleBookSurvivalReadmissionBlocked(bookStopCount)) {
      readerSurvivalBookAdmissionBlocked.store(true, std::memory_order_release);
      const size_t recentPageMinFree = readerRenderMinFreeBytes > 0 ? readerRenderMinFreeBytes : readerRecentMinFreeBytes;
      LOG_INF("BLEMEM", "stage=reader_book_readmission_latched stops=%u tier=%u peak=%u largest_request=%u "
                        "page_min_free=%u reset=book_exit",
              static_cast<unsigned>(bookStopCount), static_cast<unsigned>(readerMemoryPressureTier),
              static_cast<unsigned>(cachedRenderObservedPeakBytes),
              static_cast<unsigned>(readerLargestRenderRequestBytes), static_cast<unsigned>(recentPageMinFree));
    }
    const uint32_t now = millis();
    const uint32_t sincePreviousStop = now - readerSurvivalLastStopMs;
    if (readerSurvivalRecentStopCount == 0 ||
        sincePreviousStop >= bleinput::kReaderBleSurvivalBackoffResetMs) {
      readerSurvivalRecentStopCount = 0;
    }
    if (readerSurvivalRecentStopCount < bleinput::kReaderBleSurvivalBackoffMaxStops) {
      ++readerSurvivalRecentStopCount;
    }
    readerSurvivalSafeRenderCount = 0;
    readerSurvivalBackoffStartedMs.store(now, std::memory_order_relaxed);
    readerSurvivalBackoffDurationMs.store(
        bleinput::readerBleSurvivalBackoffMs(readerSurvivalRecentStopCount, sincePreviousStop),
        std::memory_order_relaxed);
    readerSurvivalLastStopMs = now;
    readerSurvivalBackoffRetryBlocked.store(false, std::memory_order_relaxed);
    readerSurvivalStopRequested.store(true, std::memory_order_release);
    LOG_INF("BLEMEM", "stage=reader_survival_backoff stops=%u delay_ms=%u",
            static_cast<unsigned>(readerSurvivalRecentStopCount),
            static_cast<unsigned>(readerSurvivalBackoffDurationMs.load(std::memory_order_relaxed)));
  }
  // This stop is itself the heavy boundary for the replayed render. Preserve
  // a one-shot deep-rescue arm for the matching replay event; the cleanup path
  // still requires BLE pending, quiescence, all cheaper releases, and failed
  // admission before it can release shared Reader font caches.
  readerDeepCacheRescueEligible = true;
  readerDeepCacheRescueReason = "survival_stop_replay";
  if (replayRender) readerRenderWaitingForBleStop = true;
  LOG_INF("BLEMEM", "stage=reader_survival_stop reason=%.24s replay=%u free=%u largest=%u",
          reason, replayRender ? 1U : 0U, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  bleinput::requestLifecycleReevaluation();
}

bool EpubReaderActivity::cachedRenderPreflight(const size_t highlightBytes, const bool pageLoaded) {
  if (!bleinput::bluetoothEnabled()) return true;
  const auto required = cachedRenderBudget(highlightBytes, pageLoaded);
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largest = ESP.getMaxAllocHeap();
  const bool safe = reader_memory::renderAdmitted(freeHeap, largest, required);
  LOG_INF("HEAPSHAPE", "stage=reader_render_preflight phase=%s free=%u largest=%u required=%u/%u action=%s",
          pageLoaded ? "loaded" : "page_load", static_cast<unsigned>(freeHeap), static_cast<unsigned>(largest),
          static_cast<unsigned>(required.freeBytes), static_cast<unsigned>(required.largestBlock),
          safe ? "render" : (BleHid.isRunning() ? "stop_ble" : "defer"));
  if (!safe && BleHid.isRunning()) requestReaderSurvivalStop("render_preflight", true);
  return safe;
}

bool EpubReaderActivity::cachedRenderFinalPassPreflight(const bool hasHighlights) {
  if (!bleinput::bluetoothEnabled()) return true;
  const size_t stripBytes = renderer.supportsStripGrayscale()
      ? static_cast<size_t>(renderer.getDisplayWidthBytes()) * READER_GRAYSCALE_STRIP_ROWS : 0;
  const auto required = reader_memory::finalPassBudget(stripBytes, hasHighlights);
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largest = ESP.getMaxAllocHeap();
  const bool safe = reader_memory::renderAdmitted(freeHeap, largest, required);
  if (!safe) {
    LOG_INF("HEAPSHAPE", "stage=reader_render_preflight phase=font_ready free=%u largest=%u required=%u/%u action=%s",
            static_cast<unsigned>(freeHeap), static_cast<unsigned>(largest),
            static_cast<unsigned>(required.freeBytes), static_cast<unsigned>(required.largestBlock),
            BleHid.isRunning() ? "stop_ble" : "defer");
    if (BleHid.isRunning()) requestReaderSurvivalStop("font_prewarm", true);
  }
  return safe;
}

void EpubReaderActivity::checkReaderSurvival() {
  sampleCachedRenderHeap();
  if (cachedRenderTracking) {
    cachedRenderObservedPeakBytes = std::max(cachedRenderObservedPeakBytes,
        cachedRenderStartFree > cachedRenderLowestFree ? cachedRenderStartFree - cachedRenderLowestFree : 0);
    cachedRenderTracking = false;
  }
  if (!bleinput::bluetoothEnabled() || !BleHid.isRunning()) return;
  const auto required = cachedRenderBudget(cachedRenderHighlightBytes);
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largest = ESP.getMaxAllocHeap();
  const bool stop = reader_memory::survivalStopRequired(true, true, freeHeap, largest, required);
  LOG_INF("BLEMEM", "stage=reader_survival_check free=%u largest=%u required=%u/%u peak=%u action=%s",
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(largest), static_cast<unsigned>(required.freeBytes),
          static_cast<unsigned>(required.largestBlock), static_cast<unsigned>(cachedRenderObservedPeakBytes),
          stop ? "stop" : "keep");
  if (stop) requestReaderSurvivalStop("render_complete", false);
}
#endif

void EpubReaderActivity::reportReaderQuiescent(const char* reason) {
  const bool completedRender = reason && strcmp(reason, "render_complete") == 0;
  if (completedRender) {
    sampleReaderFreeHeap();
    if (auto* fontCache = renderer.getFontCacheManager()) {
        readerLargestRenderRequestBytes = std::max(
            readerLargestRenderRequestBytes,
            std::max(static_cast<size_t>(fontCache->largestAllocationRequestBytes()), cachedRenderLargestRequestThisRender));
      readerLastRenderAllocationFailed = fontCache->failedAllocationCount() > 0;
    }
  }
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (completedRender) checkReaderSurvival();
#endif
  if (completedRender) updateReaderMemoryPressure(true);
  if (bluetoothResourceSensitive()) return;
  LOG_INF("ERS", "reader_quiescent reason=%.20s spine=%d page=%d free=%u largest=%u", reason ? reason : "stable",
          currentSpineIndex, section ? section->currentPage : nextPageNumber,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (bleinput::bluetoothEnabled()) {
    size_t bookmarkStringCapacity = 0;
    for (const auto& bookmark : cachedBookmarks) {
      bookmarkStringCapacity += bookmark.xpath.capacity() + bookmark.summary.capacity();
    }
    size_t clippingTextCapacity = 0;
    for (const auto& clipping : cachedClippings) clippingTextCapacity += clipping.text.capacity();
    LOG_INF("HEAPSHAPE", "stage=reader_core spine=%d page=%d pages=%u build=%u pending=%u free=%u max=%u min=%u",
            currentSpineIndex, section ? section->currentPage : nextPageNumber,
            static_cast<unsigned>(section ? section->pageCount : 0), section && section->isBuilding() ? 1U : 0U,
            pendingPageTurns.hasPending() ? 1U : 0U, static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
    LOG_INF("HEAPSHAPE", "stage=reader_caps_a fn=%u/%u bytes=%u bm=%u/%u bytes=%u",
            static_cast<unsigned>(currentPageFootnotes.size()), static_cast<unsigned>(currentPageFootnotes.capacity()),
            static_cast<unsigned>(currentPageFootnotes.capacity() * sizeof(FootnoteEntry)),
            static_cast<unsigned>(cachedBookmarks.size()), static_cast<unsigned>(cachedBookmarks.capacity()),
            static_cast<unsigned>(cachedBookmarks.capacity() * sizeof(BookmarkEntry)));
    LOG_INF("HEAPSHAPE", "stage=reader_caps_b bmchars=%u clip=%u/%u bytes=%u clipchars=%u hibytes=%u",
            static_cast<unsigned>(bookmarkStringCapacity), static_cast<unsigned>(cachedClippings.size()),
            static_cast<unsigned>(cachedClippings.capacity()),
            static_cast<unsigned>(cachedClippings.capacity() * sizeof(ClippingEntry)),
            static_cast<unsigned>(clippingTextCapacity),
            static_cast<unsigned>(highlightMatches.capacity() * sizeof(SavedHighlightMatch)));
  }
  // A normal cached page render is not a new retry opportunity. Section
  // completion/rebuild, a render that follows an explicitly armed heavy path,
  // and the one replay render which completes after an explicit survival stop
  // are genuine lifecycle boundaries for the one-shot Reader cleanup.
  const bool sectionLifecycleEvent = reason && strcmp(reason, "section_complete") == 0;
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  // A survival stop blocks the retry signal while the host is being torn down.
  // The replayed render is the corresponding event-driven boundary once the
  // host is actually stopped. Consume the hold here so this cannot turn into
  // a per-page retry or polling loop; a failed cleanup remains deferred until
  // another genuine lifecycle boundary.
  const bool survivalStopReplayLifecycleEvent = reason && strcmp(reason, "render_complete") == 0 &&
                                                readerSurvivalReturnHeld.load(std::memory_order_acquire) &&
                                                !BleHid.isRunning();
  // A replay that follows an explicit survival stop keeps the replay event
  // identity even though it also carries a deep-rescue arm.
  const bool heavyRenderLifecycleEvent = reason && strcmp(reason, "render_complete") == 0 &&
                                         readerDeepCacheRescueEligible && !survivalStopReplayLifecycleEvent;
  if (sectionLifecycleEvent || heavyRenderLifecycleEvent || survivalStopReplayLifecycleEvent) {
    readerSurvivalReturnHeld.store(false, std::memory_order_release);
    const char* eventReason = sectionLifecycleEvent ? "section_complete" :
                              heavyRenderLifecycleEvent ? "heavy_render_complete" :
                                                          "survival_stop_replay_complete";
    const uint32_t backoffStartedMs = readerSurvivalBackoffStartedMs.load(std::memory_order_acquire);
    const uint32_t backoffDurationMs = readerSurvivalBackoffDurationMs.load(std::memory_order_acquire);
    const uint32_t backoffElapsedMs = millis() - backoffStartedMs;
    if (backoffDurationMs > 0 && backoffElapsedMs < backoffDurationMs) {
      if (!readerSurvivalBackoffRetryBlocked.exchange(true, std::memory_order_acq_rel)) {
        bleinput::blockReaderBleReturnRetry();
        LOG_INF("BLEMEM", "stage=reader_return_deferred reason=survival_backoff event=%.28s remaining_ms=%u",
                eventReason, static_cast<unsigned>(backoffDurationMs - backoffElapsedMs));
      }
    } else {
      readerSurvivalBackoffDurationMs.store(0, std::memory_order_release);
      readerSurvivalBackoffRetryBlocked.store(false, std::memory_order_release);
      bleinput::signalReaderBleReturnLifecycleEvent(eventReason);
    }
  }
  if (sectionLifecycleEvent || heavyRenderLifecycleEvent) {
    sdFontSystem.signalReaderFontLifecycleBoundary();
  }
#else
  if (sectionLifecycleEvent) sdFontSystem.signalReaderFontLifecycleBoundary();
#endif
  bleinput::requestLifecycleReevaluation();
}

void EpubReaderActivity::openReaderMenu() {
  pendingPageTurns.clear();
  const int currentPage = section ? section->currentPage + 1 : 0;
  const int totalPages = section ? section->estimatedTotalPages() : 0;
  float bookProgress = 0.0f;
  if (epub->getBookSize() > 0 && section && section->estimatedTotalPages() > 0) {
    const float chapterProgress =
        static_cast<float>(section->currentPage) / static_cast<float>(section->estimatedTotalPages());
    bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
  startActivityForResult(std::make_unique<EpubReaderMenuActivity>(
                             renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
                             SETTINGS.orientation, !currentPageFootnotes.empty(), !cachedBookmarks.empty(),
                              !cachedClippings.empty(),
                              QuickActions::Context{section != nullptr, SETTINGS.dictionaryName[0] != '\0',
                                                    KOREADER_STORE.hasCredentials(), false, true,
                                                    FREEINK_CAP_BLE_HID_HOST != 0}),
                         [this](const ActivityResult& result) {
                           // Always apply orientation change even if the menu was cancelled
                           const auto& menu = std::get<MenuResult>(result.data);
                           applyOrientation(menu.orientation);
                           toggleAutoPageTurn(menu.pageTurnOption);
                           if (!result.isCancelled) {
                              if (menu.action == static_cast<int>(EpubReaderMenuActivity::MenuAction::QUICK_ACTIONS)) {
                                executeQuickAction(menu.quickAction);
                              } else {
                                onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                              }
                           }
                         });
}

void EpubReaderActivity::openReaderOptions() {
  pendingPageTurns.clear();
  startActivityForResult(
      std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                             TextSettingsActivity::Tab::Size),
      [this](const ActivityResult&) {
        SETTINGS.saveToFile();
        refreshAfterReaderSettings();
        // The Back/Confirm release that closed Text Settings can still be
        // visible on the first reader loop after the child activity pops.
        // Consume that one frame so it cannot navigate out of the book.
        skipNextButtonCheck = true;
        readerOptionsReturnGuard = true;
        longPowerShortcutFired = false;
        ignoreNextConfirmRelease = false;
        requestPageRender();
      });
}

void EpubReaderActivity::refreshAfterReaderSettings() {
  pendingPageTurns.clear();
  // Typography, margins, line spacing, alignment, and embedded-style options
  // are part of ReaderRenderSpec. Drop the live section so render() reloads or
  // rebuilds it with the new spec. Preserve an approximate page position and
  // let applyDeferredReposition() correct it once the new page count is known.
  RenderLock lock(*this);
  cachedSpineIndex = currentSpineIndex;
  if (section) {
    cachedChapterTotalPageCount = section->estimatedTotalPages();
    nextPageNumber = section->currentPage;
    section.reset();
  } else {
    cachedChapterTotalPageCount = 0;
  }
  currentPageFootnotes.clear();
  idlePrewarmSpine = -1;
  idlePrewarmPage = -1;
  partialRebuildStartFailed = false;
  pagesUntilFullRefresh = 1;
  // The old word coordinates/page numbers belong to the previous layout.
  // Reconcile saved clipping text against the new pagination on demand.
  highlightMatches.clear();
  highlightMatchPageObject = nullptr;
  highlightMatchSpine = -1;
  highlightMatchPage = -1;
  highlightLookupDone = false;
  allowClippingRelocation = true;
}

void EpubReaderActivity::renderSavedHighlights(const Page& page, const int fontId,
                                               const std::span<const ReaderHighlightWord> words) {
  if (!section || cachedClippings.empty()) return;

  const auto logRestoreNotFound = [](const size_t index, const char* reason) {
    LOG_DBG("CLP", "restore result=not_found index=%u reason=%s", static_cast<unsigned>(index), reason);
    LOG_DBG("CLP", "restore_match index=%u result=not_found reason=%s", static_cast<unsigned>(index), reason);
  };

  if (words.empty()) {
    for (size_t index = 0; index < cachedClippings.size(); ++index) {
      const auto& clipping = cachedClippings[index];
      LOG_DBG("CLP", "restore_attempt index=%u spine=%u page=%u savedBold=%u savedStyle=%u",
              static_cast<unsigned>(index), static_cast<unsigned>(clipping.spineIndex),
              static_cast<unsigned>(clipping.page), clipping.bold ? 1u : 0u,
              static_cast<unsigned>(clipping.styleMask));
      logRestoreNotFound(index, "no_words");
    }
    return;
  }

  const int currentPage = section->currentPage;

  // Resolve clipping ranges once per page render. renderContents() invokes
  // this method for the BW pass and every grayscale strip; repeating a
  // content search there made font-change recovery needlessly expensive on
  // the X3. Normal clippings still take the old page/range fast path.
  if (highlightMatchPageObject != &page || highlightMatchSpine != currentSpineIndex ||
      highlightMatchPage != currentPage) {
    highlightMatches.clear();
    highlightMatchPageObject = &page;
    highlightMatchSpine = currentSpineIndex;
    highlightMatchPage = currentPage;
    highlightLookupDone = false;
  }

  if (!highlightLookupDone) {
    const int pageCount = section->estimatedTotalPages();
    const ProgressRange pageRange =
        (allowClippingRelocation && pageCount > 0)
            ? getClippingPageProgressRange(epub, currentSpineIndex, currentPage, pageCount)
            : ProgressRange{0.0f, 1.0f};

    for (size_t clippingIndex = 0; clippingIndex < cachedClippings.size(); ++clippingIndex) {
      auto& clipping = cachedClippings[clippingIndex];
      LOG_DBG("CLP", "restore_attempt index=%u spine=%u page=%u savedBold=%u savedStyle=%u",
              static_cast<unsigned>(clippingIndex), static_cast<unsigned>(clipping.spineIndex),
              static_cast<unsigned>(clipping.page), clipping.bold ? 1u : 0u,
              static_cast<unsigned>(clipping.styleMask));
      if (clipping.spineIndex != currentSpineIndex) {
        logRestoreNotFound(clippingIndex, "spine");
        continue;
      }

      const bool pageMatches = clipping.page == currentPage;
      if (!pageMatches) {
        if (!allowClippingRelocation || pageCount <= 0) {
          logRestoreNotFound(clippingIndex, "relocation_disabled");
          continue;
        }

        // The saved percentage is a cheap location hint independent of the
        // chapter's old page count. It keeps recovery bounded to the likely
        // page instead of doing an O(words²) search on every page in a long
        // chapter. A first-page clipping legitimately has percentage 0.
        const float savedProgress = std::clamp(clipping.percentage, 0.0f, 1.0f);
        // A large font change can move a clip across more than one page. Keep
        // the recovery window bounded, but allow two page widths so the text
        // search is not skipped just because the old page hint is stale.
        const float pageWidth = std::max(0.01f, pageRange.end - pageRange.start);
        const float hintMargin = pageWidth * 2.0f;
        if (savedProgress + bookmarkProgressEpsilon < pageRange.start - hintMargin ||
            savedProgress - bookmarkProgressEpsilon > pageRange.end + hintMargin) {
          logRestoreNotFound(clippingIndex, "progress_hint");
          continue;
        }
      }

      int first = -1;
      int last = -1;
      bool matched = false;
      if (pageMatches && clipping.hasWordRange && clipping.firstWord <= clipping.lastWord &&
          clipping.lastWord < words.size()) {
        first = clipping.firstWord;
        last = clipping.lastWord;
        // Verify the range against the saved text. This handles stale ranges
        // after a cache rebuild while keeping the normal path allocation-free.
        matched = buildHighlightText(words, first, last) == clipping.text;
      }
      if (!matched) {
        matched = findHighlightTextRange(words, clipping.text, first, last);
      }
      if (!matched && allowClippingRelocation) {
        // A typography change can alter only the visual wrapping, not the
        // underlying words. Retry with whitespace collapsed so old line
        // breaks do not make an otherwise valid highlight disappear.
        matched = findHighlightTextRange(words, clipping.text, first, last, true);
      }
      if (!matched) {
        logRestoreNotFound(clippingIndex, "text");
        continue;
      }

      const uint8_t resolvedStyleMask = highlightStyleMask(words, first, last);
      LOG_DBG("CLP", "restore_match index=%u result=matched", static_cast<unsigned>(clippingIndex));
      LOG_DBG("CLP", "restore result=matched spine=%d page=%d style=%u bold=%u", currentSpineIndex, currentPage,
              static_cast<unsigned>(resolvedStyleMask), (resolvedStyleMask & 0x0Au) != 0 ? 1u : 0u);
      LOG_DBG("CLP", "restore style=%u bold=%u savedStyle=%u savedBold=%u", static_cast<unsigned>(resolvedStyleMask),
              (resolvedStyleMask & 0x0Au) != 0 ? 1u : 0u, static_cast<unsigned>(clipping.styleMask),
              clipping.bold ? 1u : 0u);
      LOG_DBG("CLP", "restore_apply index=%u bold=%u style=%u", static_cast<unsigned>(clippingIndex),
              clipping.bold ? 1u : 0u, static_cast<unsigned>(clipping.styleMask));

      if (!pageMatches) {
        // Keep the in-memory range current for the remaining BW/gray passes
        // and for further page redraws in this session. We deliberately do
        // not write the clipping JSON from the render path; that would add SD
        // latency and wear. The content anchor remains authoritative on the
        // next open as well.
        clipping.page = static_cast<uint16_t>(std::max(0, currentPage));
        if (first <= static_cast<int>(UINT16_MAX) && last <= static_cast<int>(UINT16_MAX)) {
          clipping.firstWord = static_cast<uint16_t>(first);
          clipping.lastWord = static_cast<uint16_t>(last);
          clipping.hasWordRange = true;
        }
        LOG_DBG("ERS", "Restored clipping after reflow: spine=%d page=%d", currentSpineIndex, currentPage);
      }

      highlightMatches.push_back(SavedHighlightMatch{static_cast<uint16_t>(first), static_cast<uint16_t>(last),
                                                     clipping.styleMask, clipping.bold});
    }
    highlightLookupDone = true;
  }

  if (highlightMatches.empty()) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  const bool darkMode = renderer.isDarkMode();
  // Keep dark mode's existing white-band/black-text contrast regardless of the
  // light-mode preference. The default black path remains the same solid fill;
  // gray preferences use the renderer's existing lightweight dither pattern.
  Color highlightBackground = Color::Black;
  bool highlightTextBlack = false;
  if (darkMode) {
    highlightBackground = Color::White;
    highlightTextBlack = true;
  } else {
    switch (SETTINGS.highlightColor) {
      case CrossPointSettings::HIGHLIGHT_DARK_GRAY:
        highlightBackground = Color::DarkGray;
        break;
      case CrossPointSettings::HIGHLIGHT_LIGHT_GRAY:
        highlightBackground = Color::LightGray;
        highlightTextBlack = true;
        break;
      case CrossPointSettings::HIGHLIGHT_WHITE:
        highlightBackground = Color::White;
        highlightTextBlack = true;
        break;
      case CrossPointSettings::HIGHLIGHT_BLACK:
      default:
        highlightBackground = Color::Black;
        break;
    }
  }

  for (const auto& match : highlightMatches) {
    const int first = match.firstWord;
    const int last = match.lastWord;
    if (first < 0 || last < first || last >= static_cast<int>(words.size())) continue;
    const uint8_t resolvedStyleMask = highlightStyleMask(words, first, last);
    bool hasFocusBoundary = false;
    for (int i = first; i <= last; ++i) hasFocusBoundary = hasFocusBoundary || words[i].focusBoundary > 0;
    // If a legacy/reflowed page lost the bold bit but the persisted clipping
    // is a bold-only selection, restore it. Focus-split words retain their
    // prefix/suffix boundary and are never forced fully bold.
    const bool forceBold = match.savedBold && (match.savedStyleMask & 0x01u) == 0 && !hasFocusBoundary;
    const bool renderedBold = (resolvedStyleMask & 0x0Au) != 0 || forceBold;
    LOG_DBG("CLP", "render highlight=1 bold=%u style=%u savedBold=%u savedStyle=%u", renderedBold ? 1u : 0u,
            static_cast<unsigned>(resolvedStyleMask), match.savedBold ? 1u : 0u,
            static_cast<unsigned>(match.savedStyleMask));

    // The normal reader is monochrome. Paint one continuous band per line
    // rather than a separate box around every word, then redraw the glyphs in
    // the opposite ink color so the saved highlight remains readable.
    int lineStart = first;
    for (int i = first; i <= last; ++i) {
      if (i != last && words[i + 1].row == words[i].row) continue;
      const auto& startWord = words[lineStart];
      const auto& endWord = words[i];
      const int spanX = startWord.x - 2;
      const int spanRight = endWord.x + endWord.width + 2;
      const int spanWidth = std::max(1, spanRight - spanX);
      if (highlightBackground == Color::Black) {
        renderer.fillRect(spanX, startWord.y - 2, spanWidth, lineHeight + 4, true);
      } else if (highlightBackground == Color::White) {
        renderer.fillRect(spanX, startWord.y - 2, spanWidth, lineHeight + 4, false);
      } else {
        renderer.fillRectDither(spanX, startWord.y - 2, spanWidth, lineHeight + 4, highlightBackground);
      }
      lineStart = i + 1;
    }
    if (darkMode) {
      renderer.setDarkMode(false);
      for (int i = first; i <= last; ++i) {
        const auto& word = words[i];
        drawStyledHighlightWord(renderer, fontId, word, highlightTextBlack, forceBold);
      }
      renderer.setDarkMode(true);
    } else {
      for (int i = first; i <= last; ++i) {
        const auto& word = words[i];
        drawStyledHighlightWord(renderer, fontId, word, highlightTextBlack, forceBold);
      }
    }
  }
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  // A failed gate suspends the episode at the caller. Retaining the parser
  // while waiting for its own heap to become free can never make progress.
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

void EpubReaderActivity::suspendBuildAtHeapGate() {
  suspendBuildAtReaderBoundary("heap", true);
}

void EpubReaderActivity::suspendBuildAtReaderWindow() {
  suspendBuildAtReaderBoundary("reader_window", false);
}

void EpubReaderActivity::suspendBuildAtReaderBoundary(const char* reason,
                                                      const bool releaseSharedReaderCaches) {
  if (!section || !section->isBuilding()) return;
  const size_t beforeFree = ESP.getFreeHeap();
  const size_t beforeLargest = ESP.getMaxAllocHeap();
  const bool readerWindow = reason && strcmp(reason, "reader_window") == 0;
  const bool partialBefore = section->isPartial();
  section->suspendBuild();  // Existing partial format, LUTs and anchors; no new cache contract.
  LOG_INF("SCT", "build_heap_release owner=parser_html_paths spine=%d free=%u->%u largest=%u->%u pages=%u",
          currentSpineIndex, static_cast<unsigned>(beforeFree), ESP.getFreeHeap(),
          static_cast<unsigned>(beforeLargest), ESP.getMaxAllocHeap(), static_cast<unsigned>(section->pageCount));
  const size_t cssBeforeFree = ESP.getFreeHeap();
  const size_t cssBeforeLargest = ESP.getMaxAllocHeap();
  const bool cssReleased = epub && epub->releaseCssParserRetainedStorage();
  LOG_INF("SCT", "build_heap_release owner=empty_css released=%u free=%u->%u largest=%u->%u",
          cssReleased ? 1U : 0U, static_cast<unsigned>(cssBeforeFree), ESP.getFreeHeap(),
          static_cast<unsigned>(cssBeforeLargest), ESP.getMaxAllocHeap());
  const size_t fontBeforeFree = ESP.getFreeHeap();
  const size_t fontBeforeLargest = ESP.getMaxAllocHeap();
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseTransientFontCaches();
  if (releaseSharedReaderCaches) sdFontSystem.releaseReaderCaches();
  sdFontSystem.signalReaderFontLifecycleBoundary();
  LOG_INF("SCT", "build_heap_release owner=build_font_scratch free=%u->%u largest=%u->%u",
          static_cast<unsigned>(fontBeforeFree), ESP.getFreeHeap(),
          static_cast<unsigned>(fontBeforeLargest), ESP.getMaxAllocHeap());
  buildSuspendedAwaitingDemand = true;
  buildSuspendedAtReaderWindow = readerWindow;
  buildHeapPaused = !readerWindow;
  if (section->pageCount > 0 && section->currentPage >= section->pageCount)
    section->currentPage = section->pageCount - 1;
  buildSuspendedReaderPage = section->currentPage;
  if (readerWindow) {
    const size_t afterFree = ESP.getFreeHeap();
    const size_t afterLargest = ESP.getMaxAllocHeap();
    LOG_INF("SCT",
            "reader_window_suspend spine=%d pages=%u reader_page=%d partial=%u free_before=%u free_after=%u "
            "largest_before=%u largest_after=%u bytes_released=%u",
            currentSpineIndex, static_cast<unsigned>(section->pageCount), section->currentPage,
            partialBefore ? 1U : 0U, static_cast<unsigned>(beforeFree), static_cast<unsigned>(afterFree),
            static_cast<unsigned>(beforeLargest), static_cast<unsigned>(afterLargest),
            static_cast<unsigned>(afterFree > beforeFree ? afterFree - beforeFree : 0));
  } else {
    LOG_INF("SCT", "build_wait reason=heap_suspended_until_reader_demand spine=%d pages=%u reader_page=%d free=%u largest=%u",
            currentSpineIndex, static_cast<unsigned>(section->pageCount), section->currentPage,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
}

bool EpubReaderActivity::recoverBuildHeapGate() {
  if (!section || !section->isBuilding()) return false;
  // A yielded parseStep holds no live glyph pointer. Drop only render-only
  // mini/bitmap arenas once for this build episode, keeping coverage, advance
  // tables and the live parser/CSS. Old displayed-page arenas can be the owner
  // that prevents a build continuing even after BLE has stopped.
  if (!buildHeapFontReclaimAttempted) {
    buildHeapFontReclaimAttempted = true;
    const size_t beforeFree = ESP.getFreeHeap();
    const size_t beforeLargest = ESP.getMaxAllocHeap();
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
    sdFontSystem.releaseUiFallbackCaches();
    sdFontSystem.releaseInterfaceFontCaches();
    LOG_INF("SCT", "build_heap_release owner=render_only_font_arenas free=%u->%u largest=%u->%u",
            static_cast<unsigned>(beforeFree), ESP.getFreeHeap(),
            static_cast<unsigned>(beforeLargest), ESP.getMaxAllocHeap());
    if (buildTickHeapGate()) return true;
  }
  suspendBuildAtHeapGate();
  return false;
}

void EpubReaderActivity::showBuildPopup() {
  // Mid-build indexing popup: only during onEnter's blocking build-to-target phase
  // (buildPopupPending), at most once, and only when the framebuffer isn't on loan.
  // If it fires while the loan is active (e.g. the parser's size-based call during
  // startBuild), pending stays set and the deadline check retries after the loan.
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  // HALF-clear the popup when the page replaces it, else "INDEXING" ghosts.
  pagesUntilFullRefresh = 1;
  buildPopupPending = false;
}

void EpubReaderActivity::openDictionaryWordSelect() {
  pendingPageTurns.clear();
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  if (!section) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  // Word geometry must match render(): viewable-area margins plus screen margin.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;

  startActivityForResult(std::make_unique<DictionaryWordSelectActivity>(renderer, mappedInput, std::move(page),
                                                                        orientedMarginLeft, orientedMarginTop),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderActivity::openClipSelection() {
  pendingPageTurns.clear();
  if (!section || !epub) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;

  float progress = 0.0f;
  if (epub->getBookSize() > 0 && section->estimatedTotalPages() > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) /
                                  static_cast<float>(section->estimatedTotalPages());
    progress = epub->calculateProgress(currentSpineIndex, chapterProgress);
  }

  startActivityForResult(
      std::make_unique<ClipSelectionActivity>(renderer, mappedInput, std::move(page), epub->getPath(), epub->getTitle(),
                                               orientedMarginLeft, orientedMarginTop, currentSpineIndex,
                                               section->currentPage, progress),
      [this](const ActivityResult&) {
        loadCachedClippings();
        requestUpdate();
      });
}

void EpubReaderActivity::loop() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (readerSurvivalStopRequested.load(std::memory_order_acquire) && !BleHid.isRunning()) {
    bleinput::blockReaderBleReturnRetry();
    readerSurvivalBackoffRetryBlocked.store(
        readerSurvivalBackoffDurationMs.load(std::memory_order_acquire) > 0, std::memory_order_release);
    readerSurvivalStopRequested.store(false, std::memory_order_release);
  }
  if (readerRenderWaitingForBleStop && !BleHid.isRunning()) {
    readerRenderWaitingForBleStop = false;
    requestUpdate(true);
  }
#endif
  // Wait until the exit popup has physically rendered before beginning the
  // statistics/cache cleanup. This gives immediate feedback and prevents
  // repeated button presses from queuing multiple transitions.
  if (exitHomePending) {
    if (exitPopupShown) {
      activityManager.goHome(exitHomeItem);
    }
    return;
  }

  if (readerOptionsReturnGuard) {
    const auto closingButton = [this](MappedInputManager::Button button) {
      return mappedInput.isPressed(button) || mappedInput.wasReleased(button);
    };
    const bool closingInput = closingButton(MappedInputManager::Button::Back) ||
                              closingButton(MappedInputManager::Button::Confirm) ||
                              closingButton(MappedInputManager::Button::Power) ||
                              closingButton(MappedInputManager::Button::Left) ||
                              closingButton(MappedInputManager::Button::Right) ||
                              closingButton(MappedInputManager::Button::Up) ||
                              closingButton(MappedInputManager::Button::Down);
    if (!closingInput) readerOptionsReturnGuard = false;
    return;
  }
  if (skipNextButtonCheck) {
    skipNextButtonCheck = false;
    return;
  }
  // A configurable long power hold is handled in the reader before the main
  // sleep guard. The main loop deliberately leaves the button alone for these
  // actions; releasing it must not also trigger a short-power action.
  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    if (longPowerShortcutFired) {
      longPowerShortcutFired = false;
      return;
    }
  }
  if (SETTINGS.longPwrBtn != CrossPointSettings::LP_PWR_SLEEP &&
      SETTINGS.longPwrBtn != CrossPointSettings::LP_PWR_IGNORE && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() >= ReaderUtils::GO_HOME_MS && !longPowerShortcutFired) {
    longPowerShortcutFired = true;
    ignoreNextConfirmRelease = true;
    switch (SETTINGS.longPwrBtn) {
      case CrossPointSettings::LP_PWR_READER_OPTIONS:
        openReaderOptions();
        break;
      case CrossPointSettings::LP_PWR_READING_STATS:
        startActivityForResult(
            std::make_unique<ReadingStatsActivity>(renderer, mappedInput, epub ? epub->getPath() : std::string{}),
            [this](const ActivityResult&) { requestUpdate(); });
        break;
      case CrossPointSettings::LP_PWR_SCREENSHOT:
        pendingScreenshot = true;
        requestUpdate();
        break;
      case CrossPointSettings::LP_PWR_BOOKMARK:
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
        break;
      case CrossPointSettings::LP_PWR_DICTIONARY:
        openDictionaryWordSelect();
        break;
      case CrossPointSettings::LP_PWR_DARK_MODE:
        SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
        SETTINGS.saveToFile();
        pagesUntilFullRefresh = 1;
        requestPageRender();
        break;
      case CrossPointSettings::LP_PWR_KOSYNC:
        launchKOReaderSync();
        break;
      case CrossPointSettings::LP_PWR_TOGGLE_BLUETOOTH:
        // The shared main-loop handler performs this action exactly once.
        break;
      case CrossPointSettings::LP_PWR_SLEEP:
      case CrossPointSettings::LP_PWR_IGNORE:
      default:
        break;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) darkShortcutFired = false;
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  // Idle glyph prewarm for the likely next page (currentPage + 1). The scan
  // pass draws nothing (FCM scan mode suppresses pixels), so the displayed
  // framebuffer is untouched; endScanAndPrewarm loads only glyphs not already
  // cached. Debounced past rapid page-flipping, one attempt per position, and
  // deferred while a render/build owns the CPU or the heap is at the render
  // floor. Cross-chapter prewarm is deliberately out of scope (next spine's
  // section isn't loaded).
  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  if (!exitHomePending && section && !section->isBuilding() && !RenderLock::peek() && renderer.hasFrameBuffer() &&
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
      !bleinput::bluetoothEnabled() &&
      !BleHid.isRunning() &&
#endif
      lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
      ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC &&
      (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
    RenderLock lock;  // the page table must not change under the scan
    // Re-check under the lock: peek() and acquisition are not atomic, so the render
    // task may have reset/replaced the section or moved the page in between.
    if (section && !section->isBuilding() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->render(renderer, SETTINGS.getReaderFontId(), 0, 0);  // scan only, no pixels
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
  }

#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  // Bluetooth-on Reader does not run the optional next-page idle prewarm above;
  // after the current display is stable, release any remaining disposable font
  // arenas before centralized BLE admission evaluates this idle state.
  runBleReturnCleanupIfNeeded();
#endif

  // Lazily resume a partial's extension build once the reader nears its watermark. Far from
  // it the rebuild is all cost (whole-chapter re-layout from page 0) and no benefit this
  // session, so reopening a partial deliberately does NOT start it (see the deferral in
  // render()); crossing this margin is the signal that the reader will actually need pages
  // past the watermark soon. Uses the last render's viewport so pagination matches the
  // partial being extended.
  if (section && !section->isBuilding() && section->isPartial() && !buildSuspendedAwaitingDemand &&
      !RenderLock::peek() && buildViewportWidth > 0 &&
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
      !BleHid.isRunning() &&
#endif
      section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount) &&
      !partialRebuildStartFailed) {
    RenderLock lock;
    // Reuse the last render's viewport so the extension paginates identically to the partial.
    const ReaderRenderSpec buildSpec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
    if (!section->startBuild(buildSpec)) {
      // Not fatal: the partial keeps serving its pages; crossing the watermark falls back to
      // the blocking extension in render(). Don't retry every tick.
      partialRebuildStartFailed = true;
      LOG_ERR("ERS", "Failed to start deferred partial extension build");
    } else {
      LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
              section->pageCount);
    }
  }

  // Drive any in-progress incremental section build forward, off the page-turn critical path,
  // but only within a small window ahead of the reader: an unbounded build monopolized the
  // RenderLock and locked out page turns. The build follows the reader instead, and instant
  // reopen comes from suspendBuild() persisting the laid-out pages as a partial on exit.
  // Skip while the render mutex is busy so we never delay a pending render; re-check
  // isBuilding() under the lock since render() may have just finished it.
  // While extending a partial (rebuild from a previous session), pageCount is pinned at the
  // partial's watermark until the build catches up, so the window check would wrongly read
  // "far enough ahead" and stall the build at 0 pages -- then the first turn past the
  // watermark re-parses the whole chapter synchronously. Keep ticking until it finalizes.
  bool backgroundBuildEligible = section && section->isBuilding() && !RenderLock::peek();
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  backgroundBuildEligible = backgroundBuildEligible && !BleHid.isRunning();
#endif
  if (backgroundBuildEligible) {
    const bool insideReaderWindow = section->isPartial() ||
                                    static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD;
    uint8_t waitReason = 0;
    if (!insideReaderWindow) {
      waitReason = 1;
    } else if (!buildTickHeapGate()) {
      waitReason = ESP.getFreeHeap() < BACKGROUND_BUILD_MIN_FREE_HEAP ? 2 : 3;
    }

    if (waitReason != 0) {
      const int readerPage = section->currentPage;
      const int pageCount = static_cast<int>(section->pageCount);
      if (buildWaitReason != waitReason || buildWaitSpine != currentSpineIndex ||
          buildWaitReaderPage != readerPage || buildWaitPageCount != pageCount) {
        const char* reason = waitReason == 1 ? "reader_window" : (waitReason == 2 ? "heap_free" : "heap_largest");
        LOG_INF("SCT", "build_wait reason=%s spine=%d pages=%d reader_page=%d partial=%u free=%u largest=%u floor=%u/%u",
                reason, currentSpineIndex, pageCount, readerPage, section->isPartial() ? 1U : 0U,
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
                static_cast<unsigned>(BACKGROUND_BUILD_MIN_FREE_HEAP),
                static_cast<unsigned>(BACKGROUND_BUILD_MIN_MAX_ALLOC));
        buildWaitReason = waitReason;
        buildWaitSpine = currentSpineIndex;
        buildWaitReaderPage = readerPage;
        buildWaitPageCount = pageCount;
      }
      if (waitReason == 2 || waitReason == 3) {
        RenderLock lock;
        if (section && section->isBuilding() && !buildTickHeapGate()) recoverBuildHeapGate();
      } else if (waitReason == 1) {
        // Once the generated watermark is safely ahead of the reader, persist
        // the resumable prefix and drop parser/layout state. Keeping build_ alive
        // here retains the very allocations that caused the long-session heap
        // collapse. A later page demand near the watermark reopens the build.
        RenderLock lock;
        if (section && section->isBuilding()) suspendBuildAtReaderWindow();
      }
    } else {
      buildWaitReason = 0;
      buildWaitSpine = -1;
      buildWaitReaderPage = -1;
      buildWaitPageCount = -1;
      RenderLock lock;
      // Re-check under the lock: render() (which also holds the RenderLock) may have finalized the
      // build between the outer isBuilding() check and acquiring the lock here, in which case
      // buildSomeMore() would fail and wrongly reset the section. The heap gate must be re-read
      // too: a render that won the lock race can expand retained glyph buffers, invalidating the
      // pre-lock heap reading. cppcheck can't see the cross-task mutation, so it flags this as
      // always true.
      // cppcheck-suppress knownConditionTrueFalse
      if (section && section->isBuilding() && !buildTickHeapGate()) {
        recoverBuildHeapGate();
      } else if (section && section->isBuilding()) {
        if (!section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
          failSectionEpisode(SectionFailureKind::Index, "background_build_failure");
          requestUpdate();
        } else if (section->isBuildComplete()) {
          // Completion is a lifecycle transition: the stable page can now be considered for BLE
          // admission. A changed pagination still needs a render at the remapped position.
          if (applyDeferredReposition()) requestUpdate();
          reportReaderQuiescent("section_complete");
        }
      }
    }
  }

  // End-of-Book screen reached (currentSpineIndex == spine count) means the book is
  // finished. Two independent finished-book features key off this same condition.
  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();

  // Drop this book from the Recent Books list; if the reader then pages back into the book,
  // re-add it. So removal only sticks if the reader leaves while still on the End-of-Book
  // screen. Acts only on the transition (guarded by recentsEntryRemoved) — no per-frame writes.
  // Folio Nooir and Carousel use the persistent recent-book record as their
  // Finished shelf and statistics source, so completed entries must remain in
  // the store.
  if (SETTINGS.removeReadBooksFromRecents &&
      SETTINGS.uiTheme != CrossPointSettings::UI_THEME::FOLIO_NOOIR &&
      SETTINGS.uiTheme != CrossPointSettings::UI_THEME::CAROUSEL) {
    if (atEndOfBook && !recentsEntryRemoved) {
      // Only treat the book as "removed by us" if it was actually in the list, so the
      // re-add branch below doesn't insert a book the feature never removed.
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      // Re-add (goes to front of the list via addBook — accepted ordering side effect).
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(),
                           epub->getDescription());
      recentsEntryRemoved = false;
    }
  }

  // Arm the move here so ANY exit path (Back, Home, file browser) relocates the book into
  // /Read/ in onExit(); paging back off the end screen disarms it (book not actually
  // finished). If removeReadBooksFromRecents also fired, RecentBooksStore::updatePath in the
  // move path becomes a safe no-op since the entry was already removed.
  if (atEndOfBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) || ReaderUtils::isTouchMenuGesture(mappedInput)) {
      automaticPageTurnActive = false;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true);
      return;
    }
  }

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    requestUpdate();
  }

  // While the end screen suggestion menu is showing it owns Confirm/Back/navigation
  // input. Anything it doesn't handle (e.g. long-press Back to the file browser) falls
  // through to the regular handlers below; page turns are absorbed by the end-of-book
  // block. A Confirm release after a long-press function (bookmark/sync) fired is left
  // to the regular Confirm handler below, which consumes it via ignoreNextConfirmRelease.
  if (atEndOfBook && endOfBookOptions.menuActive() &&
      !(ignoreNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        requestExitToHome();
        return;
      case EndOfBookOptions::Action::LastPage:
        currentSpineIndex = std::max(epub->getSpineItemsCount() - 1, 0);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        requestUpdate();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  // Enter reader menu activity on short-press Confirm or a downward swipe from the top edge. A long-press
  // that fired a bound function (bookmark or KOReader sync) sets ignoreNextConfirmRelease so the release
  // following the hold does not also open the menu.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || ReaderUtils::isTouchMenuGesture(mappedInput)) {
    if (ignoreNextConfirmRelease) {
      ignoreNextConfirmRelease = false;
    } else {
      openReaderMenu();
    }
  }

  // Long-press Confirm runs the user-selected function (SETTINGS.longPressMenuFunction).
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        // Hold ~0.4s drops a bookmark at the current page.
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !showBookmarkMessage) {
          addBookmark();
          showBookmarkMessage = true;
          ignoreNextConfirmRelease = true;  // Prevent accidental menu open after adding bookmark
          bookmarkMessageTime = millis();
          requestUpdate();
        }
        break;
      case CrossPointSettings::LP_MENU_KOSYNC:
        // Hold ~1s launches KOReader sync. If sync can't run (no credentials stored), fall
        // through so the normal Confirm-release still opens the reader menu.
        if (mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
          if (launchKOReaderSync()) {
            ignoreNextConfirmRelease = true;  // sync launched or error shown; suppress menu open
            return;
          }
        }
        break;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        // Hold ~0.4s starts dictionary word selection on the current page.
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !showDictionaryMessage) {
          ignoreNextConfirmRelease = true;  // Prevent menu open on the release that follows
          openDictionaryWordSelect();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_DARK_MODE:
        // A short hold toggles reader polarity without opening the menu. The
        // next render re-paints the page using the grayscale-aware dark path.
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !darkShortcutFired) {
          SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
          SETTINGS.saveToFile();
          pagesUntilFullRefresh = 1;
          ignoreNextConfirmRelease = true;
          darkShortcutFired = true;
          requestUpdate();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_READER_OPTIONS:
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
          ignoreNextConfirmRelease = true;
          openReaderOptions();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_SLEEP:
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
          ignoreNextConfirmRelease = true;
          activityManager.requestSleep();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_READING_STATS:
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
          ignoreNextConfirmRelease = true;
          startActivityForResult(
              std::make_unique<ReadingStatsActivity>(renderer, mappedInput, epub ? epub->getPath() : std::string{}),
              [this](const ActivityResult&) { requestUpdate(); });
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_SCREENSHOT:
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
          ignoreNextConfirmRelease = true;
          pendingScreenshot = true;
          requestUpdate();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_TOGGLE_BLUETOOTH:
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS && !darkShortcutFired) {
          darkShortcutFired = true;
          ignoreNextConfirmRelease = true;
          bleinput::toggleBluetooth();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  // Short press Back restores position when viewing a footnote (takes priority over navigation)
  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, epub ? epub->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<EpubReaderActivity*>(ctx)->requestExitToHome(); }})) {
    return;
  }

  // auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);

  // Handle short power button press for footnotes
  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::BOOKMARK) {
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return;
    }
    if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::DARK_MODE) {
      SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
      SETTINGS.saveToFile();
      pagesUntilFullRefresh = 1;
      requestUpdate();
      return;
    }
  }

  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
    } else {
      if (currentPageFootnotes.size() == 1) {
        navigateToHref(currentPageFootnotes[0].href, true);
      } else if (currentPageFootnotes.size() > 1) {
        startActivityForResult(
            std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                navigateToHref(footnoteResult.href, true);
              }
              requestUpdate();
            });
      }
    }
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt, fromSide] = ReaderUtils::detectPageTurn(mappedInput);
#if NOOIR_BLE_DIAGNOSTICS
  const bool fromBlePrev = ReaderUtils::blePageTurnInput(mappedInput, false);
  const bool fromBleNext = ReaderUtils::blePageTurnInput(mappedInput, true);
#endif
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    if (processPendingPageTurn()) return;
    return;
  }

  // At end of the book with no suggestion menu, forward button goes home and back
  // button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    pendingPageTurns.clear();
    qualityRecoveryPending.store(false, std::memory_order_release);
    if (endOfBookOptions.menuActive()) {
      // Selection movement was handled above; absorb leftover page-turn triggers so
      // e.g. "previous" at the top of the list doesn't jump back into the book
      return;
    }
    if (nextTriggered) {
      requestExitToHome();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress = !fromTilt && heldMs > ReaderUtils::SKIP_HOLD_MS;

  // Don't skip chapter after screenshot
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  const auto longPressBehavior = fromSide
                                     ? (SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_CHAPTER_SKIP
                                            ? CrossPointSettings::CHAPTER_SKIP
                                        : SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_ORIENTATION
                                            ? CrossPointSettings::ORIENTATION_CHANGE
                                            : CrossPointSettings::OFF)
                                     : SETTINGS.longPressButtonBehavior;

  if (longPress && fromSide && SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_FONT_SIZE) {
    pendingPageTurns.clear();
    SETTINGS.fontSize = static_cast<uint8_t>((SETTINGS.fontSize + 1) % CrossPointSettings::FONT_SIZE_COUNT);
    SETTINGS.saveToFile();
    // Rebuild the current chapter with the new typography while retaining the
    // approximate page position. The normal cached-position reconciliation in
    // render() corrects the page when the new pagination is known.
    nextPageNumber = section ? section->currentPage : nextPageNumber;
    cachedChapterTotalPageCount = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
    section.reset();
    requestPageRender();
    return;
  }

  if (longPress && fromSide &&
      SETTINGS.sideLongPressAction == CrossPointSettings::SIDE_LONG_TOGGLE_BLUETOOTH) {
    bleinput::toggleBluetooth();
    return;
  }

  if (longPress && !fromSide && SETTINGS.longPressButtonBehavior == CrossPointSettings::CHANGE_FONT_SIZE) {
    pendingPageTurns.clear();
    SETTINGS.fontSize = static_cast<uint8_t>((SETTINGS.fontSize + 1) % CrossPointSettings::FONT_SIZE_COUNT);
    SETTINGS.saveToFile();
    nextPageNumber = section ? section->currentPage : nextPageNumber;
    cachedChapterTotalPageCount = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
    section.reset();
    requestPageRender();
    return;
  }

  if (longPress && longPressBehavior == SETTINGS.CHAPTER_SKIP) {
    pendingPageTurns.clear();
    if (!nextTriggered && section && section->currentPage > 0) {
      section->currentPage = 0;
      requestPageRender();
      return;
    }

    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      if (nextTriggered) {
        currentSpineIndex++;
      } else if (currentSpineIndex > 0) {
        currentSpineIndex--;
      }
      section.reset();
    }
    requestPageRender();
    return;
  }

  if (longPress && longPressBehavior == SETTINGS.ORIENTATION_CHANGE) {
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestPageRender();
    return;
  }

  // Record only after the input has passed the end-of-book, screenshot, and
  // long-action branches: this confirms BLE reached normal page-turn handling.
#if NOOIR_BLE_DIAGNOSTICS
  if (prevTriggered && fromBlePrev)
    bleinput::recordDiagnosticEvent("consumer=EPUB action=page_back");
  else if (nextTriggered && fromBleNext)
    bleinput::recordDiagnosticEvent("consumer=EPUB action=page_forward");
#endif

  // No current section, attempt to rerender the book
  if (!section) {
    if (pendingPageTurns.hasPending() || RenderLock::peek()) {
      if (!pendingPageTurns.enqueue(!prevTriggered)) {
        LOG_DBG("ERS", "page-turn queue full; ignoring additional input");
      }
    }
    requestPageRender();
    return;
  }

  // A queued turn that has already survived one render must stay ahead of a
  // newly arrived input. Preserve that order, allowing opposite input to
  // cancel the still-pending turn before it is executed.
  if (pendingPageTurns.hasPending()) {
    if (!pendingPageTurns.enqueue(!prevTriggered)) {
      LOG_DBG("ERS", "page-turn queue full; ignoring additional input");
    }
    if (!RenderLock::peek()) processPendingPageTurn();
    return;
  }

  // The render task owns the live Section while it is rendering. Do not
  // mutate currentPage from the input task in that window; retain the logical
  // turn and apply it on the next safe loop pass instead.
  if (RenderLock::peek()) {
    if (!pendingPageTurns.enqueue(!prevTriggered)) {
      LOG_DBG("ERS", "page-turn queue full; ignoring additional input");
    }
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }
  pendingPageTurns.clear();

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
  requestPageRender();
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  pendingPageTurns.clear();
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    loadCachedBookmarks();
    if (!result.isCancelled) {
      const auto& sync = std::get<ProgressChangeResult>(result.data);
      int targetSpineIndex = sync.spineIndex;
      int targetPage = sync.page;
      const int activeTotalPages = section ? section->estimatedTotalPages() : 0;
      const bool cachedPageMatchesActiveSection = section && sync.totalPages > 0 &&
                                                  currentSpineIndex == sync.spineIndex && sync.page >= 0 &&
                                                  sync.page < sync.totalPages && activeTotalPages == sync.totalPages;

      if (!cachedPageMatchesActiveSection && sync.hasSavedProgress) {
        const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
        CrossPointPosition fallback =
            ProgressMapper::toCrossPoint(epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
        targetSpineIndex = fallback.spineIndex;
        targetPage = fallback.pageNumber;
      }

      if (currentSpineIndex != targetSpineIndex) {
        RenderLock lock(*this);
        currentSpineIndex = targetSpineIndex;
        nextPageNumber = targetPage;
        section.reset();
      } else if (section && section->currentPage != targetPage) {
        RenderLock lock(*this);
        const int clampedTargetPage = std::max(0, targetPage);
        section->currentPage = clampedTargetPage;
      } else if (!section) {
        nextPageNumber = targetPage;
      }
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              const auto& chapterResult = std::get<ChapterResult>(result.data);
              RenderLock lock(*this);

              currentSpineIndex = chapterResult.spineIndex;

              // If anchor is not empty, it will be used later to calculate the page number.
              pendingAnchor = chapterResult.anchor;

              // Otherwise page 0 will be used.
              nextPageNumber = 0;

              section.reset();
              requestPageRender();
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READER_OPTIONS: {
      openReaderOptions();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::CLIP_TEXT: {
      openClipSelection();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::CLIPPINGS: {
      if (epub) {
        startActivityForResult(
            std::make_unique<EpubReaderClippingListActivity>(renderer, mappedInput, epub->getPath(), epub->getTitle()),
            [this](const ActivityResult&) {
              // The clipping manager can edit or delete entries. Refresh the
              // in-memory highlight set and invalidate the per-page resolver
              // before the reader renders again. Do not leave a stale page
              // pointer here: the next Page allocation may reuse its address.
              loadCachedClippings();
              requestUpdate();
            });
      }
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult& result) {});
          break;
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      requestExitToHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      requestExitToHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      launchKOReaderSync();
      break;
    }
#if FREEINK_CAP_BLE_HID_HOST
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH: {
      startActivityForResult(std::make_unique<BluetoothSettingsActivity>(
                                 renderer, mappedInput, BluetoothSettingsActivity::Mode::ReaderControl),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
#endif
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::READING_DIRECTION:
    case EpubReaderMenuActivity::MenuAction::PAGE_FIT:
    case EpubReaderMenuActivity::MenuAction::GO_TO_PAGE: {
      break;
    }
  }
}

void EpubReaderActivity::executeQuickAction(const int actionValue) {
  const auto action = static_cast<QuickActions::ActionId>(actionValue);
  const QuickActions::Context context{section != nullptr, SETTINGS.dictionaryName[0] != '\0',
                                      KOREADER_STORE.hasCredentials(), false, true,
                                      FREEINK_CAP_BLE_HID_HOST != 0};
  if (!QuickActions::isAvailable(action, context)) return;

  switch (action) {
    case QuickActions::ActionId::RefreshScreen:
      pagesUntilFullRefresh = 1;
      requestPageRender();
      break;
    case QuickActions::ActionId::ToggleDarkMode:
      SETTINGS.readerDarkMode = SETTINGS.readerDarkMode ? 0 : 1;
      SETTINGS.saveToFile();
      pagesUntilFullRefresh = 1;
      requestPageRender();
      break;
    case QuickActions::ActionId::Lookup:
      openDictionaryWordSelect();
      break;
    case QuickActions::ActionId::ReadingStats:
      startActivityForResult(
          std::make_unique<ReadingStatsActivity>(renderer, mappedInput, epub ? epub->getPath() : std::string{}),
          [this](const ActivityResult&) { requestUpdate(); });
      break;
    case QuickActions::ActionId::ToggleBookmark:
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      break;
    case QuickActions::ActionId::ReaderOptions:
      openReaderOptions();
      break;
    case QuickActions::ActionId::Orientation: {
      const uint8_t nextOrientation = static_cast<uint8_t>((SETTINGS.orientation + 1) %
                                                            CrossPointSettings::ORIENTATION_COUNT);
      applyOrientation(nextOrientation);
      requestPageRender();
      break;
    }
    case QuickActions::ActionId::Sync:
      launchKOReaderSync();
      break;
    case QuickActions::ActionId::Screenshot:
      pendingScreenshot = true;
      requestUpdate();
      break;
    case QuickActions::ActionId::Home:
      requestExitToHome();
      break;
    case QuickActions::ActionId::Sleep:
      activityManager.requestSleep();
      break;
    case QuickActions::ActionId::ToggleBluetooth:
      bleinput::toggleBluetooth();
      break;
    case QuickActions::ActionId::None:
      break;
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  if (!KOREADER_STORE.hasCredentials()) return false;  // no-op: nothing to launch
  pendingPageTurns.clear();

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  // Pre-compute local KO position and chapter name while Epub is still in RAM.
  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos = ProgressMapper::toSavedProgress(epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  // Persist current position so the reader resumes at the right page on return.
  // goToReader() depends on this file, so abort the sync if the write fails.
  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;  // acted: surfaced a save error to the user
  }

  // Release Epub and Section to free ~65KB RAM for the TLS handshake.
  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    RenderLock lock(*this);
    if (section) {
      nextPageNumber = section->currentPage;
    }
    // The image extractor holds a raw pointer into this epub (see onEnter);
    // clear it before the early release, mirroring onExit(), or a later image
    // render would call through a dangling context.
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, currentSpineIndex, currentPage, totalPages, std::move(localKoPos),
      std::move(localChapterName), paragraphIndex));
  return true;  // acted: launched the sync activity
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  pendingPageTurns.clear();
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  pendingPageTurns.clear();
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  // calculates page turn duration by dividing by number of pages
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  // resets cached section so that space is reserved for auto page turn indicator when None or progress bar only
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    // Preserve current reading position so we can restore after reflow.
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  qualityRecoveryPending.store(false, std::memory_order_release);
  const int oldSpineIndex = currentSpineIndex;
  const int oldPage = section ? section->currentPage : nextPageNumber;
  if (isForwardTurn) {
    // Advance within the section while there are (or may still be) more pages: either a built
    // page ahead, or the section is still building (windowed), in which case more pages exist
    // beyond the current watermark and render()'s ensure-built pump will lay them out. Only when
    // the section is fully built AND we're on its last page do we move to the next spine -- using
    // the live pageCount alone would mistake the build watermark for the end of a giant spine.
    if (section->currentPage < section->pageCount - 1 || section->isBuilding() || section->isPartial() ||
        buildSuspendedAwaitingDemand) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        currentSpineIndex--;
        section.reset();
      }
    }
  }
  const int newPage = section ? section->currentPage : nextPageNumber;
  if (oldSpineIndex != currentSpineIndex || oldPage != newPage) {
    if (sessionPagesTurned < UINT32_MAX) ++sessionPagesTurned;
    if (oldSpineIndex != currentSpineIndex) {
      EpubDiagnostics::record("chapter_transition", currentSpineIndex, newPage, 0, 0, 0, 0,
                              isForwardTurn ? 1 : 2);
    }
  }
  lastPageTurnTime = millis();
  requestPageRender();
}

#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
void EpubReaderActivity::runBleReturnCleanupIfNeeded() {
  // This is a return path, not a general Reader cache policy. In particular,
  // BLE-off users do not enter it; enabled deferred intent is inherited.
  if (!bleinput::bluetoothEnabled() || WiFi.getMode() != WIFI_MODE_NULL) return;
  if (readerSurvivalStopRequested.load(std::memory_order_acquire)) return;
  const bool returnPending = bleinput::readerBleReturnPending();
  if (!returnPending) {
    readerSurvivalReturnHeld.store(false, std::memory_order_release);
    readerBleReturnCleanupAttempted = false;
    readerBleReturnCleanupGeneration = 0;
    readerDeepCacheRescueEligible = false;
    readerDeepCacheRescueReason = "pending_cleared";
    return;
  }
  // The bounded cooldown still governs early stops. After repeated actual
  // render-survival stops, this book keeps BLE deferred until its Activity exits.
  if (readerSurvivalBookAdmissionBlocked.load(std::memory_order_acquire)) return;
  if (readerSurvivalReturnHeld.load(std::memory_order_acquire)) return;

  const uint32_t backoffDurationMs = readerSurvivalBackoffDurationMs.load(std::memory_order_acquire);
  if (backoffDurationMs > 0) {
    const uint32_t elapsedMs = millis() - readerSurvivalBackoffStartedMs.load(std::memory_order_acquire);
    if (elapsedMs < backoffDurationMs) {
      if (!readerSurvivalBackoffRetryBlocked.exchange(true, std::memory_order_acq_rel)) {
        bleinput::blockReaderBleReturnRetry();
        LOG_INF("BLEMEM", "stage=reader_return_deferred reason=survival_backoff remaining_ms=%u",
                static_cast<unsigned>(backoffDurationMs - elapsedMs));
      }
      return;
    }
    readerSurvivalBackoffDurationMs.store(0, std::memory_order_release);
    readerSurvivalBackoffRetryBlocked.store(false, std::memory_order_release);
    LOG_INF("BLEMEM", "stage=reader_survival_backoff_complete elapsed_ms=%u", static_cast<unsigned>(elapsedMs));
    // Re-arm the existing lifecycle event once the bounded delay has elapsed.
    // The Reader loop is already active, so this adds no queue or timer task.
    bleinput::signalReaderBleReturnLifecycleEvent("survival_backoff_elapsed");
  }

  const uint32_t returnGeneration = bleinput::readerBleReturnGeneration();
  if (returnGeneration != readerBleReturnCleanupGeneration) {
    readerBleReturnCleanupGeneration = returnGeneration;
    readerBleReturnCleanupAttempted = false;
    readerIdleFontCachesReleased = false;
    LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup event_generation=%u", static_cast<unsigned>(returnGeneration));
  }
  if (readerBleReturnCleanupAttempted) return;

  // The normal idle cleanup and the return cleanup are both serialized against
  // the render task. Do not release anything while the visible page, section,
  // or a render request is still live.
  if (BleHid.isRunning() || !epub || !section ||
      !firstStableRenderComplete.load(std::memory_order_acquire) || section->isBuilding() ||
      exitHomePending || pendingPageTurns.hasPending() || qualityRecoveryPending.load(std::memory_order_acquire) ||
      loadingUiPending || loadingUiRenderArmed || RenderLock::peek() || activityManager.hasPendingRenderWork() ||
      bluetoothResourceSensitive() || !activityManager.bluetoothShouldBeActive()) {
    return;
  }

  // The activity/lifecycle checks above establish the only context allowed to
  // use the small soft margin. Keep all normal admission sites on the strict
  // global helper; this wrapper is local to the one-shot idle return cleanup.
  const auto readerBleReturnAdmitted = [](const size_t freeHeap, const size_t largestBlock) {
    return bleinput::readerBleStartMemoryAdmittedForQuiescentEpubReturn(freeHeap, largestBlock, true);
  };

  // Existing idle cleanup: release only disposable Reader font state. It
  // leaves coverage, persistent layout data, the current section, and the
  // current page's navigation state intact.
  readerBleReturnCleanupAttempted = true;
  const auto logReaderBleReclaim = [&](const unsigned stage, const char* item,
                                       const size_t freeBefore, const size_t freeAfter,
                                       const size_t largestBefore, const size_t largestAfter,
                                       const bool admitted) {
    LOG_INF("BLEMEM",
            "reader_ble_reclaim generation=%u stage=%u item=%.32s free_before=%u free_after=%u "
            "largest_before=%u largest_after=%u admission=%d",
            static_cast<unsigned>(returnGeneration), stage, item ? item : "?",
            static_cast<unsigned>(freeBefore), static_cast<unsigned>(freeAfter),
            static_cast<unsigned>(largestBefore), static_cast<unsigned>(largestAfter), admitted ? 1 : 0);
  };
  const auto logReaderBleReclaimResult = [&](const char* result, const bool pending) {
    LOG_INF("BLEMEM", "reader_ble_reclaim result=%s generation=%u pending=%d", result ? result : "?",
            static_cast<unsigned>(returnGeneration), pending ? 1 : 0);
  };

  // Admit a healthy idle return before dropping any Reader caches. If the
  // soft floor already passes, the upcoming stable-page render keeps its
  // prepared font state and the normal render preflight remains responsible
  // for yielding BLE before later heavy work.
  const size_t freeBeforeCleanup = ESP.getFreeHeap();
  const size_t largestBeforeCleanup = ESP.getMaxAllocHeap();
  if (readerBleReturnAdmitted(freeBeforeCleanup, largestBeforeCleanup)) {
    bleinput::allowReaderBleReturnRetry();
    logReaderBleReclaimResult("admit_before_cleanup", false);
    LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup result=not_needed free=%u largest=%u",
            static_cast<unsigned>(freeBeforeCleanup), static_cast<unsigned>(largestBeforeCleanup));
    bleinput::requestLifecycleReevaluation();
    return;
  }

  if (!readerIdleFontCachesReleased) {
    const size_t freeBefore = ESP.getFreeHeap();
    const size_t largestBefore = ESP.getMaxAllocHeap();
    EpubDiagnostics::record("reader_idle_ble_font_release_before", currentSpineIndex,
                            section->currentPage);
    {
      RenderLock cleanupLock;
      if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
      sdFontSystem.releaseInterfaceFontCaches();
    }
    const size_t freeAfter = ESP.getFreeHeap();
    const size_t largestAfter = ESP.getMaxAllocHeap();
    LOG_INF("RIDLE", "item=reader_disposable_font_assets spine=%d page=%d free=%u->%u freed=%u largest=%u->%u gain=%u",
            currentSpineIndex, section->currentPage, static_cast<unsigned>(freeBefore),
            static_cast<unsigned>(freeAfter),
            static_cast<unsigned>(freeAfter > freeBefore ? freeAfter - freeBefore : 0),
            static_cast<unsigned>(largestBefore), static_cast<unsigned>(largestAfter),
            static_cast<unsigned>(largestAfter > largestBefore ? largestAfter - largestBefore : 0));
    logReaderBleReclaim(1, "reader_disposable_font_assets", freeBefore, freeAfter, largestBefore, largestAfter,
                        readerBleReturnAdmitted(freeAfter, largestAfter));
    EpubDiagnostics::record("reader_idle_ble_font_release_after", currentSpineIndex,
                            section->currentPage, 0, freeBefore,
                            freeBefore > freeAfter ? freeBefore - freeAfter : 0, 1);
    readerIdleFontCachesReleased = true;
  }

  const size_t freeBeforeReturn = ESP.getFreeHeap();
  const size_t largestBeforeReturn = ESP.getMaxAllocHeap();
  const bool admissionPasses = readerBleReturnAdmitted(freeBeforeReturn, largestBeforeReturn);
  if (admissionPasses) {
    bleinput::allowReaderBleReturnRetry();
    logReaderBleReclaimResult("admit", false);
    LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup result=not_needed free=%u largest=%u",
            static_cast<unsigned>(freeBeforeReturn), static_cast<unsigned>(largestBeforeReturn));
    bleinput::requestLifecycleReevaluation();
    return;
  }
  if (!bleinput::readerBleReturnCleanupAllowed(
          bleinput::bluetoothEnabled(), activityManager.bluetoothShouldBeActive(), returnPending, true,
          readerIdleFontCachesReleased, BleHid.isRunning(), admissionPasses)) {
    return;
  }

  // Do not repeat a failed return purge on every cached page or Section. The
  // explicit return remains pending quietly until the BLE lifecycle itself
  // changes and arms a fresh return event.
  const bool freeFloorFailed = freeBeforeReturn < bleinput::kReaderBleStartMinFreeHeap;
  const bool largestFloorFailed = largestBeforeReturn < bleinput::kReaderBleStartMinLargestBlock;
  const char* failureShape = freeFloorFailed && largestFloorFailed
                                 ? "free_and_fragmented"
                                 : (freeFloorFailed ? "free_floor" : "fragmented");
  LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup result=run reason=%s free=%u largest=%u floors=%u/%u soft=%u/%u",
          failureShape, static_cast<unsigned>(freeBeforeReturn), static_cast<unsigned>(largestBeforeReturn),
          static_cast<unsigned>(bleinput::kReaderBleStartMinFreeHeap),
          static_cast<unsigned>(bleinput::kReaderBleStartMinLargestBlock),
          static_cast<unsigned>(bleinput::kReaderBleQuiescentReturnMinFreeHeap),
          static_cast<unsigned>(bleinput::kReaderBleQuiescentReturnMinLargestBlock));

  const auto admitReaderBleIfReady = [&](const unsigned stage, const char* item,
                                         const size_t freeBefore, const size_t largestBefore) {
    const size_t freeHeap = ESP.getFreeHeap();
    const size_t largest = ESP.getMaxAllocHeap();
    const bool admitted = readerBleReturnAdmitted(freeHeap, largest);
    logReaderBleReclaim(stage, item, freeBefore, freeHeap, largestBefore, largest, admitted);
    LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup level=1 check=%s result=%s free=%u largest=%u",
            item ? item : "?", admitted ? "admit" : "continue", static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(largest));
    if (!admitted) return false;
    bleinput::allowReaderBleReturnRetry();
    logReaderBleReclaimResult("admit", false);
    bleinput::requestLifecycleReevaluation();
    return true;
  };

  // Return item 1: UI-only fallback font arenas. The selected Reader font and
  // its persistent coverage/layout metadata are explicitly retained.
  size_t uiBeforeFree = 0;
  size_t uiBeforeLargest = 0;
  size_t uiAfterFree = 0;
  size_t uiAfterLargest = 0;
  {
    RenderLock cleanupLock;
    uiBeforeFree = ESP.getFreeHeap();
    uiBeforeLargest = ESP.getMaxAllocHeap();
    sdFontSystem.releaseUiFallbackCaches();
    uiAfterFree = ESP.getFreeHeap();
    uiAfterLargest = ESP.getMaxAllocHeap();
  }
  LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup item=ui_fallback_font_caches free=%u->%u freed=%u largest=%u->%u gain=%u",
          static_cast<unsigned>(uiBeforeFree), static_cast<unsigned>(uiAfterFree),
          static_cast<unsigned>(uiAfterFree > uiBeforeFree ? uiAfterFree - uiBeforeFree : 0),
          static_cast<unsigned>(uiBeforeLargest), static_cast<unsigned>(uiAfterLargest),
          static_cast<unsigned>(uiAfterLargest > uiBeforeLargest ? uiAfterLargest - uiBeforeLargest : 0));
  if (admitReaderBleIfReady(1, "ui_fallback_font_caches", uiBeforeFree, uiBeforeLargest)) return;

  // Return item 2: CSS parser bucket capacity, but only after the parser is
  // empty. Live rules belong to the current/next build and are never purged.
  size_t cssBeforeFree = ESP.getFreeHeap();
  size_t cssBeforeLargest = ESP.getMaxAllocHeap();
  if (epub) {
    auto* cssParser = epub->getCssParser();
    if (cssParser && cssParser->empty()) {
      const size_t cssBeforeBuckets = cssParser->bucketCount();
      bool released = false;
      {
        RenderLock cleanupLock;
        released = epub->releaseCssParserRetainedStorage();
      }
      const size_t cssAfterFree = ESP.getFreeHeap();
      const size_t cssAfterLargest = ESP.getMaxAllocHeap();
      LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup item=empty_css_capacity released=%u buckets=%u->%u freed=%u largest=%u->%u gain=%u",
              released ? 1U : 0U, static_cast<unsigned>(cssBeforeBuckets),
              static_cast<unsigned>(cssParser->bucketCount()),
              static_cast<unsigned>(cssAfterFree > cssBeforeFree ? cssAfterFree - cssBeforeFree : 0),
              static_cast<unsigned>(cssBeforeLargest), static_cast<unsigned>(cssAfterLargest),
              static_cast<unsigned>(cssAfterLargest > cssBeforeLargest ? cssAfterLargest - cssBeforeLargest : 0));
    } else {
      LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup item=empty_css_capacity skipped=live_rules");
    }
  }
  if (admitReaderBleIfReady(1, "empty_css_capacity", cssBeforeFree, cssBeforeLargest)) return;

  // Return item 3: an empty bookmark file still leaves the entry vector at
  // its load-time reserve of 16 BookmarkEntry objects. At this quiescent,
  // one-shot BLE return boundary there are no live entries for the current
  // page/menu to observe, and loadCachedBookmarks()/addBookmark() both rebuild
  // the vector lazily if a later action needs it.
  const size_t bookmarkBeforeFree = ESP.getFreeHeap();
  const size_t bookmarkBeforeLargest = ESP.getMaxAllocHeap();
  const size_t bookmarkBeforeSize = cachedBookmarks.size();
  const size_t bookmarkBeforeCapacity = cachedBookmarks.capacity();
  bool bookmarkCapacityReleased = false;
  if (cachedBookmarks.empty() && cachedBookmarks.capacity() > 0) {
    RenderLock cleanupLock;
    std::vector<BookmarkEntry>().swap(cachedBookmarks);
    bookmarkCapacityReleased = true;
  }
  const size_t bookmarkAfterFree = ESP.getFreeHeap();
  const size_t bookmarkAfterLargest = ESP.getMaxAllocHeap();
  LOG_INF("BLEMEM",
          "stage=reader_ble_return_cleanup item=empty_bookmark_capacity released=%u size=%u capacity=%u->%u bytes=%u freed=%u largest=%u->%u gain=%u",
          bookmarkCapacityReleased ? 1U : 0U, static_cast<unsigned>(bookmarkBeforeSize),
          static_cast<unsigned>(bookmarkBeforeCapacity), static_cast<unsigned>(cachedBookmarks.capacity()),
          static_cast<unsigned>(bookmarkBeforeCapacity * sizeof(BookmarkEntry)),
          static_cast<unsigned>(bookmarkAfterFree > bookmarkBeforeFree ? bookmarkAfterFree - bookmarkBeforeFree : 0),
          static_cast<unsigned>(bookmarkBeforeLargest), static_cast<unsigned>(bookmarkAfterLargest),
          static_cast<unsigned>(bookmarkAfterLargest > bookmarkBeforeLargest
                                    ? bookmarkAfterLargest - bookmarkBeforeLargest
                                    : 0));
  if (admitReaderBleIfReady(1, "empty_bookmark_capacity", bookmarkBeforeFree, bookmarkBeforeLargest)) return;

  // endScanAndPrewarm() clears scan text but retains each std::string's
  // capacity. This is rebuildable and no longer needed at this quiescent
  // return boundary.
  const size_t scanBeforeFree = ESP.getFreeHeap();
  const size_t scanBeforeLargest = ESP.getMaxAllocHeap();
  size_t scanCapacityReleased = 0;
  {
    RenderLock cleanupLock;
    if (auto* fontCache = renderer.getFontCacheManager()) {
      scanCapacityReleased = fontCache->releaseReaderScanStorage();
    }
  }
  const size_t scanAfterFree = ESP.getFreeHeap();
  const size_t scanAfterLargest = ESP.getMaxAllocHeap();
  LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup item=reader_scan_capacity released_bytes=%u freed=%u largest=%u->%u gain=%u",
          static_cast<unsigned>(scanCapacityReleased),
          static_cast<unsigned>(scanAfterFree > scanBeforeFree ? scanAfterFree - scanBeforeFree : 0),
          static_cast<unsigned>(scanBeforeLargest), static_cast<unsigned>(scanAfterLargest),
          static_cast<unsigned>(scanAfterLargest > scanBeforeLargest ? scanAfterLargest - scanBeforeLargest : 0));
  if (admitReaderBleIfReady(1, "reader_scan_capacity", scanBeforeFree, scanBeforeLargest)) return;

  // Level 2 releases only the renderer-owned AA strip, and only after every
  // Level-1 release failed admission at this genuine render-idle boundary.
  // A later grayscale render uses the existing Reader-priority path to stop
  // BLE before rebuilding the strip.
  if (renderer.supportsStripGrayscale()) {
    const size_t scratchBytes = static_cast<size_t>(renderer.getDisplayWidthBytes()) * READER_GRAYSCALE_STRIP_ROWS;
    uint8_t* scratch = renderer.getGrayscaleStripScratch(scratchBytes);
    if (scratch) {
      const size_t scratchBeforeFree = ESP.getFreeHeap();
      const size_t scratchBeforeLargest = ESP.getMaxAllocHeap();
      {
        RenderLock cleanupLock;
        renderer.releaseGrayscaleStripScratch();
      }
      const size_t scratchAfterFree = ESP.getFreeHeap();
      const size_t scratchAfterLargest = ESP.getMaxAllocHeap();
      const bool level2Admitted =
          readerBleReturnAdmitted(scratchAfterFree, scratchAfterLargest);
      LOG_INF("BLEMEM",
              "stage=reader_ble_return_cleanup level=2 item=grayscale_strip_scratch ptr=%p bytes=%u released=1 free=%u->%u largest=%u->%u admitted=%d",
              scratch, static_cast<unsigned>(scratchBytes), static_cast<unsigned>(scratchBeforeFree),
              static_cast<unsigned>(scratchAfterFree), static_cast<unsigned>(scratchBeforeLargest),
              static_cast<unsigned>(scratchAfterLargest), level2Admitted ? 1 : 0);
      logReaderBleReclaim(2, "grayscale_strip_scratch", scratchBeforeFree, scratchAfterFree,
                          scratchBeforeLargest, scratchAfterLargest, level2Admitted);
      if (level2Admitted) {
        bleinput::allowReaderBleReturnRetry();
        logReaderBleReclaimResult("admit", false);
        bleinput::requestLifecycleReevaluation();
        return;
      }
    } else {
      const size_t scratchFree = ESP.getFreeHeap();
      const size_t scratchLargest = ESP.getMaxAllocHeap();
      logReaderBleReclaim(2, "grayscale_strip_scratch", scratchFree, scratchFree, scratchLargest, scratchLargest,
                          false);
    }
  } else {
    const size_t scratchFree = ESP.getFreeHeap();
    const size_t scratchLargest = ESP.getMaxAllocHeap();
    logReaderBleReclaim(2, "grayscale_strip_scratch", scratchFree, scratchFree, scratchLargest, scratchLargest,
                        false);
  }

  // Deep rescue tier: a cold/incomplete-section or heavy image/scratch event
  // may arm it, and a failed strict admission at this fully quiescent return
  // boundary can also request it. The current page/section remains live; this
  // drops only the shared SD-font layout/glyph caches already proven
  // rebuildable by the Reader-exit path. It stays one-shot per return event.
  const size_t deepBeforeFree = ESP.getFreeHeap();
  const size_t deepBeforeLargest = ESP.getMaxAllocHeap();
  bool deepReleased = false;
  const bool deepAdmissionNeeded =
      !readerBleReturnAdmitted(deepBeforeFree, deepBeforeLargest);
  const bool deepResourceSensitive = bluetoothResourceSensitive();
  const bool deepQuiescent = !deepResourceSensitive;
  const bool deepSectionBuildIdle = section && !section->isBuilding();
  const bool deepRenderStable = firstStableRenderComplete.load(std::memory_order_acquire);
  const bool deepRenderLockFree = !RenderLock::peek();
  const bool deepRenderWorkIdle = !activityManager.hasPendingRenderWork();
  const bool deepStopRequestClear = !readerSurvivalStopRequested.load(std::memory_order_acquire);
  const bool deepReplayHoldClear = !readerSurvivalReturnHeld.load(std::memory_order_acquire);
  const bool deepSafe = bleinput::readerBleDeepCacheReclaimSafe(
      returnPending, deepQuiescent, deepSectionBuildIdle, deepRenderStable, deepRenderLockFree, deepRenderWorkIdle,
      deepStopRequestClear, deepReplayHoldClear, !readerRenderWaitingForBleStop);
  const bool deepEligible = bleinput::readerBleDeepCacheReclaimEligible(readerDeepCacheRescueEligible,
                                                                         deepAdmissionNeeded);
  LOG_INF("BLEMEM",
          "reader_deep_reclaim_eligibility generation=%u owner=reader_shared_font_caches eligible=%d "
          "reason=%.24s pending=%d quiescent=%d section_building=%d render_stable=%d render_lock=%d "
          "render_work=%d resource_sensitive=%d stop_requested=%d return_held=%d render_waiting=%d "
          "cheaper_failed=%d free=%u largest=%u",
          static_cast<unsigned>(returnGeneration), deepEligible ? 1 : 0,
          readerDeepCacheRescueReason ? readerDeepCacheRescueReason : "none", returnPending ? 1 : 0,
          deepQuiescent ? 1 : 0, deepSectionBuildIdle ? 0 : 1, deepRenderStable ? 1 : 0,
          deepRenderLockFree ? 0 : 1, deepRenderWorkIdle ? 0 : 1, deepResourceSensitive ? 1 : 0,
          deepStopRequestClear ? 0 : 1, deepReplayHoldClear ? 0 : 1,
          readerRenderWaitingForBleStop ? 1 : 0, deepAdmissionNeeded ? 1 : 0,
          static_cast<unsigned>(deepBeforeFree), static_cast<unsigned>(deepBeforeLargest));
  if (bleinput::readerBleDeepCacheReclaimAllowed(deepEligible, deepSafe, deepAdmissionNeeded)) {
    {
      RenderLock cleanupLock;
      sdFontSystem.releaseReaderCaches();
    }
    deepReleased = true;
    readerDeepCacheRescueEligible = false;
    readerDeepCacheRescueReason = "consumed";
  }
  const size_t deepAfterFree = ESP.getFreeHeap();
  const size_t deepAfterLargest = ESP.getMaxAllocHeap();
  if (deepReleased) {
    LOG_INF("BLEMEM",
            "stage=reader_ble_return_cleanup item=reader_shared_font_caches free=%u->%u freed=%u largest=%u->%u gain=%u",
            static_cast<unsigned>(deepBeforeFree), static_cast<unsigned>(deepAfterFree),
            static_cast<unsigned>(deepAfterFree > deepBeforeFree ? deepAfterFree - deepBeforeFree : 0),
            static_cast<unsigned>(deepBeforeLargest), static_cast<unsigned>(deepAfterLargest),
            static_cast<unsigned>(deepAfterLargest > deepBeforeLargest ? deepAfterLargest - deepBeforeLargest : 0));
    logReaderBleReclaim(3, "reader_shared_font_caches", deepBeforeFree, deepAfterFree,
                        deepBeforeLargest, deepAfterLargest,
                        readerBleReturnAdmitted(deepAfterFree, deepAfterLargest));
  } else {
    LOG_INF("BLEMEM",
            "reader_ble_reclaim generation=%u stage=3 item=reader_shared_font_caches skipped=%s "
            "free_before=%u free_after=%u largest_before=%u largest_after=%u admission=0",
            static_cast<unsigned>(returnGeneration),
            deepEligible && !deepAdmissionNeeded ? "not_needed" : (deepSafe ? "not_eligible" : "unsafe"),
            static_cast<unsigned>(deepBeforeFree), static_cast<unsigned>(deepBeforeFree),
            static_cast<unsigned>(deepBeforeLargest), static_cast<unsigned>(deepBeforeLargest));
  }

  // This is the single admission recheck for the return cleanup.
  const size_t freeAfterReturn = deepAfterFree;
  const size_t largestAfterReturn = deepAfterLargest;
  const bool admitted = readerBleReturnAdmitted(freeAfterReturn, largestAfterReturn);
  LOG_INF("BLEMEM", "stage=reader_ble_return_cleanup result=%s free=%u->%u freed=%u largest=%u->%u gain=%u pending=%d",
          admitted ? "admit" : "defer", static_cast<unsigned>(freeBeforeReturn),
          static_cast<unsigned>(freeAfterReturn),
          static_cast<unsigned>(freeAfterReturn > freeBeforeReturn ? freeAfterReturn - freeBeforeReturn : 0),
          static_cast<unsigned>(largestBeforeReturn), static_cast<unsigned>(largestAfterReturn),
          static_cast<unsigned>(largestAfterReturn > largestBeforeReturn
                                    ? largestAfterReturn - largestBeforeReturn
                                    : 0),
          admitted ? 0 : 1);
  if (admitted) {
    bleinput::allowReaderBleReturnRetry();
    logReaderBleReclaimResult("admit", false);
    bleinput::requestLifecycleReevaluation();
  } else {
    bleinput::blockReaderBleReturnRetry();
    logReaderBleReclaimResult("exhausted", true);
  }
}
#endif

bool EpubReaderActivity::processPendingPageTurn() {
  if (!section || RenderLock::peek()) return false;
  bool forward = true;
  if (pendingPageTurns.take(forward)) {
    qualityRecoveryPending.store(false, std::memory_order_release);
    pageTurn(forward);
    return true;
  }
  if (qualityRecoveryPending.exchange(false, std::memory_order_acq_rel)) {
    requestPageRender(true);
    return true;
  }
  return false;
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  EpubDiagnostics::Scope renderDiagnostics("render_entry", "render_exit", currentSpineIndex,
                                           section ? section->currentPage : -1);
  renderer.setDarkMode(SETTINGS.readerDarkMode != 0);
  renderer.setRenderMode(GfxRenderer::BW);
  if (!epub) {
    return;
  }
  readerRenderMinFreeBytes = 0;
  readerRenderWindowPressure = buildSuspendedAtReaderWindow || buildSuspendedAwaitingDemand || buildHeapPaused;
  readerRenderImageOrGrayWork = false;
  readerLastRenderAllocationFailed = false;
  sampleReaderFreeHeap();
  updateReaderMemoryPressure(false);
  if (restartHeapSuspendedEmptyBuild.exchange(false, std::memory_order_acq_rel)) {
    section.reset();
    nextPageNumber = 0;
  }
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  // Heavy Reader work is serialized after full NimBLE teardown. A normal page
  // render in an already-built section is deliberately allowed to proceed with
  // an active host; the activity-level resource predicate is the boundary that
  // distinguishes that cached path from section/layout/image-reclaim work.
  if (BleHid.isRunning() && bluetoothResourceSensitive()) {
    readerRenderWaitingForBleStop = true;
    return;
  }
  cachedRenderTracking = false;
  cachedRenderLargestRequestThisRender = 0;
  const bool cachedSection = section && !section->isBuilding() && !bluetoothResourceSensitive();
  if (cachedSection) {
    if (!cachedRenderPreflight(cachedRenderHighlightBytes)) return;
    cachedRenderStartFree = ESP.getFreeHeap();
    cachedRenderLowestFree = cachedRenderStartFree;
    cachedRenderTracking = true;
  }
#endif

  // Keep the current page visible while the exit path persists Recent,
  // per-book, and daily reading data. drawPopup() performs the one visible
  // refresh; the main loop queues the home transition only afterward.
  if (exitHomePending) {
    if (!exitPopupShown) {
      GUI.drawPopup(renderer, tr(STR_SAVING_READING_DATA), true);
      exitPopupShown = true;
    }
    return;
  }

  // A page request is split across two render-task turns. The first turn
  // refreshes the already-presented page with one static loading line; the
  // second turn enters the existing synchronous EPUB parser/layout/image
  // pipeline. No decoder or background task is added.
  if (loadingUiPending) {
    loadingUiPending = false;
    loadingUiRenderArmed = true;
    longOperationIndicator.showBeforeWork("EPUB_RENDER", true);
    requestUpdate(true);
    return;
  }
  if (loadingUiRenderArmed) loadingUiRenderArmed = false;

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };

  // A section build failure (e.g. an invalid/corrupt EPUB that fails XML parsing) leaves the
  // "Indexing" popup on screen with no way forward. Surface an explicit error instead of hanging.
  // clearScreen first so the error popup doesn't overlay the stale "Indexing" popup.
  const auto showBuildError = [this]() {
    failSectionEpisode(SectionFailureKind::Index, "build_or_index_failure");
    renderer.clearScreen();
    GUI.drawPopup(renderer, tr(STR_INDEX_FAILED));
    firstStableRenderComplete.store(true, std::memory_order_release);
    reportReaderQuiescent("section_failure");
  };

  if (sectionFailureKind != SectionFailureKind::None && failedSectionSpineIndex == currentSpineIndex) {
    pendingPageTurns.clear();
    renderer.clearScreen();
    const auto& message = sectionFailureKind == SectionFailureKind::PageLoad ? tr(STR_PAGE_LOAD_ERROR)
                                                                             : tr(STR_INDEX_FAILED);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, message, true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    firstStableRenderComplete.store(true, std::memory_order_release);
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    reportReaderQuiescent("terminal_error");
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    pendingPageTurns.clear();
    qualityRecoveryPending.store(false, std::memory_order_release);
    // Sole load site: runs on the render task (serialized by RenderLock); the main
    // task only reads the suggestions once the loaded flag is published
    endOfBookOptions.loadOnce(epub->getPath());
    renderer.clearScreen();
    endOfBookOptions.render(renderer, mappedInput);
    renderer.displayBuffer();
    firstStableRenderComplete.store(true, std::memory_order_release);
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    reportReaderQuiescent("end_of_book_render");
    return;
  }

  auto longOperation = longOperationIndicator.scoped("EPUB_RENDER");
  longOperationIndicator.stage("parsing");

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

  // reserves space for automatic page turn indicator when no status bar or progress bar only
  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin));
  } else {
    orientedMarginBottom += std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  // Capture for loop()'s lazy partial-extension start (must match this render's layout params).
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    buildSuspendedAwaitingDemand = false;
    buildSuspendedAtReaderWindow = false;
    buildSuspendedReaderPage = -1;
    buildHeapPaused = false;
    buildHeapFontReclaimAttempted = false;
    // A new section can be opened within the same render task as the previous
    // page. Do not let the main-loop BLE gate observe the old section's stable
    // flag while this section is still allocating/building.
    firstStableRenderComplete.store(false, std::memory_order_release);
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));
    // Fresh section, fresh chance: a failed lazy extension start in a previous
    // section must not suppress watermark-triggered rebuilds for this one.
    partialRebuildStartFailed = false;

    // A finalized cache serves every page as-is. A partial cache (suspended build from a
    // previous session) serves its pages instantly too, but a build must still run to lay
    // out the rest -- it re-parses from the top in the background (HTML already cached,
    // pages are deterministic) and finalizes, so the partial machinery retires itself.
    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    if (cacheLoaded) {
      // Matching render params means identical pagination, so the saved page number is valid
      // as-is: consume any pending settings-change reposition. Without this, a chapter total
      // saved while the section was still building (i.e. a watermark, not the real count)
      // would remap the resume page against the finalized count and teleport the reader.
      cachedChapterTotalPageCount = 0;
    }
    const bool cacheComplete = cacheLoaded && !section->isPartial();
    if (!cacheComplete) {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
      // Opening/building an incomplete section is the chapter-completion
      // heavy-work boundary for the one gated shared-cache rescue. A loaded,
      // fully cached section never arms this tier.
      readerDeepCacheRescueEligible = true;
      readerDeepCacheRescueReason = "incomplete_section";
#endif
      if (section->isPartial()) {
        LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
      } else {
        LOG_DBG("ERS", "Cache not found, building...");
      }

      // Jumps that need the final pagination or the anchor map -- explicit page jumps,
      // fragment anchors, percent jumps, and cross-setting progress repositioning -- can't
      // resolve their landing page until the whole chapter is laid out, so they take the full
      // (blocking) build with the indexing popup. Everything else -- plain forward reads, resume,
      // and explicit page jumps -- only needs a specific page, so it builds incrementally to that
      // page and finishes the rest in loop(). The settings-change reposition (cachedChapterTotal*)
      // is NOT a full-build trigger: it's deferred to applyDeferredReposition() once the real page
      // count is known, so it never blocks the first page.
      // Only a percent jump truly needs the whole chapter up front (percent -> page needs the final
      // page count). Anchor jumps (TOC / chapter select / footnotes) resolve incrementally below --
      // the anchor is recorded as its page is laid out, so a chapter-top anchor lands on page 0
      // without indexing the whole chapter.
      const bool needsFullBuild = pendingPercentJump;
      if (needsFullBuild) {
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        // The popup's own refresh is a plain FAST, so force the page that replaces it onto the HALF
        // ghost-cleanup path -- otherwise the "INDEXING" text ghosts under the rendered page.
        pagesUntilFullRefresh = 1;
        // No popup redraws while the framebuffer is lent to the build below;
        // the panel holds the popup displayed above (e-ink is persistent).
        const auto popupFn = [this]() {
          if (renderer.hasFrameBuffer()) GUI.drawPopup(renderer, tr(STR_INDEXING));
        };
        // Lend the framebuffer's 48 KB to the blocking full build; restored
        // (white) at scope exit, and the page render below redraws everything.
        GfxRenderer::FrameBufferLoan loan(renderer);
        if (!section->createSectionFile(renderSpec, popupFn)) {
          LOG_ERR("ERS", "Failed to persist page data to SD");
          section.reset();
          loan.end();  // restore before anything draws
          showBuildError();
          return;
        }
        loan.end();
      } else {
        // Lay out just enough to show the landing page; loop() builds the rest behind it. Show the
        // indexing popup up front only when the build will actually be slow: a large spine (its
        // whole HTML must be inflated before page 1 can lay out -- the giant single-spine case), or
        // a deep resume/jump that must lay out many pages to reach the landing page. Tiny sections
        // build in a blink and stay popup-free.
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        // Landing well inside a partial: the page (or anchor, via the on-disk map) is already
        // servable, so don't restart the extension build now -- it re-lays out the WHOLE chapter
        // from page 0 (minutes of background CPU + SD writes on a giant spine), pure waste when
        // the reader never nears the watermark this session. loop() starts it lazily once the
        // reader is within PARTIAL_REBUILD_START_MARGIN pages of the watermark.
        if (section->isPartial() &&
            (anchorJump ? section->getPageForAnchor(pendingAnchor).has_value()
                        : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
              (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
          // Popup only when the build will actually be slow: a big spine whose HTML still needs
          // inflating (the multi-second cost), or a deep page target. A reopen with cached HTML builds
          // fast, so no popup -- that's what made an already-indexed book look like it was reindexing.
          // A partial cache that already covers the target page shows it instantly: never popup.
          const bool willInflate = !section->hasHtmlCache();
          bool showPopup;
          if (anchorJump) {
            // An anchor jump's cost is bounded by the anchor's page, not `target`. An anchor already
            // in the on-disk map (partial or finalized cache) lands instantly: no popup. Otherwise it
            // lies beyond the indexed watermark and the build may lay out the whole spine to find it,
            // so gate on spine size alone -- laying out a big spine takes seconds even with cached
            // HTML. Ordinary chapter-top TOC jumps resolve on page 0 and stay popup-free.
            showPopup = !section->findAnchor(pendingAnchor).has_value() && spineBytes > BUILD_POPUP_BYTE_THRESHOLD;
          } else {
            const bool targetAvailable = target < static_cast<int>(section->pageCount);
            showPopup = !targetAvailable && ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) ||
                                             target > BUILD_POPUP_PAGE_THRESHOLD);
          }
          if (showPopup) {
            GUI.drawPopup(renderer, tr(STR_INDEXING));
            // HALF-clear the popup when the page replaces it, else "INDEXING" ghosts under the page.
            pagesUntilFullRefresh = 1;
          }
          // Mid-build popup surfacing for slow builds the predictive gates can't
          // see (image extraction/probing inside a single page, or any chunk
          // overrunning the deadline). The parser fires the callback before the
          // first image probe; buildPopupPending gates it to this blocking phase
          // so a background build in loop() can never draw over a displayed page.
          buildPopupPending = !showPopup;
          const unsigned long buildStartMs = millis();
          bool started;
          {
            // Lend the framebuffer's 48 KB to startBuild only (the spine HTML
            // inflation peak). The chunk loop below runs without it so the popup
            // can draw mid-build; background chunks never had the loan either.
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(); });
          }
          if (!started) {
            LOG_ERR("ERS", "Failed to start section build");
            section.reset();
            buildPopupPending = false;
            showBuildError();
            return;
          }
          while (!section->isBuildComplete() &&
                 (anchorJump ? !section->findAnchor(pendingAnchor) : static_cast<int>(section->pageCount) <= target)) {
            if (!buildTickHeapGate()) {
              if (!recoverBuildHeapGate()) break;
            }
            // Anchor jump: build until the anchor's page is laid out (usually page 0), checking a
            // partial's on-disk anchor map too so an already-indexed anchor resolves immediately.
            // Otherwise: build until the target page exists. loop() builds the rest behind it.
            if (buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
              // The predictive gates guessed fast but the build blew the silent budget.
              showBuildPopup();
            }
            if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
              LOG_ERR("ERS", "Failed during incremental section build");
              section.reset();
              buildPopupPending = false;
              showBuildError();
              return;
            }
          }
          buildPopupPending = false;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      }
    }

    if (!pendingAnchor.empty()) {
      // Resolve from the pages laid out so far and/or the on-disk map (finalized or partial).
      const auto page = section->findAnchor(pendingAnchor);
      if (page) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  // Extend the build to the requested page if needed (for partials and in-progress builds).
  // This runs every render, so it covers both the first page and any forward turn that gets
  // ahead of the background builder; pages already built do no work here.
  //
  // Crossing a partial's watermark before the extension rebuild has caught up means a
  // synchronous wait spanning the remaining prefix re-layout -- potentially tens of
  // seconds on a giant spine. Show the indexing popup so it isn't a silent freeze
  // (the page that replaces it takes the HALF ghost-cleanup path). Ordinary window
  // catch-ups on a non-partial build are a page or two and stay popup-free.
  if (buildSuspendedAwaitingDemand) {
    if (section->pageCount == 0) return;  // Keep the prior frame; Back/new demand stays usable.
    if (section->currentPage >= section->pageCount) section->currentPage = section->pageCount - 1;
    buildSuspendedReaderPage = section->currentPage;
  }
  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    pagesUntilFullRefresh = 1;
  }
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    // Start a build to extend a partial toward the requested page.
    if (!section->isBuilding() && !section->startBuild(renderSpec)) {
      LOG_ERR("ERS", "Failed to start partial extension build");
      section.reset();
      showBuildError();
      return;
    }
    // Extend until either the target page exists or the build completes.
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!buildTickHeapGate()) {
        if (!recoverBuildHeapGate()) {
          if (section->pageCount == 0) return;
          break;
        }
      }
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
    }
  }
  // For an in-progress incremental build, make sure the page we're about to show has been laid out.
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!buildTickHeapGate()) {
        if (!recoverBuildHeapGate()) {
          if (section->pageCount == 0) return;
          break;
        }
      }
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
    }
  }

  // The requested page is now as built as it will get. If it still lands past the end,
  // clamp to the last real page: the UINT16_MAX "last page" sentinel from backward chapter
  // navigation, an explicit jump beyond a finished chapter, or a stale saved position.
  // Guarded on !isBuilding() because a still-building section's pageCount is only the current
  // watermark (not the final count) and has already been driven far enough by the loops above.
  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  // Apply a deferred settings-change reposition now that the real page count is known (a no-op for
  // a plain resume / unchanged pagination). If still building, this defers to loop() on completion.
  applyDeferredReposition();

  longOperationIndicator.stage("rendering");
  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    firstStableRenderComplete.store(true, std::memory_order_release);
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    longOperation.complete();
    reportReaderQuiescent("empty_section_render");
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    firstStableRenderComplete.store(true, std::memory_order_release);
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    longOperation.complete();
    reportReaderQuiescent("page_bounds_render");
    return;
  }

  updateBookmarkFlag();

  {
    // Unified page read: the in-progress build's in-RAM table if it has reached the page,
    // otherwise the on-disk file (finalized section, or a partial from a previous session).
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      automaticPageTurnActive = false;
      // Retrying rebuilds a transiently corrupt section and usually recovers, but a page that keeps
      // failing would loop forever on a blank screen, so bound the retries before giving up.
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      // Abandon (not suspend) any active build BEFORE clearing: clearCache deletes the files,
      // and the destructor's suspend would otherwise commit tables into a deleted handle.
      section->abandonBuild();
      const bool cacheCleared = section->clearCache();
      if (giveUp || !cacheCleared) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;  // Reset so a later user-initiated navigation can try afresh
        failSectionEpisode(SectionFailureKind::PageLoad,
                           cacheCleared ? "page_load_retry_limit" : "section_cache_invalidation_failed");
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        firstStableRenderComplete.store(true, std::memory_order_release);
        showPendingSyncSaveError();
        reportReaderQuiescent("page_load_failure");
        return;
      }
      section.reset();
      requestUpdate();  // Try again after clearing cache
      showPendingSyncSaveError();
      return;
    }
    pageLoadRetryCount = 0;  // Reset the retry counter once a page loads cleanly

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    if (!renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft)) {
      // Missing-image work or an unsafe font/highlight budget requests full
      // NimBLE teardown and one replay after it is stopped. With BLE already
      // off, an impossible budget stays deferred until explicit reader demand.
      return;
    }
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
  }
  // Only persist when the position actually changed. render() also runs on menu,
  // bookmark and screenshot re-renders, and writeAtomic is several FAT ops for 6 bytes.
  // Every real page turn changes currentPage, so progress durability is unaffected.
  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->pageCount != lastSavedPageCount) {
    if (saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages())) {
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = section->currentPage;
      lastSavedPageCount = section->estimatedTotalPages();
    }
  }

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
  }

  if (showDictionaryMessage) {
    GUI.drawPopup(renderer, tr(STR_DICT_NO_DICT_SET));
  }
  longOperation.complete();
  // Only now has the first usable page completed its full render, refresh,
  // progress bookkeeping, and cleanup. A completed Section build is
  // deliberately insufficient because it may still be inside this render task.
  firstStableRenderComplete.store(true, std::memory_order_release);
  reportReaderQuiescent("render_complete");
}

bool EpubReaderActivity::applyDeferredReposition() {
  if (cachedChapterTotalPageCount == 0 || !section || section->isBuilding()) {
    return false;
  }
  bool changed = false;
  // Only remap when the chapter actually re-paginated (e.g. after a settings change). A plain
  // resume has identical pagination, so section->pageCount == cachedChapterTotalPageCount and
  // nothing moves.
  if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
    const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
    int newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  cachedChapterTotalPageCount = 0;  // consumed; don't read cached progress again
  return changed;
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount);
}
bool EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int diagnosticSpine = currentSpineIndex;
  const int diagnosticPage = section ? section->currentPage : -1;
  EpubDiagnostics::Scope renderContentsDiagnostics("render_contents_entry", "render_cleanup", diagnosticSpine,
                                                   diagnosticPage);
  const int fontId = SETTINGS.getReaderFontId();
  const bool guideDots = SETTINGS.guideDots != 0;
  const bool pageNeedsImageDecode = page->hasImagesNeedingDecode();
  readerRenderImageOrGrayWork = pageNeedsImageDecode || page->hasImages() || SETTINGS.textAntiAliasing;
  readerRenderWindowPressure = buildSuspendedAtReaderWindow || buildSuspendedAwaitingDemand || buildHeapPaused;
  sampleReaderFreeHeap();
  updateReaderMemoryPressure(false);
  // Clippings elsewhere in the book do not require a word arena for this
  // spine. Preserve the existing page/reflow matching below for relevant clips.
  const bool spineHasClippings = std::any_of(cachedClippings.begin(), cachedClippings.end(), [this](const auto& clip) {
    return clip.spineIndex == currentSpineIndex;
  });
  const size_t highlightWordCount = spineHasClippings ? countHighlightWords(*page) : 0;
  const size_t highlightBytes = highlightWordCount * sizeof(ReaderHighlightWord);
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (!pageNeedsImageDecode && !cachedRenderPreflight(highlightBytes, true)) return false;
  cachedRenderHighlightBytes = highlightBytes;
  sampleCachedRenderHeap();
#endif
  auto highlightStorage = highlightWordCount ? makeUniqueNoThrow<ReaderHighlightWord[]>(highlightWordCount) : nullptr;
  if (highlightWordCount && !highlightStorage) {
    LOG_ERR("ERS", "render_deferred reason=highlight_workspace requested=%u free=%u largest=%u",
            static_cast<unsigned>(highlightBytes), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
    if (bleinput::bluetoothEnabled() && BleHid.isRunning()) requestReaderSurvivalStop("highlight_workspace", true);
#endif
    return false;
  }
  std::span<ReaderHighlightWord> highlightWords(highlightStorage.get(), highlightWordCount);
#if FREEINK_CAP_BLE_HID_HOST
  const bool logBleCachedRender = BleHid.isRunning() && !pageNeedsImageDecode;
  if (logBleCachedRender) logReaderHeapShape("reader_cached_render_before");
  struct CachedRenderHeapLog {
    bool enabled;
    ~CachedRenderHeapLog() {
      if (enabled) logReaderHeapShape("reader_cached_render_after");
    }
  } cachedRenderHeapLog{logBleCachedRender};
#else
  const bool logBleCachedRender = false;
#endif
  // A pending manual turn means this destination is intermediate. Keep the
  // normal BW page (including image warmup/decode and highlights), but defer
  // only the expensive grayscale/AA replay. When the queue is empty the
  // existing full-quality path below is used unchanged.
  const bool intermediatePage = pendingPageTurns.hasPending();
  qualityRecoveryPending.store(intermediatePage, std::memory_order_release);

#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  // A built section can still contain a first-use image decode. Treat that
  // exact cache-miss path as heavy, while allowing ordinary cached page turns
  // to keep an already-running NimBLE host.
  if (BleHid.isRunning() && pageNeedsImageDecode) {
    readerDeepCacheRescueEligible = true;
    readerDeepCacheRescueReason = "image_decode";
    readerRenderWaitingForBleStop = true;
    bleinput::requestLifecycleReevaluation();
    return false;
  }
#endif

  // The image pixel-cache RAM slot lives for exactly one page render (it feeds
  // the BW double-refresh and every grayscale band pass); release it on every
  // exit so nothing stays resident across page turns.
  struct PxcSlotGuard {
    int spine;
    int page;
    ~PxcSlotGuard() {
      ImageBlock::releaseRenderCache();
      EpubDiagnostics::record("image_cache_release", spine, page);
    }
  } pxcSlotGuard{diagnosticSpine, diagnosticPage};

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();

  // Decode missing EPUB image caches while the heap is still at its peak.  The
  // font prewarm below can fragment the largest free block enough for JPEGDEC
  // or PNGdec to fail even though the source image is valid.  ImageBlock writes
  // the pixel cache and framebuffer during this pass; clear those pixels before
  // beginning the actual BW/gray render.  Cached images are skipped, so normal
  // page turns retain the fast path.
  if (pageNeedsImageDecode) {
    unsigned long imageCount = 0;
    for (const auto& element : page->elements) {
      if (element->getTag() == TAG_PageImage) ++imageCount;
    }
    EpubDiagnostics::record("image_warmup_start", diagnosticSpine, diagnosticPage, 0, 0, imageCount);
    const unsigned long imageWarmupStart = millis();
    longOperationIndicator.stage("decoding");
    page->warmImageCaches(renderer, orientedMarginLeft, orientedMarginTop);
    renderer.clearScreen();
    EpubDiagnostics::record("image_warmup_end", diagnosticSpine, diagnosticPage, millis() - imageWarmupStart, 0,
                            imageCount);
  }

  // Font prewarm: scan pass accumulates text, then prewarm, then real render.
  // Keep the proven normal-render behavior independent of BLE admission.
  const unsigned long fontScanStart = millis();
  EpubDiagnostics::record("font_scan_start", diagnosticSpine, diagnosticPage);
  auto scope = fcm->createPrewarmScope();
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  scope.setAdmissionHook(&EpubReaderActivity::admitReaderFontPrewarm, this);
#endif
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, guideDots);  // scan pass
  EpubDiagnostics::record("font_scan_end", diagnosticSpine, diagnosticPage, millis() - fontScanStart);
  const unsigned long fontPrewarmStart = millis();
  if (!scope.endScanAndPrewarm()) return false;
  sampleReaderFreeHeap();
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  if (auto* fontCache = renderer.getFontCacheManager()) {
    cachedRenderLargestRequestThisRender =
        std::max(cachedRenderLargestRequestThisRender, static_cast<size_t>(fontCache->largestAllocationRequestBytes()));
  }
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  sampleCachedRenderHeap();
  if (!pageNeedsImageDecode && !cachedRenderFinalPassPreflight(!highlightWords.empty())) return false;
#endif
  EpubDiagnostics::record("font_prewarm_end", diagnosticSpine, diagnosticPage, millis() - fontPrewarmStart);
  const auto tPrewarm = millis();
  if (!highlightWords.empty())
    collectHighlightWords(*page, renderer, fontId, orientedMarginLeft, orientedMarginTop, highlightWords);

  const bool pageHasImages = page->hasImages();
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing;
  const bool needsAnyGrayscale = !intermediatePage && (needsTextGrayscale || pageHasImages);
  const bool tiledGrayscale = needsAnyGrayscale && renderer.supportsStripGrayscale();
  // Whole-plane buffering only pays when the BW refresh genuinely runs async
  // underneath it; on blocking panels (X3) it would just spend ~50 KB for the
  // identical serial timing. Image pages take the blocking double-FAST path
  // below (no async refresh is ever started), so they'd spend the buffers with
  // nothing in flight to overlap.
  const bool overlapRefresh = tiledGrayscale && renderer.supportsAsyncRefresh() && !pageHasImages;
  auto renderGrayscalePass = [&]() {
    if (needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, guideDots);
      if (spineHasClippings) renderSavedHighlights(*page, fontId, highlightWords);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
      // Image-only grayscale passes draw after the normal page/status-bar pass.
      // Redraw the lightweight status bar last so a full-page EPUB image cannot
      // paint over the footer on X3/X4 panels.
      renderStatusBar();
    }
  };

  longOperationIndicator.stage("rendering");
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop, guideDots);
  if (spineHasClippings) renderSavedHighlights(*page, fontId, highlightWords);
  renderStatusBar();
  const auto tBwRender = millis();
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  sampleCachedRenderHeap();
#endif
#if FREEINK_CAP_BLE_HID_HOST
  if (logBleCachedRender) logReaderHeapShape("reader_cached_after_bw_render");
#endif

  longOperationIndicator.stage("refreshing");
  // Piggyback on the reader's normal display refresh; the indicator never
  // schedules an extra e-ink update or animation.
  longOperationIndicator.drawIfDue();
  if (pageHasImages && !intermediatePage) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    page->blankImages(renderer, orientedMarginLeft, orientedMarginTop);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);

    // Restore only the image rectangles. Text and status-bar pixels were not
    // blanked, so avoid another full page/font render here.
    page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    // The image is restored after the first status-bar draw. Keep the footer
    // visible and deterministic without another full-screen refresh.
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    // The image's own page is handled above and doesn't count toward the full
    // refresh cadence. But the grayscale pass below leaves gray charge in the
    // image region that a plain fast diff on the *next* page can't clear, so
    // text there ghosts gray (#2190). Force the next ordinary page onto the
    // HALF ghost-cleanup path, which drives every pixel to its target
    // regardless of residue.
    pagesUntilFullRefresh = 1;
  } else {
    if (intermediatePage) {
      // The BW pass above already includes images. Do not repeat image
      // decoding, grayscale dithering, or AA while another manual turn is
      // waiting. The next render with an empty queue takes the normal path.
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
      if (pageHasImages) pagesUntilFullRefresh = 1;
      return true;
    }
    // Async form: start the waveform and return so the grayscale plane rendering
    // below overlaps the panel's refresh time instead of following it.
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, overlapRefresh);
  }
  const auto tDisplay = millis();
#if FREEINK_CAP_BLE_HID_HOST
  if (logBleCachedRender) logReaderHeapShape("reader_cached_after_display");
#endif

  // Tiled grayscale: render each plane band-by-band, leaving the BW
  // framebuffer intact so no full-frame storeBwBuffer is needed; controller
  // RAM is re-synced from the live framebuffer afterward. The page is
  // re-rendered ceil(H/STRIP_ROWS) times per plane, but renderCharImpl culls
  // out-of-band glyphs before decode so the cost stays close to one render.
  // Both text (drawPixel) and images (DirectPixelWriter) honor the active
  // strip target. When the BW refresh above went out async, the plane
  // rendering below overlaps the panel's refresh time; only the controller
  // RAM writes wait for BUSY.
  if (tiledGrayscale) {
    constexpr int STRIP_ROWS = READER_GRAYSCALE_STRIP_ROWS;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;

    // Render one plane band-by-band into a whole-plane buffer without touching
    // the controller, so it can run while the refresh is still in flight.
    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    // Tiered on heap pressure: two plane buffers hide both plane renders
    // inside the refresh wait; one hides the LSB render (its buffer is reused
    // for MSB after streaming); none falls back to the strip-scratch flow with
    // no overlap. Each buffer is only attempted when it leaves ~60 KB free so
    // the pass never starves concurrent allocations: the next page re-render
    // allocates through throwing std::string paths that abort() on OOM under
    // -fno-exceptions, so a plane buffer that "fits" but eats the render
    // headroom is worse than the strip fallback. Blocking panels skip the
    // buffers entirely (nothing to overlap).
    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    // Free-heap alone ignores fragmentation: taking the largest block for a
    // plane can leave only slivers behind even when total headroom looks fine.
    // Require the block to fit the plane with 16 KB contiguous to spare, which
    // also keeps the advance-table batch scratch viable mid-render (same
    // rationale as BACKGROUND_BUILD_MIN_MAX_ALLOC).
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return ESP.getFreeHeap() >= planeBytes + PLANE_BUF_HEADROOM &&
             ESP.getMaxAllocHeap() >= planeBytes + PLANE_BUF_MAX_ALLOC_RESERVE;
    };
#if NOOIR_EPUB_DIAGNOSTICS
    const bool planeHeapFits = planeBufFits();
    const bool lsbPlaneEligible = overlapRefresh && planeHeapFits;
    // Flags: bit 0 async/headroom overlap policy, bit 1 heap gate, bit 2 image
    // page, bit 3 text AA, bit 4 renderer reports async support. This keeps a
    // policy skip distinct from an eligible nothrow allocation failure.
    const unsigned long planeGateFlags = (overlapRefresh ? 1UL : 0UL) | (planeHeapFits ? 2UL : 0UL) |
                                         (pageHasImages ? 4UL : 0UL) | (needsTextGrayscale ? 8UL : 0UL) |
                                         (renderer.supportsAsyncRefresh() ? 16UL : 0UL);
    EpubDiagnostics::record("grayscale_plane_gate", diagnosticSpine, diagnosticPage, 0, planeBytes,
                            planeGateFlags, 0, lsbPlaneEligible ? 1 : 0);
#endif
    auto lsbPlaneBuf = (overlapRefresh && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
#if NOOIR_EPUB_DIAGNOSTICS
    EpubDiagnostics::record("grayscale_plane_alloc", diagnosticSpine, diagnosticPage, 0, planeBytes, 1, planeBytes,
                            lsbPlaneBuf ? 1 : (lsbPlaneEligible ? -1 : 0));
#endif
#if NOOIR_EPUB_DIAGNOSTICS
    const bool msbPlaneEligible = lsbPlaneBuf && planeBufFits();
#endif
    auto msbPlaneBuf = (lsbPlaneBuf && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
#if NOOIR_EPUB_DIAGNOSTICS
    EpubDiagnostics::record("grayscale_plane_alloc", diagnosticSpine, diagnosticPage, 0, planeBytes, 1, planeBytes,
                            msbPlaneBuf ? 1 : (msbPlaneEligible ? -1 : 0));
#endif
    const bool hadLsbPlane = static_cast<bool>(lsbPlaneBuf);
    const bool hadMsbPlane = static_cast<bool>(msbPlaneBuf);

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();

      renderer.waitRefreshComplete();
      const auto tWait = millis();

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      // BW framebuffer is intact; re-sync controller RAM for the next
      // differential page turn directly from it.
      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1);
    } else {
      // Per-strip scratch tier: blocking panels (X3) and the OOM fallback.
      // The strip writes below need the panel idle, so wait out any pending
      // async refresh first (no-op on blocking panels).
      const size_t scratchBytes = static_cast<size_t>(gwBytes) * STRIP_ROWS;
#if FREEINK_CAP_BLE_HID_HOST
      if (logBleCachedRender) logReaderHeapShape("reader_cached_before_gray_scratch");
#endif
      uint8_t* scratch = renderer.getGrayscaleStripScratch(scratchBytes);
      std::unique_ptr<uint8_t[]> fallbackScratch;
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
      if (!scratch && !BleHid.isRunning()) {
        renderer.reserveGrayscaleStripScratch(scratchBytes);
        scratch = renderer.getGrayscaleStripScratch(scratchBytes);
      }
      if (!scratch && BleHid.isRunning()) {
        // The BW refresh may still be in flight. Finish it before asking the
        // lifecycle manager to stop BLE and retry this same page render.
        renderer.waitRefreshComplete();
        readerDeepCacheRescueEligible = true;
        readerDeepCacheRescueReason = "grayscale_scratch";
        readerRenderWaitingForBleStop = true;
        bleinput::requestLifecycleReevaluation();
        return false;
      }
#else
      if (!scratch) {
        renderer.reserveGrayscaleStripScratch(scratchBytes);
        scratch = renderer.getGrayscaleStripScratch(scratchBytes);
      }
#endif
      if (!scratch) {
        fallbackScratch = makeUniqueNoThrow<uint8_t[]>(scratchBytes);
        scratch = fallbackScratch.get();
      }
      EpubDiagnostics::record("grayscale_scratch_alloc", diagnosticSpine, diagnosticPage, 0, scratchBytes, 1,
                              scratchBytes, scratch ? 1 : 0);
      renderer.waitRefreshComplete();
      if (!scratch) {
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
        if (overlapRefresh) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
      } else {
        // Bands may be streamed in any order: X4 windows each via setRamArea,
        // X3 via PTL.
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch, y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(true, scratch, y, rows);
        }
        const auto tGrayLsb = millis();

        // MSB plane.
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch, y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(false, scratch, y, rows);
        }
        const auto tGrayMsb = millis();

        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        const auto tGrayDisplay = millis();

        // BW framebuffer is intact; re-sync controller RAM for the next
        // differential page turn directly from it.
        renderer.cleanupGrayscaleWithFrameBuffer();
        const auto tCleanup = millis();

        const auto tEnd = millis();
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
                tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0);
      }
      if (scratch) {
        EpubDiagnostics::record("grayscale_scratch_done", diagnosticSpine, diagnosticPage, 0, scratchBytes, 1,
                                scratchBytes, fallbackScratch ? 0 : 1);
      }
#if FREEINK_CAP_BLE_HID_HOST
      if (logBleCachedRender) logReaderHeapShape("reader_cached_after_gray_scratch");
#endif
    }
    EpubDiagnostics::record("grayscale_plane_free", diagnosticSpine, diagnosticPage, 0, planeBytes,
                            (hadLsbPlane ? 1u : 0u) + (hadMsbPlane ? 1u : 0u),
                            planeBytes * ((hadLsbPlane ? 1u : 0u) + (hadMsbPlane ? 1u : 0u)), 1);
  } else {
    // Fallback path for a controller without strip support. grayscale rendering
    // TODO: Only do this if font supports it
    if (needsAnyGrayscale) {
      // Save the BW frame before the grayscale passes overwrite it, restore
      // after. Only needed when grayscale actually renders.
      if (!renderer.storeBwBuffer()) {
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
        const auto tEnd = millis();
        LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
                tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
        return true;
      }
      const auto tBwStore = millis();

      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      const auto tGrayLsb = millis();

      // Render and copy to MSB buffer
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      const auto tGrayMsb = millis();

      // display grayscale part
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
      const auto tBwRestore = millis();

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
              "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
              tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
    } else {
      // No text AA and no images: BW frame already displayed above, no grayscale
      // to render, so no save/restore.
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
              tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
      return true;
    }
  }
#if FREEINK_CAP_BLE_HID_HOST
  if (logBleCachedRender) logReaderHeapShape("reader_cached_after_grayscale");
#endif
  return true;
}

void EpubReaderActivity::renderStatusBar() const {
  // Calculate progress in book. Use the estimated total while a giant spine is still building so
  // "page X of Y" and the progress bar don't read off the small build watermark.
  const int currentPage = section->currentPage + 1;
  const int currentChapterPageCount = section->estimatedTotalPages();
  int displayedPage = currentPage;
  int displayedPageCount = currentChapterPageCount;
  bool displayedPageCountEstimated = section->isBuilding();
  if (stablePagesReady && SETTINGS.pageNumberMode == CrossPointSettings::STABLE_PAGES) {
    displayedPage = static_cast<int>(StablePageCache::pageFor(stablePageIndex, currentSpineIndex,
                                                               section->currentPage, currentChapterPageCount));
    displayedPageCount = static_cast<int>(stablePageIndex.totalPages);
    displayedPageCountEstimated = true;
  }
  const float pageCount = currentChapterPageCount;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  std::string title;

  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
    }

  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  GUI.drawStatusBar(renderer, bookProgress, displayedPage, displayedPageCount, title, 0, textYOffset, true,
                    currentPageBookmarked, displayedPageCountEstimated,
                    stablePagesReady && SETTINGS.pageNumberMode == CrossPointSettings::STABLE_PAGES);
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;
  pendingPageTurns.clear();

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestPageRender();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  pendingPageTurns.clear();
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestPageRender();
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (cachedBookmarks.capacity() < initialBookmarkCacheCapacity) {
    cachedBookmarks.reserve(initialBookmarkCacheCapacity);
  }
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::loadCachedClippings() {
  cachedClippings.clear();
  if (epub) ClipFile::load(epub->getPath(), cachedClippings);
  LOG_DBG("CLP", "Reader loaded %u clipping(s)", static_cast<unsigned>(cachedClippings.size()));
  highlightMatches.clear();
  highlightMatches.reserve(cachedClippings.size());
  highlightMatchPageObject = nullptr;
  highlightMatchSpine = -1;
  highlightMatchPage = -1;
  highlightLookupDone = false;
  allowClippingRelocation = true;
}

void EpubReaderActivity::addBookmark() {
  if (!section || !epub) {
    return;
  }
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock(*this);
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && epub->getBookSize() > 0 && info.totalPages > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(info.totalPages);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}
