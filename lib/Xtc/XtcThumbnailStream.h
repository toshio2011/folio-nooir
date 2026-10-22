#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace xtc_thumb {
constexpr size_t kMaxWorkspaceBytes = 64u * 1024u;
constexpr size_t kPostWorkspaceHeapReserve = 40u * 1024u;
constexpr size_t kLargestBlockMargin = 8u * 1024u;
constexpr size_t kStreamingChunkBytes = 512u;
constexpr uint16_t kUnmapped = 0xFFFFu;

struct SourceRange { uint32_t begin = 0; uint32_t end = 0; };

inline SourceRange sourceRange(const uint16_t destination, const uint16_t sourceSize,
                               const uint32_t inverseScaleFp) {
  SourceRange range{};
  range.begin = static_cast<uint32_t>((static_cast<uint64_t>(destination) * inverseScaleFp) >> 16);
  range.end = static_cast<uint32_t>((static_cast<uint64_t>(destination + 1u) * inverseScaleFp) >> 16);
  if (range.begin >= sourceSize) range.begin = sourceSize - 1u;
  if (range.end > sourceSize) range.end = sourceSize;
  if (range.end <= range.begin) range.end = std::min<uint32_t>(range.begin + 1u, sourceSize);
  return range;
}

inline uint8_t bitsForCount(uint32_t value) {
  uint8_t bits = 0;
  do { ++bits; value >>= 1; } while (value != 0);
  return bits;
}

struct WorkspaceLayout {
  size_t xAxisBytes = 0, yAxisBytes = 0, firstPlaneBytes = 0, outputBytes = 0;
  size_t secondPlaneColumnBytes = 0, totalBytes = 0, largestAllocationBytes = 0;
  size_t rowBytes = 0, planeBytes = 0;
  uint32_t maxCellSamples = 0;
  uint8_t sumBits = 0;
  bool valid = false;
};

inline bool makeWorkspaceLayout(uint16_t sourceWidth, uint16_t sourceHeight,
                                uint16_t targetWidth, uint16_t targetHeight,
                                uint32_t inverseScaleFp, WorkspaceLayout& layout) {
  layout = {};
  if (!sourceWidth || !sourceHeight || !targetWidth || !targetHeight || !inverseScaleFp) return false;
  uint32_t maxXCount = 0, maxYCount = 0;
  for (uint16_t x = 0; x < targetWidth; ++x) {
    const auto range = sourceRange(x, sourceWidth, inverseScaleFp);
    maxXCount = std::max(maxXCount, range.end - range.begin);
  }
  for (uint16_t y = 0; y < targetHeight; ++y) {
    const auto range = sourceRange(y, sourceHeight, inverseScaleFp);
    maxYCount = std::max(maxYCount, range.end - range.begin);
  }
  if (!maxXCount || !maxYCount ||
      static_cast<uint64_t>(maxXCount) * maxYCount > std::numeric_limits<uint32_t>::max()) return false;

  layout.maxCellSamples = maxXCount * maxYCount;
  layout.sumBits = bitsForCount(layout.maxCellSamples);
  const uint64_t pixels = static_cast<uint64_t>(targetWidth) * targetHeight;
  const uint64_t rowBytes = ((static_cast<uint32_t>(targetWidth) + 31u) / 32u) * 4u;
  const uint64_t planeBytes = (static_cast<uint64_t>(sourceWidth) * sourceHeight + 7u) / 8u;
  const uint64_t firstBytes = (pixels * layout.sumBits + 7u) / 8u;
  const uint64_t outputBytes = rowBytes * targetHeight;
  const uint64_t xBytes = (static_cast<uint64_t>(sourceWidth) + targetWidth) * sizeof(uint16_t);
  const uint64_t yBytes = (static_cast<uint64_t>(sourceHeight) + targetHeight) * sizeof(uint16_t);
  const uint64_t secondBytes = static_cast<uint64_t>(targetHeight) * sizeof(uint32_t);
  const uint64_t total = firstBytes + outputBytes + xBytes + yBytes + secondBytes;
  const uint64_t largest = std::max({firstBytes, outputBytes, xBytes, yBytes, secondBytes});
  if (total > kMaxWorkspaceBytes || total > std::numeric_limits<size_t>::max() ||
      largest > std::numeric_limits<size_t>::max() || rowBytes > std::numeric_limits<size_t>::max() ||
      planeBytes > std::numeric_limits<size_t>::max()) return false;
  layout.xAxisBytes = static_cast<size_t>(xBytes);
  layout.yAxisBytes = static_cast<size_t>(yBytes);
  layout.firstPlaneBytes = static_cast<size_t>(firstBytes);
  layout.outputBytes = static_cast<size_t>(outputBytes);
  layout.secondPlaneColumnBytes = static_cast<size_t>(secondBytes);
  layout.totalBytes = static_cast<size_t>(total);
  layout.largestAllocationBytes = static_cast<size_t>(largest);
  layout.rowBytes = static_cast<size_t>(rowBytes);
  layout.planeBytes = static_cast<size_t>(planeBytes);
  layout.valid = true;
  return true;
}

