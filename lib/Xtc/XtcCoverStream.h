#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace xtc_cover {
constexpr uint16_t kMaxBandGroups = 8;

#pragma pack(push, 1)
struct TwoBitBmpHeader {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
  uint8_t palette[16];
};
#pragma pack(pop)
static_assert(sizeof(TwoBitBmpHeader) == 70, "2-bit BMP headers contain four palette entries");

inline bool makeTwoBitBmpHeader(const uint16_t width, const uint16_t height, const size_t rowBytes,
                                TwoBitBmpHeader& header) {
  const uint64_t imageBytes = static_cast<uint64_t>(rowBytes) * height;
  const uint64_t fileBytes = sizeof(header) + imageBytes;
  if (!width || !height || imageBytes > UINT32_MAX || fileBytes > UINT32_MAX) return false;
  std::memset(&header, 0, sizeof(header));
  header.bfType = 0x4D42;
  header.bfSize = static_cast<uint32_t>(fileBytes);
  header.bfOffBits = sizeof(header);
  header.biSize = 40;
  header.biWidth = width;
  header.biHeight = -static_cast<int32_t>(height);  // top-down scanlines
  header.biPlanes = 1;
  header.biBitCount = 2;
  header.biCompression = 0;
  header.biSizeImage = static_cast<uint32_t>(imageBytes);
  header.biXPelsPerMeter = 2835;
  header.biYPelsPerMeter = 2835;
  header.biClrUsed = 4;
  header.biClrImportant = 4;
  for (uint8_t index = 0; index < 4; ++index) {
    const uint8_t gray = static_cast<uint8_t>(index * 85u);
    header.palette[index * 4u + 0u] = gray;
    header.palette[index * 4u + 1u] = gray;
    header.palette[index * 4u + 2u] = gray;
  }
  return true;
}

inline bool makeTwoBitCoverWorkspace(const uint16_t width, const uint16_t height,
                                     size_t& planeBytes, size_t& columnBytes,
                                     size_t& bandPlaneBytes, size_t& bmpRowBytes, size_t& stripBytes,
                                     size_t& totalBytes) {
  planeBytes = columnBytes = bandPlaneBytes = bmpRowBytes = stripBytes = totalBytes = 0;
  if (width == 0 || height == 0 || (height & 7u) != 0) return false;
  const uint64_t pixels = static_cast<uint64_t>(width) * height;
  const uint64_t plane = (pixels + 7u) / 8u;
  const uint64_t column = (static_cast<uint32_t>(height) + 7u) / 8u;
  const uint64_t row = ((static_cast<uint32_t>(width) * 2u + 31u) / 32u) * 4u;
  const uint64_t bandGroups = std::min<uint64_t>(kMaxBandGroups, height / 8u);
  const uint64_t firstPlaneBand = static_cast<uint64_t>(width) * bandGroups;
  const uint64_t strip = row * 8u * bandGroups;
  const uint64_t total = strip + firstPlaneBand;
  if (plane > static_cast<uint64_t>(SIZE_MAX) || column > static_cast<uint64_t>(SIZE_MAX) ||
      plane * 2u > static_cast<uint64_t>(SIZE_MAX) || column * width != plane ||
      row > static_cast<uint64_t>(SIZE_MAX) || firstPlaneBand > static_cast<uint64_t>(SIZE_MAX) ||
      strip > static_cast<uint64_t>(SIZE_MAX) ||
      total > static_cast<uint64_t>(SIZE_MAX)) return false;
  planeBytes = static_cast<size_t>(plane);
  columnBytes = static_cast<size_t>(column);
  bandPlaneBytes = static_cast<size_t>(firstPlaneBand);
  bmpRowBytes = static_cast<size_t>(row);
  stripBytes = static_cast<size_t>(strip);
  totalBytes = static_cast<size_t>(total);
  return true;
}

// Assembles up to 64 BMP rows from XTCH's two column-major bit planes. The
// caller feeds each complete page stream once per band; keeping a small band
// avoids both a full-page buffer and a full-page reread for every 8-row group.
class TwoBitCoverStripAssembler {
 public:
  TwoBitCoverStripAssembler(const uint16_t width, const uint16_t height,
                            const size_t planeBytes, const size_t columnBytes,
                            const size_t bmpRowBytes, const size_t stripBytes,
                            uint8_t* firstPlaneColumns, uint8_t* outputRows)
      : width_(width), height_(height), planeBytes_(planeBytes), columnBytes_(columnBytes),
        bmpRowBytes_(bmpRowBytes), stripBytes_(stripBytes), firstPlaneColumns_(firstPlaneColumns),
        outputRows_(outputRows) {}

