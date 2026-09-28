#pragma once

#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>
#include <string>

#ifndef NOOIR_SD_FONT_DIAGNOSTICS
#define NOOIR_SD_FONT_DIAGNOSTICS 0
#endif

class GfxRenderer;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Discover SD card fonts and load user's saved selection. Call once during setup.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);

  /// Lazily load the separately selected interface font. Boot, recovery,
  /// sleep, and update activities deliberately do not call this method.
  void ensureUiLoaded(GfxRenderer& renderer);

  /// Advance one bounded interface-font load step. Returns true only when a
  /// load attempt finished and the caller should repaint.
  bool progressUiLoad(GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + fontSize enum.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t fontSizeEnum) const;

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registryDirty_.store(true, std::memory_order_release); }

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() {
    if (registryDirty_.exchange(false, std::memory_order_acquire)) {
      registry_.discover();
      uiReloadRequested_.store(true, std::memory_order_release);
    }
  }

  /// Drop only disposable state belonging to size-matched UI fallback fonts.
  /// The selected reader-size font remains loaded and its current page cache
  /// is left warm. Coverage, metadata, and advance state are never dropped.
  void releaseUiFallbackCaches();

#if NOOIR_SD_FONT_DIAGNOSTICS
  /// Emit one bounded SDMEM checkpoint for the diagnostic firmware. Repeated
  /// render-task calls are coalesced for lifecycle stages.
  void diagnosticCheckpoint(const char* stage);
#endif

 private:
  // Load the active SD family at the built-in UI point sizes and register each
  // as a size-matched CJK fallback for the corresponding UI font, so CJK book
  // titles/list rows render at the same size as the surrounding Latin UI text.
  // No-op when no SD family is loaded. Safe to call repeatedly (sizes already
  // loaded are reused).
  void setupUiFallbacks(GfxRenderer& renderer);

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  SdCardFontManager uiManager_{0x554946u};  // distinct IDs from the reader manager
  std::atomic<bool> registryDirty_{false};
  std::atomic<bool> uiReloadRequested_{false};
  std::string uiAttemptedFamily_;
  std::string uiLoadingFamily_;
  uint8_t uiLoadingStep_ = 0;
  bool uiLoadInProgress_ = false;

#if NOOIR_SD_FONT_DIAGNOSTICS
  bool diagnosticHomeRendered_ = false;
  bool diagnosticLibraryRendered_ = false;
  bool diagnosticReaderFirstRender_ = false;
  bool diagnosticReaderSession_ = false;
  bool diagnosticHomeReturn_ = false;
#endif
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
