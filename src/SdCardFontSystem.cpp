#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "InterfaceFontSelection.h"
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
struct UiFontFallbackSize {
  int fontId;
  uint8_t pointSize;
};
constexpr UiFontFallbackSize kUiFontFallbackSizes[] = {
    {SMALL_FONT_ID, 8},
    {UI_10_FONT_ID, 10},
    {UI_12_FONT_ID, 12},
};

struct InterfaceFontRole {
  int fontId;
  uint8_t pointSize;
  uint8_t styleMask;
};
constexpr InterfaceFontRole kInterfaceFontRoles[] = {
    {SMALL_FONT_ID, 8, InterfaceFontSelection::styleMaskForPointSize(8)},
    {UI_10_FONT_ID, 10, InterfaceFontSelection::styleMaskForPointSize(10)},
    {UI_12_FONT_ID, 12, InterfaceFontSelection::styleMaskForPointSize(12)},
};

const char* interfaceRoleName(const int fontId) {
  for (const auto& role : kInterfaceFontRoles) {
    if (role.fontId == fontId) {
      switch (role.pointSize) {
        case 8:
          return "small_8pt";
        case 10:
          return "ui_10pt";
        case 12:
          return "ui_12pt";
      }
    }
  }
  return "unknown";
}

void logBuiltInUiRoles(const char* family, const char* reason) {
  for (const auto& role : kInterfaceFontRoles) {
    LOG_INF("UIFONT", "role=%s pt=%u family=%s source=built_in_fallback custom_mask=0x00 fallback_mask=0x%02X reason=%s",
            interfaceRoleName(role.fontId), static_cast<unsigned>(role.pointSize), family ? family : "Default",
            static_cast<unsigned>(role.styleMask), reason ? reason : "default selected");
  }
}

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  renderer_ = &renderer;
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
  const bool familySelected = SETTINGS.uiFontFamilyName[0] != '\0';
  if (!InterfaceFontSelection::mayApplyCustomUiFont(
          InterfaceFontSelection::kCustomUiFontsEnabledForCurrentRelease, familySelected)) {
    const bool customAssetsResident = !uiManager_.currentFamilyName().empty() || uiLoadInProgress_;
    if (customAssetsResident) {
      renderer.clearUiFontOverrides();
      uiManager_.unloadAll(renderer, false);
      renderer.clearUiTextPreparation();
      uiLoadInProgress_ = false;
      uiLoadingFamily_.clear();
      uiLoadingStep_ = 0;
    }
    if (!uiAttemptedFamily_.empty()) uiAttemptedFamily_.clear();
    if (!familySelected) {
      return;
    }

    if (!uiDeferredFallbackLogged_) {
      LOG_INF("UIFONT", "saved_family=%s state=deferred selection_preserved=1 source=built_in_fallback reason=custom UI fonts disabled for 1.6.6 X4 validation",
              SETTINGS.uiFontFamilyName);
      logBuiltInUiRoles(SETTINGS.uiFontFamilyName, "custom UI fonts deferred for physical X4 timing");
      uiDeferredFallbackLogged_ = true;
    }
    return;
  }

  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "UI registry dirty — re-discovering fonts");
    registry_.discover();
  }
  const bool registryChanged = registryWasDirty || uiReloadRequested_.exchange(false, std::memory_order_acquire);

  const std::string wantedFamily = SETTINGS.uiFontFamilyName;
  const std::string currentFamily = uiManager_.currentFamilyName();

  if (uiLoadInProgress_ && !registryChanged && uiLoadingFamily_ == wantedFamily) return;
  if (uiLoadInProgress_) {
    renderer.clearUiFontOverrides();
    uiManager_.unloadAll(renderer, false);
    uiLoadInProgress_ = false;
    uiLoadingFamily_.clear();
    uiLoadingStep_ = 0;
  }

  if (wantedFamily.empty()) {
    if (!currentFamily.empty()) {
      renderer.clearUiFontOverrides();
      uiManager_.unloadAll(renderer, false);
      logBuiltInUiRoles("Default", "default selected");
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

  const auto* family = registry_.findFamily(wantedFamily);
  if (!InterfaceFontSelection::isCompatibleFamily(family, sizeof(SETTINGS.uiFontFamilyName))) {
    LOG_INF("UIFONT", "select family=%s source=built_in_fallback reason=family missing or incomplete", wantedFamily.c_str());
    logBuiltInUiRoles(wantedFamily.c_str(), "family missing or incomplete");
    uiManager_.unloadAll(renderer, false);
    if (strcmp(SETTINGS.uiFontFamilyName, wantedFamily.c_str()) == 0) {
      SETTINGS.uiFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
    uiAttemptedFamily_.clear();
    return;
  }

  LOG_INF("UIFONT", "select family=%s registry_index=%d persisted=1 compatible=1", wantedFamily.c_str(),
          registry_.getFamilyIndex(wantedFamily));
  for (const auto& role : kInterfaceFontRoles) {
    const auto* file = family->findFile(role.pointSize);
    LOG_INF("UIFONT", "path role=%s pt=%u path=%s", interfaceRoleName(role.fontId),
            static_cast<unsigned>(role.pointSize), file ? file->path.c_str() : "missing");
  }

  if (!uiManager_.beginFamilyUiSizes(*family, renderer)) {
    LOG_ERR("UIFONT", "select family=%s source=built_in_fallback reason=Mini Pack start failed",
            wantedFamily.c_str());
    logBuiltInUiRoles(wantedFamily.c_str(), "Mini Pack start failed");
    if (strcmp(SETTINGS.uiFontFamilyName, wantedFamily.c_str()) == 0) {
      SETTINGS.uiFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
    uiAttemptedFamily_.clear();
    return;
  }

  uiLoadingFamily_ = wantedFamily;
  uiLoadingStep_ = 0;
  uiLoadInProgress_ = true;
  LOG_INF("UIFONT", "family=%s Mini Pack load started; processing 8/10/12 pt roles in order",
          wantedFamily.c_str());
}

bool SdCardFontSystem::progressUiLoad(GfxRenderer& renderer) {
  if (!uiLoadInProgress_) return false;

  const auto fallbackToBuiltIn = [this, &renderer](const char* reason) {
    renderer.clearUiFontOverrides();
    uiManager_.unloadAll(renderer, false);
    if (strcmp(SETTINGS.uiFontFamilyName, uiLoadingFamily_.c_str()) == 0) {
      SETTINGS.uiFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
    LOG_ERR("UIFONT", "family=%s source=built_in_fallback reason=%s", uiLoadingFamily_.c_str(), reason);
    logBuiltInUiRoles(uiLoadingFamily_.c_str(), reason);
    uiAttemptedFamily_.clear();
    uiLoadingFamily_.clear();
    uiLoadingStep_ = 0;
    uiLoadInProgress_ = false;
    return true;
  };

  const auto* family = registry_.findFamily(uiLoadingFamily_);
  if (!family || uiLoadingStep_ >= sizeof(kInterfaceFontRoles) / sizeof(kInterfaceFontRoles[0]) ||
      !uiManager_.loadFamilyUiSize(*family, renderer, kInterfaceFontRoles[uiLoadingStep_].pointSize,
                                   kInterfaceFontRoles[uiLoadingStep_].styleMask)) {
    return fallbackToBuiltIn("size load failed");
  }
  ++uiLoadingStep_;
  if (uiLoadingStep_ < sizeof(kInterfaceFontRoles) / sizeof(kInterfaceFontRoles[0])) return false;

  // Validate only custom faces that this compact pack actually loaded. A
  // missing Regular/Bold role stays mapped to its matching built-in face.
  static constexpr uint32_t kUiCoverageProbes[] = {' ', '0', 'A', 'a'};
  bool hasCustomRole = false;
  for (const auto& role : kInterfaceFontRoles) {
    const int id = uiManager_.getFontIdForPointSize(role.pointSize);
    if (id == 0) {
      LOG_INF("UIFONT", "role=%s pt=%u family=%s source=built_in_fallback custom_mask=0x00 fallback_mask=0x%02X font_id=0 reason=no requested face loaded",
              interfaceRoleName(role.fontId), static_cast<unsigned>(role.pointSize), uiLoadingFamily_.c_str(),
              static_cast<unsigned>(role.styleMask));
      continue;
    }
    const auto fontIt = renderer.getFontMap().find(id);
    if (fontIt == renderer.getFontMap().end()) {
      return fallbackToBuiltIn("font registration incomplete");
    }
    const auto fallbackIt = renderer.getFontMap().find(role.fontId);
    if (fallbackIt == renderer.getFontMap().end()) {
      return fallbackToBuiltIn("built-in UI role missing");
    }
    const uint8_t loadedMask = uiManager_.getLoadedStyleMaskForPointSize(role.pointSize);
    hasCustomRole = hasCustomRole || (loadedMask & role.styleMask) != 0;
    for (uint8_t style = 0; style < 4; ++style) {
      if ((loadedMask & role.styleMask & (1u << style)) == 0) continue;
      for (const uint32_t cp : kUiCoverageProbes) {
        if (!fontIt->second.hasCodepoint(cp, static_cast<EpdFontFamily::Style>(style))) {
          return fallbackToBuiltIn("basic UI glyph coverage missing");
        }
      }
    }

    // Join selected SD faces and built-in fallbacks by style. This preserves,
    // for example, the built-in 8 pt Bold while using only the custom 8 pt
    // Regular role.
    const EpdFontFamily miniPack =
        EpdFontFamily::withStyleFallbacks(fontIt->second, fallbackIt->second, loadedMask & role.styleMask);
    if (!renderer.replaceFontFamily(id, miniPack)) {
      return fallbackToBuiltIn("font family replacement failed");
    }
    renderer.setUiFontOverride(role.fontId, id, role.fontId, loadedMask & role.styleMask);
    const uint8_t customMask = loadedMask & role.styleMask;
    const uint8_t fallbackMask = role.styleMask & static_cast<uint8_t>(~customMask);
    const char* source = customMask == 0 ? "built_in_fallback" :
                         fallbackMask == 0 ? "custom" : "mixed_custom_and_built_in";
    LOG_INF("UIFONT", "role=%s pt=%u family=%s source=%s custom_mask=0x%02X fallback_mask=0x%02X font_id=%d",
            interfaceRoleName(role.fontId), static_cast<unsigned>(role.pointSize), uiLoadingFamily_.c_str(), source,
            static_cast<unsigned>(customMask), static_cast<unsigned>(fallbackMask), id);
  }

  if (!hasCustomRole) {
    return fallbackToBuiltIn("no usable UI roles");
  }

  // Built-in UI IDs remain the Arabic fallback, so missing Arabic glyphs do
  // not break localized navigation or menus.
  LOG_INF("UIFONT", "family=%s Mini Pack load complete; custom UI role mappings active",
          uiLoadingFamily_.c_str());
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

  for (const auto& ui : kUiFontFallbackSizes) {
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

void SdCardFontSystem::releaseReaderCaches() {
#if NOOIR_SD_FONT_DIAGNOSTICS
  manager_.logMemoryStats("reader_before_shared_release");
#endif
  manager_.releaseReaderCaches();
#if NOOIR_SD_FONT_DIAGNOSTICS
  manager_.logMemoryStats("reader_after_shared_release");
#endif
}

void SdCardFontSystem::releaseInterfaceFontCaches() {
#if NOOIR_SD_FONT_DIAGNOSTICS
  uiManager_.logMemoryStats("before_interface_font_unload");
#endif
  if (SETTINGS.uiFontFamilyName[0] != '\0' &&
      (!uiManager_.currentFamilyName().empty() || uiLoadInProgress_)) {
    LOG_INF("UIFONT", "unload family=%s selection_preserved=1 reason=heavy activity boundary",
            SETTINGS.uiFontFamilyName);
    logBuiltInUiRoles(SETTINGS.uiFontFamilyName, "custom assets unloaded at heavy boundary");
  }
  // Remove renderer overrides before deleting the SD-owned font objects.
  // Keep the persisted family choice so ensureUiLoaded() can restore it after
  // the heavy activity ends.
  if (renderer_) {
    renderer_->clearUiFontOverrides();
    uiManager_.unloadAll(*renderer_, false);
    renderer_->clearUiTextPreparation();
  }
  uiLoadInProgress_ = false;
  uiLoadingFamily_.clear();
  uiLoadingStep_ = 0;
  uiAttemptedFamily_.clear();
#if NOOIR_SD_FONT_DIAGNOSTICS
  uiManager_.logMemoryStats("after_interface_font_unload");
#endif
}

void SdCardFontSystem::releaseInterfaceFontGlyphCaches() {
#if NOOIR_SD_FONT_DIAGNOSTICS
  uiManager_.logMemoryStats("pressure_before_interface_cache_release");
#endif
  uiManager_.releaseReaderCaches();
  // The renderer's bounded key cache only records which UI labels were
  // prepared. Drop those keys with the backing font arenas so the next UI
  // draw can repopulate the selected custom roles lazily.
  if (renderer_) renderer_->clearUiTextPreparation();
#if NOOIR_SD_FONT_DIAGNOSTICS
  uiManager_.logMemoryStats("pressure_after_interface_cache_release");
#endif
}

void SdCardFontSystem::releaseReaderFontOwner(GfxRenderer& renderer) {
  manager_.releaseReaderFontOwner(renderer);
}

void SdCardFontSystem::signalReaderFontLifecycleBoundary() { manager_.beginAllocationRetryBoundary(); }

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
