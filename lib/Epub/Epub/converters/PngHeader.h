#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace pngheader {

inline uint32_t readBigEndian32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

inline uint32_t crc32(const uint8_t* bytes, const size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

// Parse and validate PNG's fixed 33-byte signature + IHDR prefix. This lets
// EPUB layout/indexing read image dimensions without allocating PNGdec's much
// larger inflate-and-scanline workspace.
inline bool parseDimensions(const uint8_t* header, const size_t length, int16_t& width, int16_t& height) {
  static constexpr uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (!header || length < 33 || std::memcmp(header, signature, sizeof(signature)) != 0 ||
      readBigEndian32(header + 8) != 13 || std::memcmp(header + 12, "IHDR", 4) != 0) {
    return false;
  }

  const uint32_t expectedCrc = readBigEndian32(header + 29);
  if (crc32(header + 12, 17) != expectedCrc) return false;

  const uint32_t parsedWidth = readBigEndian32(header + 16);
  const uint32_t parsedHeight = readBigEndian32(header + 20);
  if (parsedWidth == 0 || parsedHeight == 0 ||
      parsedWidth > static_cast<uint32_t>(std::numeric_limits<int16_t>::max()) ||
      parsedHeight > static_cast<uint32_t>(std::numeric_limits<int16_t>::max())) {
    return false;
  }

  width = static_cast<int16_t>(parsedWidth);
  height = static_cast<int16_t>(parsedHeight);
  return true;
}

}  // namespace pngheader
