#include "ImageToFramebufferDecoder.h"

#include <Logging.h>

#include <cstdint>

bool ImageToFramebufferDecoder::validateImageDimensions(int width, int height, const std::string& format) {
  // A corrupt header must not be able to overflow the pixel-count check and
  // make an enormous allocation look safe on the X3's fragmented heap.
  if (width <= 0 || height <= 0) {
    LOG_ERR("IMG", "Invalid %s dimensions: %dx%d", format.c_str(), width, height);
    return false;
  }
  const int64_t pixels = static_cast<int64_t>(width) * static_cast<int64_t>(height);
  if (pixels > MAX_SOURCE_PIXELS) {
    LOG_ERR("IMG", "Image too large (%dx%d = %lld pixels %s), max supported: %d pixels", width, height,
            static_cast<long long>(pixels), format.c_str(), MAX_SOURCE_PIXELS);
    return false;
  }
  return true;
}

bool ImageToFramebufferDecoder::validateImageDimensionsForRender(const int width, const int height,
                                                                 const std::string& format,
                                                                 const RenderConfig& config) {
  // The opt-in callers must still downsample/stream after this check; this
  // helper only widens the header guard and never authorizes a source-sized
  // framebuffer allocation.
  const int64_t pixels = static_cast<int64_t>(width) * static_cast<int64_t>(height);
  if (!config.allowBoundedLargeSource || pixels <= MAX_SOURCE_PIXELS) {
    return validateImageDimensions(width, height, format);
  }

  if (width <= 0 || height <= 0) {
    LOG_ERR("EPUBIMG", "skip=%s reason=invalid_dimensions size=%dx%d", format.c_str(), width, height);
    return false;
  }

  if (width > MAX_BOUNDED_SOURCE_DIMENSION || height > MAX_BOUNDED_SOURCE_DIMENSION ||
      pixels > MAX_BOUNDED_SOURCE_PIXELS || config.maxWidth <= 0 || config.maxHeight <= 0) {
    LOG_ERR("EPUBIMG", "skip=%s reason=bounded_source_limit source=%dx%d pixels=%lld target=%dx%d", format.c_str(),
            width, height, static_cast<long long>(pixels), config.maxWidth, config.maxHeight);
    return false;
  }

  LOG_DBG("EPUBIMG", "bounded_source=%s source=%dx%d pixels=%lld target=%dx%d", format.c_str(), width, height,
          static_cast<long long>(pixels), config.maxWidth, config.maxHeight);
  return true;
}

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}
