#pragma once

#include "ImageToFramebufferDecoder.h"

// Small, bounds-checked JPEG fallback for baseline JPEGs whose Huffman tables
// exceed JPEGDEC's fast-table limit. Normal JPEGs continue to use JPEGDEC.
class TjpgdToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                           const RenderConfig& config) override;
  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override;
  const char* getFormatName() const override { return "TJpgDec JPEG"; }

  // Header-only check used before invoking JPEGDEC. It avoids entering the
  // decoder for valid JPEGs with long AC Huffman codes that JPEGDEC rejects.
  static bool requiresFallback(const std::string& imagePath);
};