inline bool canAllocateWorkspace(const WorkspaceLayout& layout, size_t freeBytes, size_t largestFreeBlock) {
  return layout.valid &&
         freeBytes >= layout.totalBytes + kPostWorkspaceHeapReserve + kStreamingChunkBytes &&
         largestFreeBlock >= layout.largestAllocationBytes + kLargestBlockMargin + kStreamingChunkBytes;
}

class TwoPlaneReducer {
 public:
  TwoPlaneReducer(uint16_t sw, uint16_t sh, uint16_t tw, uint16_t th, uint32_t inverseScaleFp,
                  const WorkspaceLayout& layout, uint8_t* xAxis, uint8_t* yAxis, uint8_t* firstSums,
                  uint8_t* output, uint32_t* secondColumnSums)
      : sw_(sw), sh_(sh), tw_(tw), th_(th), inverseScaleFp_(inverseScaleFp), layout_(layout),
        xAxis_(reinterpret_cast<uint16_t*>(xAxis)), yAxis_(reinterpret_cast<uint16_t*>(yAxis)),
        firstSums_(firstSums), output_(output), secondColumnSums_(secondColumnSums) {}

  bool initialize() {
    if (!layout_.valid || !xAxis_ || !yAxis_ || !firstSums_ || !output_ || !secondColumnSums_) return false;
    xMap_ = xAxis_; xCounts_ = xMap_ + sw_;
    yMap_ = yAxis_; yCounts_ = yMap_ + sh_;
    std::fill_n(xMap_, sw_, kUnmapped); std::fill_n(xCounts_, tw_, 0);
    std::fill_n(yMap_, sh_, kUnmapped); std::fill_n(yCounts_, th_, 0);
    std::memset(firstSums_, 0, layout_.firstPlaneBytes);
    std::memset(output_, 0xFF, layout_.outputBytes);
    std::fill_n(secondColumnSums_, th_, 0u);
    for (uint16_t x = 0; x < tw_; ++x) {
      const auto range = sourceRange(x, sw_, inverseScaleFp_);
      xCounts_[x] = static_cast<uint16_t>(range.end - range.begin);
      for (uint32_t sx = range.begin; sx < range.end; ++sx) xMap_[sx] = x;
    }
    for (uint16_t y = 0; y < th_; ++y) {
      const auto range = sourceRange(y, sh_, inverseScaleFp_);
      yCounts_[y] = static_cast<uint16_t>(range.end - range.begin);
      for (uint32_t sy = range.begin; sy < range.end; ++sy) yMap_[sy] = y;
    }
    return true;
  }

  bool addFirstPlane(const uint8_t* data, size_t size, size_t offset) {
    if (!data) return false;
    for (size_t i = 0; i < size; ++i) if (!visitSourceByte(data[i], offset + i, true)) return false;
    return true;
  }
  bool addSecondPlane(const uint8_t* data, size_t size, size_t offset) {
    if (!data) return false;
    for (size_t i = 0; i < size; ++i) if (!visitSourceByte(data[i], offset + i, false)) return false;
    return true;
  }
  bool finishSecondPlane() {
    if (activeX_ != kUnmapped) finalizeColumn(activeX_);
    activeX_ = kUnmapped;
    return healthy_;
  }
  const uint8_t* output() const { return output_; }
  size_t outputSize() const { return layout_.outputBytes; }
  size_t rowBytes() const { return layout_.rowBytes; }
  bool healthy() const { return healthy_; }

