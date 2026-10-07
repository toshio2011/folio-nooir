#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {

static uint8_t fontSizeEnumFromSettings() {
  uint8_t e = SETTINGS.fontSize;
  if (e >= CrossPointSettings::FONT_SIZE_COUNT) e = 1;  // default to MEDIUM
  return e;
}

// Built-in UI fonts and their physical point sizes (at 150 DPI, matching the
// SD-font converter). Each is paired with a same-size SD fallback so CJK UI
// text matches the surrounding Latin. See SdCardFontSystem::setupUiFallbacks.
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};
constexpr UiFontSize kUiFontSizes[] = {
    {SMALL_FONT_ID, 8},
    {UI_10_FONT_ID, 10},
    {UI_12_FONT_ID, 12},
};

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.discover();

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      if (manager_.loadFamily(*family, renderer, fontSizeEnumFromSettings())) {
        setupUiFallbacks(renderer);
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.sdFontFamilyName[0] = '\0';
        SETTINGS.saveToFile();
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
  }

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    registry_.discover();
    uiReloadRequested_.store(true, std::memory_order_release);
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t sizeEnum = fontSizeEnumFromSettings();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
      return;
    }
    const auto* selected = family->findClosestReaderSize(sizeEnum);
    const uint8_t wantedPt = selected ? selected->pointSize : 0;
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (enum %u)%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            sizeEnum, registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, sizeEnum)) {
      setupUiFallbacks(renderer);
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.saveToFile();
  }
}

void SdCardFontSystem::ensureUiLoaded(GfxRenderer& renderer) {
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "UI registry dirty — re-discovering fonts");
    registry_.discover();
  }
  const bool registryChanged = registryWasDirty || uiReloadRequested_.exchange(false, std::memory_order_acquire);

  const char* wantedFamily = SETTINGS.uiFontFamilyName;
  const std::string currentFamily = uiManager_.currentFamilyName();

  if (uiLoadInProgress_ && !registryChanged && uiLoadingFamily_ == wantedFamily) return;
  if (uiLoadInProgress_) {
    renderer.clearUiFontOverrides();
    uiManager_.unloadAll(renderer, false);
    uiLoadInProgress_ = false;
    uiLoadingFamily_.clear();
    uiLoadingStep_ = 0;
  }

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      renderer.clearUiFontOverrides();
      uiManager_.unloadAll(renderer, false);
    }
    uiAttemptedFamily_.clear();
    return;
  }

  if (!registryChanged && currentFamily == wantedFamily && !currentFamily.empty()) return;
  if (!registryChanged && uiAttemptedFamily_ == wantedFamily) return;

  renderer.clearUiFontOverrides();
  // beginFamilyUiSizes() owns the UI-manager teardown.  Keep the renderer
  // mappings cleared before it runs, but avoid unloading the same manager twice.
  uiAttemptedFamily_ = wantedFamily;

  if (wantedFamily[0] == '\0') return;

  const auto* family = registry_.findFamily(wantedFamily);
  if (!family || !family->hasInterfaceSizes()) {
    LOG_DBG("SDFS", "Interface font unavailable: %s (using built-in UI)", wantedFamily);
    return;
  }

  if (!uiManager_.beginFamilyUiSizes(*family, renderer)) {
    LOG_ERR("SDFS", "Failed to start interface font: %s (using built-in UI)", wantedFamily);
    return;
  }

  uiLoadingFamily_ = wantedFamily;
  uiLoadingStep_ = 0;
  uiLoadInProgress_ = true;
  LOG_DBG("SDFS", "Starting interface font load: %s", wantedFamily);
}