  bool begin(const uint16_t group) {
    if (width_ == 0 || height_ == 0 || (height_ & 7u) != 0 || columnBytes_ == 0 || planeBytes_ > SIZE_MAX / 2u ||
        bmpRowBytes_ > SIZE_MAX / 8u ||
        static_cast<uint64_t>(columnBytes_) * width_ != planeBytes_ ||
        bmpRowBytes_ < (static_cast<size_t>(width_) * 2u + 7u) / 8u ||
        group >= height_ / 8u ||
        !firstPlaneColumns_ || !outputRows_) return false;
    firstGroup_ = group;
    groupCount_ = static_cast<uint16_t>(std::min<uint32_t>(kMaxBandGroups, height_ / 8u - group));
    if (groupCount_ == 0 ||
        static_cast<uint64_t>(groupCount_) * width_ > SIZE_MAX ||
        static_cast<uint64_t>(groupCount_) * 8u * bmpRowBytes_ > stripBytes_) return false;
    firstColumnsSeen_ = 0;
    secondColumnsSeen_ = 0;
    nextOffset_ = 0;
    healthy_ = true;
    std::memset(firstPlaneColumns_, 0, static_cast<size_t>(groupCount_) * width_);
    std::memset(outputRows_, 0, static_cast<size_t>(groupCount_) * 8u * bmpRowBytes_);
    return true;
  }

  bool addChunk(const uint8_t* data, const size_t size, const size_t offset) {
    if (!data || !healthy_ || planeBytes_ > SIZE_MAX / 2u) return healthy_ = false;
    const size_t pageBytes = planeBytes_ * 2u;
    if (offset != nextOffset_ || offset > pageBytes || size > pageBytes - offset) return healthy_ = false;
    nextOffset_ += size;
    const size_t planeEnd = planeBytes_;
    const size_t end = offset + size;
    for (size_t absolute = offset; absolute < end; ++absolute) {
      const bool secondPlane = absolute >= planeEnd;
      const size_t local = secondPlane ? absolute - planeEnd : absolute;
      if (local >= planeBytes_) continue;
      const size_t column = local / columnBytes_;
      const size_t byteInColumn = local % columnBytes_;
      if (byteInColumn < firstGroup_ || byteInColumn >= firstGroup_ + groupCount_ || column >= width_) continue;
      const size_t groupInBand = byteInColumn - firstGroup_;
      const uint16_t x = static_cast<uint16_t>(width_ - 1u - column);
      const uint8_t value = data[absolute - offset];
      if (!secondPlane) {
        firstPlaneColumns_[groupInBand * width_ + x] = value;
        ++firstColumnsSeen_;
        continue;
      }
      ++secondColumnsSeen_;
      for (uint8_t row = 0; row < 8; ++row) {
        const uint16_t y = static_cast<uint16_t>(groupInBand * 8u + row);
        if (y >= height_) break;
        const uint8_t bit = static_cast<uint8_t>(7u - row);
        const uint8_t firstPlane = firstPlaneColumns_[groupInBand * width_ + x];
        const uint8_t pixel = static_cast<uint8_t>((((firstPlane >> bit) & 1u) << 1u) |
                                                   ((value >> bit) & 1u));
        // XTH values use an asymmetric middle-level order: 0=white, 1=dark,
        // 2=light, 3=black. Keep both middle grays in their native order.
        constexpr uint8_t kBmpPaletteIndex[4] = {3, 1, 2, 0};
        const uint8_t paletteIndex = kBmpPaletteIndex[pixel];
        const size_t byteIndex = static_cast<size_t>(y) * bmpRowBytes_ + x / 4u;
        const uint8_t shift = static_cast<uint8_t>(6u - (x % 4u) * 2u);
        outputRows_[byteIndex] = static_cast<uint8_t>((outputRows_[byteIndex] & ~(0x03u << shift)) |
                                                      (paletteIndex << shift));
      }
    }
    return healthy_;
  }

  bool finish() {
    healthy_ = healthy_ && nextOffset_ == planeBytes_ * 2u &&
               firstColumnsSeen_ == static_cast<size_t>(width_) * groupCount_ &&
               secondColumnsSeen_ == static_cast<size_t>(width_) * groupCount_;
    return healthy_;
  }

  const uint8_t* rows() const { return outputRows_; }
  size_t rowBytes() const { return bmpRowBytes_; }
  uint16_t firstRow() const { return static_cast<uint16_t>(firstGroup_ * 8u); }
  uint16_t rowCount() const { return static_cast<uint16_t>(groupCount_ * 8u); }

 private:
  uint16_t width_;
  uint16_t height_;
  size_t planeBytes_;
  size_t columnBytes_;
  size_t bmpRowBytes_;
  size_t stripBytes_;
  uint8_t* firstPlaneColumns_;
  uint8_t* outputRows_;
  uint16_t firstGroup_ = 0;
  uint16_t groupCount_ = 0;
  size_t firstColumnsSeen_ = 0;
  size_t secondColumnsSeen_ = 0;
  size_t nextOffset_ = 0;
  bool healthy_ = false;
};

}  // namespace xtc_cover
