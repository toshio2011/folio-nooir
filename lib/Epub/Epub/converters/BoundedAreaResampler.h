#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// A small, deterministic streaming reducer for downscaling one grayscale image.
//
// Source rows may arrive in horizontal segments (JPEG MCU blocks) or as
// complete rows (PNG scanlines).  The reducer bins source pixels into the
// destination rectangle, averages each horizontal bin, then averages those
// rows vertically.  A CBZ-only nearest mode instead selects the center source
// sample for each output pixel, preserving thin manga strokes without a full
// intermediate image. Only one source-row accumulator, one destination-row
// accumulator, and one completed output row are kept in RAM; no full image or
// intermediate framebuffer is allocated.
class BoundedAreaResampler {
 public:
  BoundedAreaResampler() = default;
  BoundedAreaResampler(const BoundedAreaResampler&) = delete;
  BoundedAreaResampler& operator=(const BoundedAreaResampler&) = delete;

  bool begin(const int sourceWidth, const int sourceHeight, const int destinationWidth,
             const int destinationHeight, const bool preferNearest = false) {
    reset();
    if (sourceWidth <= destinationWidth || sourceHeight <= destinationHeight || destinationWidth <= 0 ||
        destinationHeight <= 0) {
      return false;
    }

    sourceWidth_ = sourceWidth;
    sourceHeight_ = sourceHeight;
    destinationWidth_ = destinationWidth;
    destinationHeight_ = destinationHeight;
    nearestMode_ = preferNearest;

    horizontalSums_.reset(new (std::nothrow) uint32_t[static_cast<size_t>(destinationWidth_)]);
    verticalSums_.reset(new (std::nothrow) uint32_t[static_cast<size_t>(destinationWidth_)]);
    completedRow_.reset(new (std::nothrow) uint8_t[static_cast<size_t>(destinationWidth_)]);
    if (!horizontalSums_ || !verticalSums_ || !completedRow_) {
      reset();
      return false;
    }

    std::memset(horizontalSums_.get(), 0, sizeof(uint32_t) * static_cast<size_t>(destinationWidth_));
    std::memset(verticalSums_.get(), 0, sizeof(uint32_t) * static_cast<size_t>(destinationWidth_));
    sourceRow_ = -1;
    nextSourceX_ = 0;
    destinationRow_ = -1;
    pendingRowIndex_ = -1;
    verticalRowCount_ = 0;
    pendingRow_ = false;
    nextNearestDestinationRow_ = 0;
    nearestDestinationRow_ = -1;
    nearestDestinationX_ = 0;
    nearestRowActive_ = false;
    return true;
  }

  void reset() {
    horizontalSums_.reset();
    verticalSums_.reset();
    completedRow_.reset();
    sourceWidth_ = 0;
    sourceHeight_ = 0;
    destinationWidth_ = 0;
    destinationHeight_ = 0;
    sourceRow_ = -1;
    nextSourceX_ = 0;
    destinationRow_ = -1;
    pendingRowIndex_ = -1;
    verticalRowCount_ = 0;
    pendingRow_ = false;
    nearestMode_ = false;
    nextNearestDestinationRow_ = 0;
    nearestDestinationRow_ = -1;
    nearestDestinationX_ = 0;
    nearestRowActive_ = false;
  }

  bool active() const { return horizontalSums_ && verticalSums_ && completedRow_; }

  // Add a contiguous segment of one source row.  Segments must arrive in
  // raster order; this is the order provided by JPEGDEC MCU callbacks.
  bool addSourceRowSegment(const int sourceY, const int sourceX, const uint8_t* pixels, const int count) {
    if (!active() || !pixels || sourceY < 0 || sourceY >= sourceHeight_ || sourceX < 0 || count <= 0 ||
        sourceX >= sourceWidth_ || sourceX + count > sourceWidth_) {
      return false;
    }

    if (sourceY != sourceRow_) {
      if (sourceRow_ >= 0 && nextSourceX_ != sourceWidth_) return false;
      if (pendingRow_) return false;
      if (sourceRow_ >= 0 && sourceY != sourceRow_ + 1) return false;
      sourceRow_ = sourceY;
      nextSourceX_ = 0;
      std::memset(horizontalSums_.get(), 0, sizeof(uint32_t) * static_cast<size_t>(destinationWidth_));
      nearestRowActive_ = false;
      nearestDestinationRow_ = -1;
      nearestDestinationX_ = 0;
      if (nearestMode_) {
        // Pick the center source sample for each destination row.  This is a
        // bounded, streaming alternative for CBZ manga: it keeps thin ink
        // strokes from being averaged into a broad gray haze while retaining
        // the same memory bound as the normal reducer. EPUB keeps area mode.
        while (nextNearestDestinationRow_ < destinationHeight_ &&
               nearestSourceY(nextNearestDestinationRow_) < sourceY) {
          ++nextNearestDestinationRow_;
        }
        if (nextNearestDestinationRow_ < destinationHeight_ &&
            nearestSourceY(nextNearestDestinationRow_) == sourceY) {
          nearestRowActive_ = true;
          nearestDestinationRow_ = nextNearestDestinationRow_;
          nearestDestinationX_ = 0;
        }
      }
    }

    if (sourceX != nextSourceX_) return false;

    // Bresenham-style binning avoids one division per source pixel while still
    // assigning every source sample to its exact destination box.
    const int64_t scaledStart = static_cast<int64_t>(sourceX) * destinationWidth_;
    int destinationX = static_cast<int>(scaledStart / sourceWidth_);
    uint32_t error = static_cast<uint32_t>(scaledStart % sourceWidth_);
    for (int i = 0; i < count; ++i) {
      if (nearestMode_) {
        if (!nearestRowActive_) continue;
        const int currentSourceX = sourceX + i;
        while (nearestDestinationX_ < destinationWidth_ &&
               nearestSourceX(nearestDestinationX_) < currentSourceX) {
          ++nearestDestinationX_;
        }
        if (nearestDestinationX_ < destinationWidth_ &&
            nearestSourceX(nearestDestinationX_) == currentSourceX) {
          horizontalSums_[nearestDestinationX_] = pixels[i];
          ++nearestDestinationX_;
        }
        continue;
      }
      if (destinationX >= destinationWidth_) destinationX = destinationWidth_ - 1;
      horizontalSums_[destinationX] += pixels[i];
      error += static_cast<uint32_t>(destinationWidth_);
      if (error >= static_cast<uint32_t>(sourceWidth_)) {
        error -= static_cast<uint32_t>(sourceWidth_);
        ++destinationX;
      }
    }
    nextSourceX_ += count;

    if (nextSourceX_ == sourceWidth_) return finishSourceRow();
    return true;
  }

