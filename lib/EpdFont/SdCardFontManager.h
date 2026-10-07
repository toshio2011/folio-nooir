#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef NOOIR_SD_FONT_DIAGNOSTICS
#define NOOIR_SD_FONT_DIAGNOSTICS 0
#endif

class GfxRenderer;
class SdCardFont;
struct SdCardFontFamilyInfo;
struct SdCardFontFileInfo;

class SdCardFontManager {
 public:
  SdCardFontManager() = default;
  explicit SdCardFontManager(uint32_t idSalt) : idSalt_(idSalt) {}
  ~SdCardFontManager();
  SdCardFontManager(const SdCardFontManager&) = delete;
  SdCardFontManager& operator=(const SdCardFontManager&) = delete;

  // Load the font file whose physical point size is closest to the reader
  // fontSizeEnum (SMALL=12, MEDIUM=14, LARGE=16, EXTRA_LARGE=18). Only one
  // .cpfont file is loaded; other sizes remain on disk. This keeps resident
  // interval + kern/ligature tables to one size's worth of memory.
  // Returns true on success.
  bool loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t fontSizeEnum);

  // Load exact 8/10/12 pt files for a separate interface-font selection.
  bool loadFamilyUiSizes(const SdCardFontFamilyInfo& family, GfxRenderer& renderer);
  bool beginFamilyUiSizes(const SdCardFontFamilyInfo& family, GfxRenderer& renderer);
  bool loadFamilyUiSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize);

  // Additively load the .cpfont of `family` at the exact physical `pointSize`
  // (used for size-matched CJK UI fallback alongside the reader-size font).
  // Does not unload anything. If a font of that size is already loaded its id
  // is reused. Returns the font id, or 0 if the family has no file at that size
  // or loading failed.
  int loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize);

  // Unload everything, unregister from renderer.
  void unloadAll(GfxRenderer& renderer, bool clearRendererFallbacks = true);

  // Release rebuildable caches for size-matched UI fallback fonts while
  // retaining the first loaded font, which is the selected reader-size font.
  // No files are read and the owned font objects remain registered.
  void releaseUiFallbackCaches();

#if NOOIR_SD_FONT_DIAGNOSTICS
  void logMemoryStats(const char* stage) const;
#endif

  // Look up the font ID for the loaded family. Returns 0 if nothing loaded
  // or familyName doesn't match.
  int getFontId(const std::string& familyName) const;

  int getFontIdForPointSize(uint8_t pointSize) const;

  // Get name of currently loaded family (empty if none).
  const std::string& currentFamilyName() const { return loadedFamilyName_; };

  // Point size that was actually loaded.
  // 0 if nothing loaded.
  uint8_t currentPointSize() const { return loadedPointSize_; };

 private:
  struct LoadedFont {
    SdCardFont* font;  // heap-allocated, owned
    int fontId;
    uint8_t size;
  };
  int computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) const;

  // Load+register a single .cpfont file and append it to loaded_.
  // Returns the font id, or 0 on failure (allocation, read, or id collision).
  int loadFile(const SdCardFontFileInfo& file, const char* familyName, GfxRenderer& renderer);

  std::string loadedFamilyName_;
  uint8_t loadedPointSize_ = 0;
  std::vector<LoadedFont> loaded_;
  // Interface and reader managers may load the same family at the same size.
  // Keep their internal renderer IDs distinct without changing the persisted
  // family-name selection or any reader ID.
  uint32_t idSalt_ = 0;
};
