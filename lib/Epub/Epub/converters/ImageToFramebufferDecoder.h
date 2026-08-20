#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>

#include "ImageDiagnostics.h"

class GfxRenderer;

struct ImageDimensions {
  int16_t width;
  int16_t height;
};

struct RenderConfig {
  int x, y;
  int maxWidth, maxHeight;
  bool useGrayscale = true;
  bool useDithering = true;
  bool performanceMode = false;
  bool useExactDimensions = false;  // If true, use maxWidth/maxHeight as exact output size (no recalculation)
  bool cbzQualityMode = false;      // CBZ-only reduced-strength quantization
  // Temporary CBZ-only A/B diagnostic.  When enabled, the image converter
  // emits only logical black (0) or white (3); the reader then presents the
  // native BW framebuffer path instead of composing the two grayscale planes.
  bool cbzBwDiagnostic = false;
  // Temporary CBZ-only direct A/B diagnostic.  This is intentionally separate
  // from cbzBwDiagnostic: the direct test must not create or replay a pixel
  // cache, and it clips decoded source pixels to the visible viewport.
  bool cbzDirectBwDiagnostic = false;
  int cbzDirectViewportX = 0;
  int cbzDirectViewportY = 0;
  int cbzDirectViewportWidth = 0;
  int cbzDirectViewportHeight = 0;
  // EPUB-only opt-in for bounded large-source handling. CBZ and all other
  // callers leave this false, preserving the existing source-pixel guard.
  bool allowBoundedLargeSource = false;
  // One-shot, bounded source crop for the CBZ TJpgDec diagnostic. The
  // converter streams gray8 rows directly to a small BMP on SD; no crop
  // framebuffer is retained.
  bool cbzDumpTjpgGrayCrop = false;
  int cbzDumpCropX = 0;
  int cbzDumpCropY = 0;
  int cbzDumpCropWidth = 0;
  int cbzDumpCropHeight = 0;
  std::string cbzDumpCropPath;
  std::string cachePath;            // If non-empty, decoder will write pixel cache to this path
  ImageRenderDiagnostics* diagnostics = nullptr;  // Temporary CBZ-only timing diagnostics
  ImageQualityProbe* qualityProbe = nullptr;      // Temporary bounded pre/post-dither probe
};

inline void recordImageQualityPixel(const RenderConfig& config, const int screenX, const int screenY,
                                    const uint8_t gray, const uint8_t level) {
  if (config.diagnostics) {
    ImageRenderDiagnostics& diagnostics = *config.diagnostics;
    if (gray < diagnostics.grayMin) diagnostics.grayMin = gray;
    if (gray > diagnostics.grayMax) diagnostics.grayMax = gray;
    diagnostics.grayBins[gray >> 6]++;
    diagnostics.quantizedBins[level > 3 ? 3 : level]++;
    ++diagnostics.emittedPixels;
  }
  if (config.qualityProbe) {
    config.qualityProbe->record(screenX - config.x, screenY - config.y, gray, level);
  }
}

class ImageToFramebufferDecoder {
 public:
  virtual ~ImageToFramebufferDecoder() = default;

  virtual bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) = 0;

  virtual bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const = 0;

  virtual const char* getFormatName() const = 0;

 protected:
  // Size validation helpers
  static constexpr int MAX_SOURCE_PIXELS = 3145728;  // 2048 * 1536
  static constexpr int MAX_BOUNDED_SOURCE_DIMENSION = 16384;
  static constexpr int64_t MAX_BOUNDED_SOURCE_PIXELS = 32ll * 1024ll * 1024ll;

  bool validateImageDimensions(int width, int height, const std::string& format);
  bool validateImageDimensionsForRender(int width, int height, const std::string& format,
                                        const RenderConfig& config);
  void warnUnsupportedFeature(const std::string& feature, const std::string& imagePath);
};