  bool hasPendingRow() const { return pendingRow_; }
  int pendingRowIndex() const { return pendingRow_ ? pendingRowIndex_ : -1; }
  const uint8_t* pendingRowData() const { return pendingRow_ ? completedRow_.get() : nullptr; }
  void consumePendingRow() { pendingRow_ = false; }

  // Flush the final destination row after the decoder has delivered its last
  // source row.
  bool finish() {
    if (!active() || sourceRow_ < 0 || nextSourceX_ != sourceWidth_) return false;
    if (pendingRow_) return false;
    return nearestMode_ ? nextNearestDestinationRow_ >= destinationHeight_ : finishDestinationRow();
  }

 private:
  int nearestSourceY(const int destinationY) const {
    return static_cast<int>((static_cast<int64_t>(2 * destinationY + 1) * sourceHeight_) /
                             (2 * destinationHeight_));
  }

  int nearestSourceX(const int destinationX) const {
    return static_cast<int>((static_cast<int64_t>(2 * destinationX + 1) * sourceWidth_) /
                             (2 * destinationWidth_));
  }

  bool finishSourceRow() {
    if (nearestMode_) {
      if (nearestRowActive_) {
        if (nearestDestinationX_ != destinationWidth_) return false;
        for (int x = 0; x < destinationWidth_; ++x) {
          completedRow_[x] = static_cast<uint8_t>(horizontalSums_[x]);
        }
        pendingRowIndex_ = nearestDestinationRow_;
        pendingRow_ = true;
        ++nextNearestDestinationRow_;
        nearestRowActive_ = false;
      }
      return true;
    }
    const int destinationY = static_cast<int>((static_cast<int64_t>(sourceRow_) * destinationHeight_) / sourceHeight_);
    if (destinationRow_ < 0) {
      destinationRow_ = destinationY;
    } else if (destinationY != destinationRow_) {
      if (!finishDestinationRow()) return false;
      destinationRow_ = destinationY;
    }

    for (int x = 0; x < destinationWidth_; ++x) {
      const int sourceStart = static_cast<int>((static_cast<int64_t>(x) * sourceWidth_ + destinationWidth_ - 1) /
                                               destinationWidth_);
      const int sourceEnd = static_cast<int>((static_cast<int64_t>(x + 1) * sourceWidth_ + destinationWidth_ - 1) /
                                             destinationWidth_);
      const int sourceCount = std::max(1, sourceEnd - sourceStart);
      verticalSums_[x] += horizontalSums_[x] / static_cast<uint32_t>(sourceCount);
    }
    ++verticalRowCount_;
    return true;
  }

  bool finishDestinationRow() {
    if (destinationRow_ < 0 || verticalRowCount_ == 0 || pendingRow_) return false;
    for (int x = 0; x < destinationWidth_; ++x) {
      completedRow_[x] = static_cast<uint8_t>(verticalSums_[x] / static_cast<uint32_t>(verticalRowCount_));
      verticalSums_[x] = 0;
    }
    pendingRowIndex_ = destinationRow_;
    pendingRow_ = true;
    verticalRowCount_ = 0;
    return true;
  }

  int sourceWidth_{0};
  int sourceHeight_{0};
  int destinationWidth_{0};
  int destinationHeight_{0};
  int sourceRow_{-1};
  int nextSourceX_{0};
  int destinationRow_{-1};
  int pendingRowIndex_{-1};
  uint32_t verticalRowCount_{0};
  bool pendingRow_{false};
  bool nearestMode_{false};
  int nextNearestDestinationRow_{0};
  int nearestDestinationRow_{-1};
  int nearestDestinationX_{0};
  bool nearestRowActive_{false};
  std::unique_ptr<uint32_t[]> horizontalSums_;
  std::unique_ptr<uint32_t[]> verticalSums_;
  std::unique_ptr<uint8_t[]> completedRow_;
};
