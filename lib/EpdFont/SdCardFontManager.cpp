#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

#ifndef NOOIR_INTERFACE_FONT_DIAGNOSTICS
#define NOOIR_INTERFACE_FONT_DIAGNOSTICS 0
#endif

#if NOOIR_INTERFACE_FONT_DIAGNOSTICS
namespace {
uint8_t interfaceFontLoadDiagnosticRecords = 0;
}
#endif

#if NOOIR_SD_FONT_DIAGNOSTICS
#include <Arduino.h>
#endif

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
// Produces a deterministic ID that is stable across load/unload cycles and reboots,
// and changes when font content changes (different header/TOC = different contentHash).
int SdCardFontManager::computeFontId(const uint32_t contentHash, const char* familyName, const uint8_t pointSize) const {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash ^ idSalt_;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is reserved as "not found" sentinel
}

int SdCardFontManager::loadFile(const SdCardFontFileInfo& file, const char* familyName, GfxRenderer& renderer) {
  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", file.path.c_str());
    return 0;
  }

  const unsigned long startedMs = millis();
  if (!font->load(file.path.c_str())) {
    LOG_ERR("SDMGR", "Failed to load %s", file.path.c_str());
    delete font;
    return 0;
  }
#if NOOIR_INTERFACE_FONT_DIAGNOSTICS
  if (interfaceFontLoadDiagnosticRecords < 16) {
    LOG_INF("UIFONT", "load pt=%u ms=%lu styles=%u", file.pointSize, millis() - startedMs, font->styleCount());
    ++interfaceFontLoadDiagnosticRecords;
  }
#endif

  int fontId = computeFontId(font->contentHash(), familyName, file.pointSize);
  // Guard against collision with built-in font IDs (astronomically unlikely
  // with FNV-1a hashes, but provides a safety net)
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, file.path.c_str());
    delete font;
    return 0;
  }
  renderer.registerSdCardFont(fontId, font);
  renderer.registerFontPointSize(fontId, file.pointSize);
  loaded_.push_back({font, fontId, file.pointSize});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", file.path.c_str(), file.pointSize, fontId, font->styleCount());

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  return fontId;
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t fontSizeEnum) {
  // Unload any previously loaded family first
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

  // Select the physical point size closest to the built-in reader sizes. Some
  // CJK font packs only ship larger sizes, so ordinal selection can make
  // MEDIUM load 18pt+ and produce oversized pages on small devices.
  const SdCardFontFileInfo* selected = family.findClosestReaderSize(fontSizeEnum);
  if (!selected) {
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  const int fontId = loadFile(*selected, family.name.c_str(), renderer);
  if (fontId == 0) {
    return false;
  }

  renderer.bindArabicFallbackForPointSize(fontId, selected->pointSize);
  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

bool SdCardFontManager::loadFamilyUiSizes(const SdCardFontFamilyInfo& family, GfxRenderer& renderer) {
  if (!beginFamilyUiSizes(family, renderer)) return false;

  static constexpr uint8_t kUiPointSizes[] = {8, 10, 12};
  for (const uint8_t pointSize : kUiPointSizes) {
    if (!loadFamilyUiSize(family, renderer, pointSize)) {
      unloadAll(renderer, false);
      return false;
    }
  }

  return true;
}

bool SdCardFontManager::beginFamilyUiSizes(const SdCardFontFamilyInfo& family, GfxRenderer& renderer) {
  unloadAll(renderer, false);
  if (!family.hasInterfaceSizes()) return false;
  loadedFamilyName_ = family.name;
  loadedPointSize_ = 0;
  return true;
}

bool SdCardFontManager::loadFamilyUiSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                         const uint8_t pointSize) {
  const SdCardFontFileInfo* file = family.findFile(pointSize);
  return file != nullptr && loadFile(*file, family.name.c_str(), renderer) != 0;
}

int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                           uint8_t pointSize) {
  const SdCardFontFileInfo* file = family.findFile(pointSize);
  if (!file) return 0;  // family has no .cpfont at this exact size

  // Reuse an already-loaded font of the same size (e.g. when a reader size
  // happens to match a UI size) instead of double-loading the file.
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }

  return loadFile(*file, family.name.c_str(), renderer);
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer, const bool clearRendererFallbacks) {
  // Reader and interface-font managers may coexist. Remove only IDs owned by
  // this manager; never clear the renderer-wide SD registry here.
  if (clearRendererFallbacks) renderer.clearFallbackFonts();
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
}

void SdCardFontManager::releaseUiFallbackCaches() {
  // loaded_[0] is the reader-size font selected by loadFamily().  Any later
  // entries were loaded only for size-matched UI fallback.  Keep the selected
  // font's current mini data warm; the UI copies are rebuildable and can be
  // repopulated lazily when the UI is shown again.
  for (size_t i = 1; i < loaded_.size(); ++i) {
    if (loaded_[i].font) loaded_[i].font->releaseDisposableCaches();
  }
}

#if NOOIR_SD_FONT_DIAGNOSTICS
void SdCardFontManager::logMemoryStats(const char* stage) const {
  if (loaded_.empty()) {
    LOG_INF("SDMEM", "stage=%s n=0 free=%u min=%u max=%u", stage ? stage : "?", ESP.getFreeHeap(),
            ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    return;
  }

  for (size_t slot = 0; slot < loaded_.size(); ++slot) {
    const auto& loaded = loaded_[slot];
    if (!loaded.font) continue;
    const auto mem = loaded.font->getMemoryStats();
    LOG_INF("SDMEM",
            "stage=%s slot=%u id=%d pt=%u ci=%u cov=%u ae=%u adv=%u pk=%u pl=%u mi=%u mg=%u mb=%u mk=%u ov=%u/%u tot=%u free=%u min=%u max=%u",
            stage ? stage : "?", static_cast<unsigned>(slot), loaded.fontId, static_cast<unsigned>(loaded.size),
            mem.coverageIntervals, mem.coverageBytes, mem.advanceEntries, mem.advanceBytes,
            mem.persistentKernBytes, mem.persistentLigatureBytes, mem.miniIntervalBytes, mem.miniGlyphBytes,
            mem.miniBitmapBytes, mem.miniKernBytes, mem.overflowGlyphs, mem.overflowBitmapBytes, mem.totalBytes,
            ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  }
}
#endif

int SdCardFontManager::getFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  return loaded_.front().fontId;
}

int SdCardFontManager::getFontIdForPointSize(const uint8_t pointSize) const {
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }
  return 0;
}
