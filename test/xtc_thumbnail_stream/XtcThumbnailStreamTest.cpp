#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "Xtc/XtcThumbnailStream.h"

namespace {
struct Workspace {
  xtc_thumb::WorkspaceLayout layout;
  std::vector<uint8_t> xAxis, yAxis, sums, output;
  std::vector<uint32_t> second;
  Workspace(uint16_t sw, uint16_t sh, uint16_t tw, uint16_t th, uint32_t inv) {
    EXPECT_TRUE(xtc_thumb::makeWorkspaceLayout(sw, sh, tw, th, inv, layout));
    xAxis.resize(layout.xAxisBytes); yAxis.resize(layout.yAxisBytes);
    sums.resize(layout.firstPlaneBytes); output.resize(layout.outputBytes);
    second.resize(layout.secondPlaneColumnBytes / sizeof(uint32_t));
  }
};

uint32_t threshold(uint16_t x, uint16_t y) {
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  return static_cast<uint32_t>(128 + ((static_cast<int>(hash >> 24) - 128) / 2));
}

std::vector<uint8_t> encode(uint16_t width, uint16_t height,
                            const std::function<uint8_t(uint16_t, uint16_t)>& pixelAt) {
  const size_t plane = (static_cast<size_t>(width) * height + 7u) / 8u;
  const size_t colBytes = (static_cast<size_t>(height) + 7u) / 8u;
  std::vector<uint8_t> data(plane * 2u, 0);
  for (uint16_t y = 0; y < height; ++y) for (uint16_t x = 0; x < width; ++x) {
    const uint8_t pixel = pixelAt(x, y);
    const size_t offset = (width - 1u - x) * colBytes + y / 8u;
    const uint8_t mask = static_cast<uint8_t>(1u << (7u - (y % 8u)));
    if (pixel & 2u) data[offset] |= mask;
    if (pixel & 1u) data[plane + offset] |= mask;
  }
  return data;
}

std::vector<uint8_t> reference(const std::vector<uint8_t>& data, uint16_t sw, uint16_t sh,
                               uint16_t tw, uint16_t th, uint32_t inv) {
  const size_t plane = (static_cast<size_t>(sw) * sh + 7u) / 8u;
  const size_t colBytes = (static_cast<size_t>(sh) + 7u) / 8u;
  const size_t rowBytes = ((static_cast<size_t>(tw) + 31u) / 32u) * 4u;
  std::vector<uint8_t> out(rowBytes * th, 0xFF);
  for (uint16_t y = 0; y < th; ++y) for (uint16_t x = 0; x < tw; ++x) {
    const auto yr = xtc_thumb::sourceRange(y, sh, inv);
    const auto xr = xtc_thumb::sourceRange(x, sw, inv);
    uint32_t sum = 0, count = 0;
    for (uint32_t sy = yr.begin; sy < yr.end; ++sy) for (uint32_t sx = xr.begin; sx < xr.end; ++sx) {
      const size_t offset = (sw - 1u - sx) * colBytes + sy / 8u;
      if (offset >= plane) continue;
      const uint8_t bit = static_cast<uint8_t>(7u - (sy % 8u));
      const uint8_t pixel = static_cast<uint8_t>((((data[offset] >> bit) & 1u) << 1) |
                                                  ((data[plane + offset] >> bit) & 1u));
      sum += (3u - pixel) * 85u; ++count;
    }
    const uint8_t gray = count ? static_cast<uint8_t>(sum / count) : 255;
    const size_t index = static_cast<size_t>(y) * rowBytes + x / 8u;
    const uint8_t mask = static_cast<uint8_t>(1u << (7u - (x % 8u)));
    if (gray >= threshold(x, y)) out[index] |= mask;
    else out[index] &= static_cast<uint8_t>(~mask);
  }
  return out;
}

void compareForPattern(const std::function<uint8_t(uint16_t, uint16_t)>& pixelAt, size_t chunk,
                       uint16_t sw = 8, uint16_t sh = 8, uint16_t tw = 4, uint16_t th = 4,
                       uint32_t inv = 2u << 16) {
  const auto data = encode(sw, sh, pixelAt);
  Workspace workspace(sw, sh, tw, th, inv);
  xtc_thumb::TwoPlaneReducer reducer(sw, sh, tw, th, inv, workspace.layout, workspace.xAxis.data(),
      workspace.yAxis.data(), workspace.sums.data(), workspace.output.data(), workspace.second.data());
  ASSERT_TRUE(reducer.initialize());
  const size_t plane = workspace.layout.planeBytes;
  for (size_t offset = 0; offset < plane; offset += chunk) {
    const size_t count = std::min(chunk, plane - offset);
    ASSERT_TRUE(reducer.addFirstPlane(data.data() + offset, count, offset));
  }
  for (size_t offset = 0; offset < plane; offset += chunk) {
    const size_t count = std::min(chunk, plane - offset);
    ASSERT_TRUE(reducer.addSecondPlane(data.data() + plane + offset, count, offset));
  }
  ASSERT_TRUE(reducer.finishSecondPlane());
  EXPECT_EQ(workspace.output, reference(data, sw, sh, tw, th, inv));
}
}  // namespace

TEST(XtcThumbnailStream, MatchesAreaAverageAndDitherForAllGrayLevels) {
  compareForPattern([](uint16_t x, uint16_t y) { return static_cast<uint8_t>((x + 2u * y) % 4u); }, 1);
}

TEST(XtcThumbnailStream, ChunkBoundariesPreserveColumnAndPlaneOrder) {
  const auto pattern = [](uint16_t x, uint16_t y) {
    return static_cast<uint8_t>(((x == 0 || x == 7) && (y == 0 || y == 7)) ? 3u : ((x + y) % 4u));
  };
  compareForPattern(pattern, 3);
  compareForPattern(pattern, 7);
  compareForPattern(pattern, 512);
}

TEST(XtcThumbnailStream, MatchesTypicalPortraitThumbnailScaleAndBmpRowPadding) {
  const auto pattern = [](uint16_t x, uint16_t y) {
    return static_cast<uint8_t>((x * 3u + y * 5u + (y / 7u)) % 4u);
  };
  compareForPattern(pattern, 512, 48, 80, 13, 22, 238312u);
}

TEST(XtcThumbnailStream, 220ThumbnailFitsObservedHeapButHqAndBleAreDeferred) {
  xtc_thumb::WorkspaceLayout regular, hq;
  ASSERT_TRUE(xtc_thumb::makeWorkspaceLayout(480, 800, 132, 220, 238312u, regular));
  ASSERT_TRUE(xtc_thumb::makeWorkspaceLayout(480, 800, 216, 360, 145635u, hq));
  EXPECT_TRUE(xtc_thumb::canAllocateWorkspace(regular, 90'000, 65'524));
  EXPECT_FALSE(xtc_thumb::canAllocateWorkspace(regular, 35'816, 32'756));
  EXPECT_FALSE(xtc_thumb::canAllocateWorkspace(hq, 90'000, 65'524));
}

TEST(XtcThumbnailStream, PreservesFreeHeapReserveAndRejectsInvalidDimensions) {
  xtc_thumb::WorkspaceLayout layout;
  ASSERT_TRUE(xtc_thumb::makeWorkspaceLayout(480, 800, 132, 220, 238312u, layout));
  EXPECT_FALSE(xtc_thumb::canAllocateWorkspace(layout, 65'000, 65'000));
  EXPECT_FALSE(xtc_thumb::makeWorkspaceLayout(0, 800, 132, 220, 238312u, layout));
}
