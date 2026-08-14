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

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_ERR("IMG", "Warning: Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}
