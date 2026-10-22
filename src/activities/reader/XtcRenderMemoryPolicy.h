#pragma once

#include <algorithm>
#include <cstddef>

namespace xtc_memory {

// Saving the current B/W framebuffer is an optimization: the XTCH renderer can
// stream the base plane again when this copy is unavailable. Keep this optional
// copy away from the heap floor where grayscale work or BLE may leave only a
// few kilobytes after partial chunks have been allocated.
inline constexpr std::size_t kBwSnapshotChunkBytes = 8000;
inline constexpr std::size_t kBwSnapshotHeadroomBytes = 12 * 1024;

constexpr bool canPreserveBwFrame(const std::size_t freeHeap, const std::size_t largestBlock,
                                  const std::size_t frameBufferBytes) {
  if (frameBufferBytes == 0 || largestBlock < std::min(frameBufferBytes, kBwSnapshotChunkBytes)) return false;
  return freeHeap >= frameBufferBytes + kBwSnapshotHeadroomBytes;
}

}  // namespace xtc_memory
