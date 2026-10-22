#pragma once
#include <Epub.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"
#include "activities/Activity.h"

#ifndef NOOIR_KOSYNC_FONT_DIAGNOSTICS
#define NOOIR_KOSYNC_FONT_DIAGNOSTICS 0
#endif

/**
 * Activity for syncing reading progress with KOReader sync server.
 *
 * Flow:
 * 1. Connect to WiFi (if not connected)
 * 2. Calculate document hash
 * 3. Fetch remote progress
 * 4. Show comparison and options (Apply/Upload)
 * 5. Apply or upload progress
 */
class KOReaderSyncActivity final : public Activity {
 public:
  explicit KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::string& documentPath,
                                int currentSpineIndex, int currentPage, int totalPagesInSpine,
                                SavedProgressPosition localKoPos, std::string localChapterName,
                                std::optional<uint16_t> currentParagraphIndex = std::nullopt)
      : Activity("KOReaderSync", renderer, mappedInput),
        documentPath(documentPath),
        currentSpineIndex(currentSpineIndex),
        currentPage(currentPage),
        totalPagesInSpine(totalPagesInSpine),
        currentParagraphIndex(currentParagraphIndex),
        localChapterName(std::move(localChapterName)),
        remoteProgress{},
        remotePosition{},
        localProgress(std::move(localKoPos)) {}

  // Page-based pre-rendered documents (XTC/XTCH) use KOReader's standard
  // paged-document progress representation. The sync activity owns no Xtc
  // object; it only needs the source/cache paths and bounded page counts.
  KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& documentPath,
                       const std::string& progressCachePath, const uint32_t currentPage0,
                       const uint32_t totalPageCount)
      : Activity("KOReaderSync", renderer, mappedInput),
        documentPath(documentPath),
        currentSpineIndex(0),
        currentPage(totalPageCount == 0 || totalPageCount > static_cast<uint32_t>(std::numeric_limits<int>::max())
                         ? 0
                         : static_cast<int>(currentPage0 < totalPageCount ? currentPage0 : totalPageCount - 1)),
        totalPagesInSpine(totalPageCount <= static_cast<uint32_t>(std::numeric_limits<int>::max())
                              ? static_cast<int>(totalPageCount)
                              : 0),
        remoteProgress{},
        remotePosition{},
        localProgress(pageProgress(currentPage0, totalPageCount)),
        pageBasedDocument(true),
        progressCachePath(progressCachePath) {}

  // KOReader's paged-document contract is a 1-based numeric page string and
  // a page/total-page fraction. XTC stores and exposes zero-based page indices.
  static SavedProgressPosition pageProgress(const uint32_t currentPage0, const uint32_t totalPageCount) {
    if (totalPageCount == 0) return {"", 0.0f};
    const uint32_t page1 = (currentPage0 < totalPageCount ? currentPage0 : totalPageCount - 1) + 1;
    return {std::to_string(page1), static_cast<float>(page1) / static_cast<float>(totalPageCount)};
  }

  // Reject XPath values, zero, overflow, and out-of-book page numbers. A
  // same-content binary ID makes total page count portable for XTC/XTCH.
  static std::optional<uint32_t> pageFromProgress(const std::string& progress, const uint32_t totalPageCount) {
    if (progress.empty() || totalPageCount == 0) return std::nullopt;
    uint32_t page1 = 0;
    for (const char character : progress) {
      if (character < '0' || character > '9') return std::nullopt;
      const uint32_t digit = static_cast<uint32_t>(character - '0');
      if (page1 > (std::numeric_limits<uint32_t>::max() - digit) / 10) return std::nullopt;
      page1 = page1 * 10 + digit;
    }
    if (page1 == 0 || page1 > totalPageCount) return std::nullopt;
    return page1 - 1;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING || state == UPLOADING; }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    SYNC_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
    NO_CREDENTIALS
  };

  std::shared_ptr<Epub> epub;  // null until lazy-loaded after TLS in performSync()
  std::string documentPath;
  std::string localChapterName;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;
  std::optional<uint16_t> currentParagraphIndex;

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string documentHash;
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  uint8_t diagnosticDocumentMethod = 0;
#endif

  // Remote progress data
  bool hasRemoteProgress = false;
  KOReaderProgress remoteProgress;
  CrossPointPosition remotePosition;

  // Local progress as KOReader format (pre-computed before Epub was released)
  SavedProgressPosition localProgress;
  bool pageBasedDocument = false;
  std::string progressCachePath;

  // Selection in result screen (0=Apply, 1=Upload)
  int selectedOption = 0;

  // Timed return for successful smart-sync terminal states.
  unsigned long autoReturnAt = 0;
  static constexpr unsigned long AUTO_RETURN_DELAY_MS = 1200;

  // Tracks whether this session activated WiFi. Set in onEnter past the credentials
  // check; checked in onExit to decide whether to silent-reboot. Can't rely on
  // WiFi.getMode() because performUpload() calls esp_wifi_stop() on the way out,
  // which makes WiFi.getMode() return WIFI_MODE_NULL.
  bool wifiActivated = false;

  void onWifiSelectionComplete(bool success);
  void performSync();
  void performUpload();
  bool smartSyncEnabled() const;
  void markAutoReturn();
  void completeAlreadySynced();
  void ensureEpubLoaded();
  std::string calculateDocumentId() const;
  void saveProgressAndReturn(int spineIndex, int page);
  void returnToReader();
};
