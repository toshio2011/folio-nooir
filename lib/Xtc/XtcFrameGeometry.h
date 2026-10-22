#pragma once

#include <cstdint>

namespace xtc_frame {

enum class Mode : uint8_t { FitContain = 0, CropFill = 1 };

struct Rect {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct Mapping {
  Rect source;
  Rect destination;
  bool valid = false;
};

// Maps a source page into a screen viewport using integer-only, centered
// geometry. FitContain preserves the full page and leaves balanced margins;
// CropFill fills the viewport and selects the largest centered source crop
// with the viewport aspect ratio. No pixels or frame buffers are allocated.
inline bool calculateMapping(const uint16_t sourceWidth, const uint16_t sourceHeight,
                             const uint16_t viewportX, const uint16_t viewportY,
                             const uint16_t viewportWidth, const uint16_t viewportHeight,
                             const Mode mode, Mapping& mapping) {
  mapping = {};
  if (sourceWidth == 0 || sourceHeight == 0 || viewportWidth == 0 || viewportHeight == 0) return false;
  if (static_cast<uint32_t>(viewportX) + viewportWidth > UINT16_MAX ||
      static_cast<uint32_t>(viewportY) + viewportHeight > UINT16_MAX) return false;
  if (mode != Mode::FitContain && mode != Mode::CropFill) return false;

  mapping.source = {0, 0, sourceWidth, sourceHeight};
  mapping.destination = {viewportX, viewportY, viewportWidth, viewportHeight};
  const uint64_t sourceAspect = static_cast<uint64_t>(sourceWidth) * viewportHeight;
  const uint64_t viewportAspect = static_cast<uint64_t>(sourceHeight) * viewportWidth;

  if (mode == Mode::FitContain) {
    if (sourceAspect > viewportAspect) {
      const uint32_t height = static_cast<uint32_t>(static_cast<uint64_t>(sourceHeight) * viewportWidth / sourceWidth);
      mapping.destination.height = static_cast<uint16_t>(height == 0 ? 1 : height);
      mapping.destination.y = static_cast<uint16_t>(viewportY + (viewportHeight - mapping.destination.height) / 2u);
    } else {
      const uint32_t width = static_cast<uint32_t>(static_cast<uint64_t>(sourceWidth) * viewportHeight / sourceHeight);
      mapping.destination.width = static_cast<uint16_t>(width == 0 ? 1 : width);
      mapping.destination.x = static_cast<uint16_t>(viewportX + (viewportWidth - mapping.destination.width) / 2u);
    }
  } else {
    if (sourceAspect > viewportAspect) {
      const uint32_t width = static_cast<uint32_t>(static_cast<uint64_t>(sourceHeight) * viewportWidth / viewportHeight);
      mapping.source.width = static_cast<uint16_t>(width == 0 ? 1 : width);
      mapping.source.x = static_cast<uint16_t>((sourceWidth - mapping.source.width) / 2u);
    } else {
      const uint32_t height = static_cast<uint32_t>(static_cast<uint64_t>(sourceWidth) * viewportHeight / viewportWidth);
      mapping.source.height = static_cast<uint16_t>(height == 0 ? 1 : height);
      mapping.source.y = static_cast<uint16_t>((sourceHeight - mapping.source.height) / 2u);
    }
  }

  mapping.valid = mapping.source.width > 0 && mapping.source.height > 0 &&
                  static_cast<uint32_t>(mapping.source.x) + mapping.source.width <= sourceWidth &&
                  static_cast<uint32_t>(mapping.source.y) + mapping.source.height <= sourceHeight &&
                  mapping.destination.width > 0 && mapping.destination.height > 0 &&
                  mapping.destination.x >= viewportX && mapping.destination.y >= viewportY &&
                  static_cast<uint32_t>(mapping.destination.x) + mapping.destination.width <= static_cast<uint32_t>(viewportX) + viewportWidth &&
                  static_cast<uint32_t>(mapping.destination.y) + mapping.destination.height <= static_cast<uint32_t>(viewportY) + viewportHeight;
  return mapping.valid;
}

}  // namespace xtc_frame