 private:
  bool visitSourceByte(uint8_t value, size_t offset, bool firstPlane) {
    if (offset >= layout_.planeBytes) return true;
    const size_t colBytes = (static_cast<size_t>(sh_) + 7u) / 8u;
    if (!colBytes) return false;
    const size_t column = offset / colBytes, byteInColumn = offset % colBytes;
    if (column >= sw_) return true;
    const uint16_t sx = static_cast<uint16_t>(sw_ - 1u - column);
    const uint16_t dx = xMap_[sx];
    if (dx == kUnmapped) return true;
    if (!firstPlane && activeX_ != dx) {
      if (activeX_ != kUnmapped) finalizeColumn(activeX_);
      activeX_ = dx;
      std::fill_n(secondColumnSums_, th_, 0u);
    }
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const size_t syValue = byteInColumn * 8u + bit;
      if (syValue >= sh_) break;
      const uint16_t dy = yMap_[syValue];
      if (dy == kUnmapped || ((value >> (7u - bit)) & 1u) == 0) continue;
      if (firstPlane) {
        const size_t index = static_cast<size_t>(dy) * tw_ + dx;
        const uint32_t sum = readPacked(index);
        if (sum >= layout_.maxCellSamples) return healthy_ = false;
        writePacked(index, sum + 1u);
      } else if (++secondColumnSums_[dy] > layout_.maxCellSamples) {
        return healthy_ = false;
      }
    }
    return true;
  }
  uint32_t readPacked(size_t index) const {
    const size_t startBit = index * layout_.sumBits;
    uint32_t value = 0;
    for (uint8_t bit = 0; bit < layout_.sumBits; ++bit) {
      const size_t pos = startBit + bit;
      value |= static_cast<uint32_t>((firstSums_[pos / 8u] >> (pos % 8u)) & 1u) << bit;
    }
    return value;
  }
  void writePacked(size_t index, uint32_t value) {
    const size_t startBit = index * layout_.sumBits;
    for (uint8_t bit = 0; bit < layout_.sumBits; ++bit) {
      const size_t pos = startBit + bit;
      const uint8_t mask = static_cast<uint8_t>(1u << (pos % 8u));
      if ((value >> bit) & 1u) firstSums_[pos / 8u] |= mask;
      else firstSums_[pos / 8u] &= static_cast<uint8_t>(~mask);
    }
  }
  void finalizeColumn(uint16_t dx) {
    const uint64_t xCount = xCounts_[dx];
    for (uint16_t y = 0; y < th_; ++y) {
      const size_t index = static_cast<size_t>(y) * tw_ + dx;
      const uint64_t count = xCount * yCounts_[y];
      const uint64_t sum1 = readPacked(index), sum2 = secondColumnSums_[y];
      const uint8_t gray = count ? static_cast<uint8_t>((count * 255u - sum1 * 170u - sum2 * 85u) / count) : 255;
      uint32_t hash = static_cast<uint32_t>(dx) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
      hash = (hash ^ (hash >> 13)) * 1274126177u;
      const int threshold = static_cast<int>(hash >> 24);
      const bool white = gray >= 128 + ((threshold - 128) / 2);
      const size_t byteIndex = static_cast<size_t>(y) * layout_.rowBytes + dx / 8u;
      const uint8_t mask = static_cast<uint8_t>(1u << (7u - (dx % 8u)));
      if (white) output_[byteIndex] |= mask;
      else output_[byteIndex] &= static_cast<uint8_t>(~mask);
    }
  }

  uint16_t sw_, sh_, tw_, th_;
  uint32_t inverseScaleFp_;
  const WorkspaceLayout& layout_;
  uint16_t* xAxis_;
  uint16_t* yAxis_;
  uint16_t* xMap_ = nullptr;
  uint16_t* xCounts_ = nullptr;
  uint16_t* yMap_ = nullptr;
  uint16_t* yCounts_ = nullptr;
  uint8_t* firstSums_;
  uint8_t* output_;
  uint32_t* secondColumnSums_;
  uint16_t activeX_ = kUnmapped;
  bool healthy_ = true;
};
}  // namespace xtc_thumb