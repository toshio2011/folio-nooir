#pragma once
#include <HalStorage.h>

#include <memory>
#include <string>
#include <cstdint>

#include "Block.h"

class ImageBlock final : public Block {
 public:
  struct PixelCacheReplayStats {
    uint32_t cachePasses = 0;
    uint32_t bandsRead = 0;
    uint32_t replayCalls = 0;
    uint64_t bytesRead = 0;
  };

  ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height);
  ~ImageBlock() override = default;

  const std::string& getImagePath() const { return imagePath; }
  int16_t getWidth() const { return width; }
  int16_t getHeight() const { return height; }

  bool imageExists() const;
  bool hasValidCache() const;
  bool needsDecode() const;
  void renderPlaceholder(GfxRenderer& renderer, int x, int y) const;
  static void clearSessionRenderFailures();

  // A page render draws its image up to ~13 times (BW double-refresh plus every
  // grayscale band pass), and each draw streams the whole .pxc off SD. The
  // first draw caches the pixel payload in RAM (chunked, heap-gated, falls back
  // to streaming when it doesn't fit); the reader calls this when the page
  // render completes so nothing stays resident between pages.
  static void releaseRenderCache();
  static void resetPixelCacheReplayStats();
  static PixelCacheReplayStats getPixelCacheReplayStats();

  // Shared bounded pixel-cache replay for other image readers (currently CBZ).
  // The cache remains session-owned by the caller and does not alter EPUB
  // rendering behavior.
  static bool renderFromPixelCache(GfxRenderer& renderer, const std::string& cachePath, int x, int y, int width,
                                   int height);

  // CBZ grayscale replay. A cache miss streams only the active physical band:
  // contiguous rows in native landscape and compacted logical columns in
  // portrait. EPUB keeps using the full replay path above.
  static bool renderFromPixelCacheBand(GfxRenderer& renderer, const std::string& cachePath, int x, int y,
                                       int width, int height);

  // Lazy extraction hook: the section build only header-probes images for their
  // dimensions; the file at imagePath is extracted out of the book on first
  // render, via this callback (function pointer + context, not std::function —
  // this is render-loop code). Registered by the reader activity that owns the
  // Epub, cleared on its exit.
  using ExtractFn = bool (*)(void* ctx, const char* srcPath, const char* destPath);
  static void setExtractor(void* ctx, ExtractFn fn);

  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return false; }

  void render(GfxRenderer& renderer, const int x, const int y);
  bool serialize(HalFile& file);
  static std::unique_ptr<ImageBlock> deserialize(HalFile& file);

 private:
  std::string imagePath;
  std::string srcPath;  // book-internal source href; empty once known-extracted
  int16_t width;
  int16_t height;
  // Prevent repeated EPUB re-inflation across the BW and grayscale passes when
  // a decoder failure persists for this deserialized page image.
  bool sourceRefreshAttempted = false;
  // The same ImageBlock may be rendered for the BW pass and several grayscale
  // bands. Cache existence is stable for that page render, so memoize the
  // cheap result and avoid an SD stat before every replay. The flag is scoped
  // to this block instance and therefore cannot hide changes across pages or
  // reader sessions.
  bool cachePresenceKnown = false;
  bool cachePresent = false;

  static void* extractCtx;
  static ExtractFn extractFn;
};
