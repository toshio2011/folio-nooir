#pragma once

#include <EpdFontFamily.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class FontDecompressor;
class GfxRenderer;
class SdCardFont;

class FontCacheManager {
 public:
  static constexpr uint8_t MAX_SCAN_FONTS = 2;
  static constexpr size_t MAX_SCAN_TEXT_BYTES = 4096;
  using PrewarmAdmissionHook = bool (*)(void* context, size_t requiredFreeBytes, size_t requiredLargestBlock);
  FontCacheManager(const std::map<int, EpdFontFamily>& fontMap, const std::map<int, SdCardFont*>& sdCardFonts,
                   GfxRenderer* renderer = nullptr);

  void setFontDecompressor(FontDecompressor* d);

  void clearCache();
  // Release only the built-in compressed-font page/hot buffers. The next UI
  // draw rebuilds these buffers lazily; SD-font ownership is unchanged.
  void releaseTransientFontCaches();
  // Drop disposable SD-font glyph/mini-cache state while retaining the
  // persistent coverage and advance metadata used by Nooir's layout path.
  void releaseSdFontCaches();
  // Runtime pressure only changes rebuildable per-page font caches. Both
  // Arabic/primary scan slots remain available at every tier.
  void setReaderCacheBudget(uint16_t pageGlyphLimit, size_t scanTextBytesPerFont);
  // Free retained scan-string capacity after a completed prewarm at a
  // quiescent Reader boundary. This preserves the current pressure budget.
  size_t releaseReaderScanStorage();
  // Restore the process-wide prewarm limits and free scan strings retained by
  // the Reader's last prewarm scope. Call at the Reader exit boundary.
  size_t releaseReaderPressureState();
  uint32_t largestAllocationRequestBytes() const;
  uint32_t failedAllocationCount() const;
  void prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask = 0x0F);
  void logStats(const char* label = "render");
  void resetStats();

  // Scan-mode API: called by GfxRenderer::drawText() during scan pass
  bool isScanning() const;
  void recordText(const char* text, int fontId, EpdFontFamily::Style style);
  void recordCodepoint(uint32_t cp, int fontId, EpdFontFamily::Style style);

  // The FontDecompressor pointer, needed by GfxRenderer::getGlyphBitmap()
  FontDecompressor* getDecompressor() const { return fontDecompressor_; }

  // RAII scope for two-pass prewarm pattern
  class PrewarmScope {
   public:
    explicit PrewarmScope(FontCacheManager& manager);
    ~PrewarmScope();
    bool endScanAndPrewarm();
    void setAdmissionHook(PrewarmAdmissionHook hook, void* context);
    PrewarmScope(PrewarmScope&& other) noexcept;
    PrewarmScope& operator=(PrewarmScope&&) = delete;
    PrewarmScope(const PrewarmScope&) = delete;
    PrewarmScope& operator=(const PrewarmScope&) = delete;

   private:
    FontCacheManager* manager_;
    bool active_ = true;
  };
  PrewarmScope createPrewarmScope();

 private:
  const std::map<int, EpdFontFamily>& fontMap_;
  const std::map<int, SdCardFont*>& sdCardFonts_;
  GfxRenderer* renderer_ = nullptr;
  FontDecompressor* fontDecompressor_ = nullptr;

  enum class ScanMode : uint8_t { None, Scanning };
  ScanMode scanMode_ = ScanMode::None;
  struct ScanEntry {
    std::string text;
    uint32_t styleCounts[4] = {};
    int fontId = -1;
  };
  ScanEntry scanEntries_[MAX_SCAN_FONTS];
  uint8_t scanEntryCount_ = 0;
  uint8_t scanFontLimit_ = MAX_SCAN_FONTS;
  size_t scanTextByteLimit_ = MAX_SCAN_TEXT_BYTES;
  PrewarmAdmissionHook admissionHook_ = nullptr;
  void* admissionContext_ = nullptr;

  ScanEntry* findOrCreateScanEntry(int fontId);
  bool prewarmCacheForScope(int fontId, const char* utf8Text, uint8_t styleMask);
  void clearAdmissionHook();
};