bool SdCardFontSystem::progressUiLoad(GfxRenderer& renderer) {
  if (!uiLoadInProgress_) return false;

  const auto* family = registry_.findFamily(uiLoadingFamily_);
  static constexpr uint8_t kUiPointSizes[] = {8, 10, 12};
  if (!family || uiLoadingStep_ >= sizeof(kUiPointSizes) / sizeof(kUiPointSizes[0]) ||
      !uiManager_.loadFamilyUiSize(*family, renderer, kUiPointSizes[uiLoadingStep_])) {
    renderer.clearUiFontOverrides();
    uiManager_.unloadAll(renderer, false);
    LOG_ERR("SDFS", "Interface font step failed: %s (using built-in UI)", uiLoadingFamily_.c_str());
    uiAttemptedFamily_ = uiLoadingFamily_;
    uiLoadingFamily_.clear();
    uiLoadingStep_ = 0;
    uiLoadInProgress_ = false;
    return true;
  }
  ++uiLoadingStep_;
  if (uiLoadingStep_ < sizeof(kUiPointSizes) / sizeof(kUiPointSizes[0])) return false;

  const int ui8 = uiManager_.getFontIdForPointSize(8);
  const int ui10 = uiManager_.getFontIdForPointSize(10);
  const int ui12 = uiManager_.getFontIdForPointSize(12);
  if (ui8 == 0 || ui10 == 0 || ui12 == 0) {
    renderer.clearUiFontOverrides();
    uiManager_.unloadAll(renderer, false);
    LOG_ERR("SDFS", "Interface font sizes incomplete: %s (using built-in UI)", uiLoadingFamily_.c_str());
    uiAttemptedFamily_ = uiLoadingFamily_;
    uiLoadingFamily_.clear();
    uiLoadingStep_ = 0;
    uiLoadInProgress_ = false;
    return true;
  }

  // Do not activate a family that cannot render the basic Latin UI alphabet.
  // Arabic and other script gaps still use the existing built-in fallback
  // mapping below; a family with no common UI glyphs is simply incompatible.
  const int uiIds[] = {ui8, ui10, ui12};
  static constexpr uint32_t kUiCoverageProbes[] = {' ', '0', 'A', 'a'};
  for (const int id : uiIds) {
    const auto fontIt = renderer.getFontMap().find(id);
    if (fontIt == renderer.getFontMap().end()) {
      renderer.clearUiFontOverrides();
      uiManager_.unloadAll(renderer, false);
      LOG_DBG("SDFS", "Interface font registration incomplete: %s (using built-in UI)",
              uiLoadingFamily_.c_str());
      uiAttemptedFamily_ = uiLoadingFamily_;
      uiLoadingFamily_.clear();
      uiLoadingStep_ = 0;
      uiLoadInProgress_ = false;
      return true;
    }
    for (const uint32_t cp : kUiCoverageProbes) {
      if (!fontIt->second.hasCodepoint(cp)) {
        renderer.clearUiFontOverrides();
        uiManager_.unloadAll(renderer, false);
        LOG_DBG("SDFS", "Interface font lacks basic UI coverage: %s (using built-in UI)",
                uiLoadingFamily_.c_str());
        uiAttemptedFamily_ = uiLoadingFamily_;
        uiLoadingFamily_.clear();
        uiLoadingStep_ = 0;
        uiLoadInProgress_ = false;
        return true;
      }
    }
  }

  // Built-in UI IDs are the Arabic fallback, so a custom family that lacks a
  // UI Arabic codepoint cannot break localized navigation or menus.
  renderer.setUiFontOverride(SMALL_FONT_ID, ui8, SMALL_FONT_ID);
  renderer.setUiFontOverride(UI_10_FONT_ID, ui10, UI_10_FONT_ID);
  renderer.setUiFontOverride(UI_12_FONT_ID, ui12, UI_12_FONT_ID);
  LOG_DBG("SDFS", "Loaded interface font family: %s", uiLoadingFamily_.c_str());
  uiAttemptedFamily_ = uiLoadingFamily_;
  uiLoadingFamily_.clear();
  uiLoadingStep_ = 0;
  uiLoadInProgress_ = false;
  return true;
}

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;  // no SD family loaded — nothing to fall back to

  const auto* family = registry_.findFamily(familyName);
  if (!family) return;

  // Probe the already-loaded reader-size font before paying for the UI sizes:
  // resolveTextFontId only redirects on CJK codepoints, so a Latin-only family
  // can never act as a fallback and its UI sizes would be dead weight in RAM.
  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;
  // One representative codepoint per script: Han, Hiragana, Katakana, Hangul.
  static constexpr uint32_t kCjkProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
  bool hasCjk = false;
  for (const uint32_t cp : kCjkProbes) {
    if (readerIt->second.hasCodepoint(cp)) {
      hasCjk = true;
      break;
    }
  }
  if (!hasCjk) {
    LOG_DBG("SDFS", "%s has no CJK coverage - skipping UI fallback sizes", familyName.c_str());
    return;
  }

  for (const auto& ui : kUiFontSizes) {
    const int sdFontId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (sdFontId != 0) {
      renderer.setFallbackFont(ui.fontId, sdFontId);
    } else {
      LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", ui.pointSize, familyName.c_str());
    }
  }
}

void SdCardFontSystem::releaseUiFallbackCaches() {
#if NOOIR_SD_FONT_DIAGNOSTICS
  diagnosticReaderSession_ = true;
  diagnosticReaderFirstRender_ = false;
  diagnosticHomeReturn_ = false;
  manager_.logMemoryStats("reader_before_ui_release");
#endif

  manager_.releaseUiFallbackCaches();

#if NOOIR_SD_FONT_DIAGNOSTICS
  manager_.logMemoryStats("reader_after_ui_release");
#endif
}

#if NOOIR_SD_FONT_DIAGNOSTICS
void SdCardFontSystem::diagnosticCheckpoint(const char* stage) {
  if (!stage) return;

  if (strcmp(stage, "home_rendered") == 0) {
    if (!diagnosticHomeRendered_) {
      manager_.logMemoryStats(stage);
      diagnosticHomeRendered_ = true;
    }
    if (diagnosticReaderSession_ && !diagnosticHomeReturn_) {
      manager_.logMemoryStats("home_return");
      diagnosticHomeReturn_ = true;
      diagnosticReaderSession_ = false;
    }
    return;
  }

  if (strcmp(stage, "library_rendered") == 0) {
    if (!diagnosticLibraryRendered_) {
      manager_.logMemoryStats(stage);
      diagnosticLibraryRendered_ = true;
    }
    return;
  }

  if (strcmp(stage, "reader_first_render") == 0) {
    if (!diagnosticReaderFirstRender_) {
      manager_.logMemoryStats(stage);
      diagnosticReaderFirstRender_ = true;
    }
    return;
  }

  manager_.logMemoryStats(stage);
}
#endif

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}
