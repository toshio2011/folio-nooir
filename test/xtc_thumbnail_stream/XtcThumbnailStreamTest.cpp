#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "Xtc/XtcCoverStream.h"
#include "Xtc/XtcThumbnailStream.h"
#include "Xtc/Xtc/XtcTypes.h"

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

TEST(XtcStreamingPolicy, RejectsUnboundedChunksAndAllowsTheEightKiBLimit) {
  EXPECT_FALSE(xtc::isPageStreamingChunkSizeSupported(0));
  EXPECT_TRUE(xtc::isPageStreamingChunkSizeSupported(1));
  EXPECT_TRUE(xtc::isPageStreamingChunkSizeSupported(1024));
  EXPECT_TRUE(xtc::isPageStreamingChunkSizeSupported(8u * 1024u));
  EXPECT_FALSE(xtc::isPageStreamingChunkSizeSupported(8u * 1024u + 1u));
}

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

TEST(XtcThumbnailStream, StreamsOneBitSourceAcrossArbitraryChunkBoundaries) {
  constexpr uint16_t sourceWidth = 16;
  constexpr uint16_t sourceHeight = 16;
  constexpr uint16_t targetWidth = 8;
  constexpr uint16_t targetHeight = 8;
  constexpr uint32_t inverseScaleFp = 2u << 16;
  xtc_thumb::OnePlaneWorkspaceLayout layout;
  ASSERT_TRUE(xtc_thumb::makeOnePlaneWorkspaceLayout(sourceWidth, sourceHeight, targetWidth, targetHeight,
                                                      inverseScaleFp, layout));
  EXPECT_TRUE(xtc_thumb::canAllocateOnePlaneWorkspace(layout, 90'000, 65'524));
  EXPECT_FALSE(xtc_thumb::canAllocateOnePlaneWorkspace(layout, 40'000, 65'524));

  std::vector<uint16_t> xMap(sourceWidth), yMap(sourceHeight), xCounts(targetWidth), yCounts(targetHeight);
  std::vector<uint32_t> whiteSums(targetWidth);
  std::vector<uint8_t> outputRow(layout.outputRowBytes);
  std::vector<uint8_t> source(layout.pageBytes, 0);
  const auto isWhite = [](uint16_t x, uint16_t y) { return ((x / 3u + y / 2u) % 3u) != 0; };
  for (uint16_t y = 0; y < sourceHeight; ++y) {
    for (uint16_t x = 0; x < sourceWidth; ++x) {
      if (!isWhite(x, y)) continue;
      source[static_cast<size_t>(y) * layout.sourceRowBytes + x / 8u] |=
          static_cast<uint8_t>(1u << (7u - (x % 8u)));
    }
  }

  std::vector<uint8_t> output(layout.bmpRowBytes * targetHeight, 0);
  xtc_thumb::OnePlaneReducer reducer(sourceWidth, sourceHeight, targetWidth, targetHeight, inverseScaleFp,
                                     layout, xMap.data(), yMap.data(), xCounts.data(), yCounts.data(),
                                     whiteSums.data(), outputRow.data());
  ASSERT_TRUE(reducer.initialize());
  const auto emitRow = [&output, &layout](const uint8_t* row, const size_t bytes, const uint16_t y) {
    if (bytes != layout.bmpRowBytes || y >= 8) return false;
    std::copy_n(row, bytes, output.data() + static_cast<size_t>(y) * bytes);
    return true;
  };
  for (size_t offset = 0; offset < source.size(); offset += 7) {
    const size_t count = std::min<size_t>(7, source.size() - offset);
    ASSERT_TRUE(reducer.addChunk(source.data() + offset, count, offset, emitRow));
  }
  ASSERT_TRUE(reducer.finish(emitRow));

  std::vector<uint8_t> expected(layout.bmpRowBytes * targetHeight, 0xFF);
  for (uint16_t y = 0; y < targetHeight; ++y) {
    for (uint16_t x = 0; x < targetWidth; ++x) {
      const auto xr = xtc_thumb::sourceRange(x, sourceWidth, inverseScaleFp);
      const auto yr = xtc_thumb::sourceRange(y, sourceHeight, inverseScaleFp);
      uint32_t sum = 0, count = 0;
      for (uint32_t sy = yr.begin; sy < yr.end; ++sy) {
        for (uint32_t sx = xr.begin; sx < xr.end; ++sx) {
          sum += isWhite(static_cast<uint16_t>(sx), static_cast<uint16_t>(sy)) ? 255u : 0u;
          ++count;
        }
      }
      const uint8_t gray = count ? static_cast<uint8_t>(sum / count) : 255;
      const size_t byteIndex = static_cast<size_t>(y) * layout.bmpRowBytes + x / 8u;
      const uint8_t mask = static_cast<uint8_t>(1u << (7u - (x % 8u)));
      if (gray >= xtc_thumb::onePlaneDitherThreshold(x, y)) expected[byteIndex] |= mask;
      else expected[byteIndex] &= static_cast<uint8_t>(~mask);
    }
  }
  EXPECT_EQ(output, expected);
}

TEST(XtcThumbnailStream, RejectsOutOfOrderOnePlaneChunksAndTruncatedPages) {
  xtc_thumb::OnePlaneWorkspaceLayout layout;
  ASSERT_TRUE(xtc_thumb::makeOnePlaneWorkspaceLayout(8, 8, 4, 4, 2u << 16, layout));
  std::vector<uint16_t> xMap(8), yMap(8), xCounts(4), yCounts(4);
  std::vector<uint32_t> sums(4);
  std::vector<uint8_t> row(layout.outputRowBytes), chunk(layout.pageBytes, 0xFF);
  xtc_thumb::OnePlaneReducer reducer(8, 8, 4, 4, 2u << 16, layout, xMap.data(), yMap.data(),
                                     xCounts.data(), yCounts.data(), sums.data(), row.data());
  ASSERT_TRUE(reducer.initialize());
  const auto ignoreRow = [](const uint8_t*, size_t, uint16_t) { return true; };
  EXPECT_FALSE(reducer.addChunk(chunk.data(), 1, 1, ignoreRow));

  xtc_thumb::OnePlaneReducer truncated(8, 8, 4, 4, 2u << 16, layout, xMap.data(), yMap.data(),
                                       xCounts.data(), yCounts.data(), sums.data(), row.data());
  ASSERT_TRUE(truncated.initialize());
  ASSERT_TRUE(truncated.addChunk(chunk.data(), 2, 0, ignoreRow));
  EXPECT_FALSE(truncated.finish(ignoreRow));
}

TEST(XtcCoverStream, PreservesAllFourGrayLevelsWithArbitraryChunkBoundaries) {
  constexpr uint16_t width = 5;
  constexpr uint16_t height = 80;
  constexpr size_t planeBytes = (width * height + 7u) / 8u;
  constexpr size_t columnBytes = height / 8u;
  constexpr size_t bmpRowBytes = 4;
  constexpr size_t stripBytes = bmpRowBytes * 8u * xtc_cover::kMaxBandGroups;
  constexpr size_t bandPlaneBytes = width * xtc_cover::kMaxBandGroups;
  std::vector<uint8_t> source(planeBytes * 2u, 0);
  const auto pixelAt = [](uint16_t x, uint16_t y) { return static_cast<uint8_t>((x + 2u * y) % 4u); };
  for (uint16_t y = 0; y < height; ++y) {
    for (uint16_t x = 0; x < width; ++x) {
      const uint8_t pixel = pixelAt(x, y);
      const size_t offset = (width - 1u - x) * columnBytes + y / 8u;
      const uint8_t mask = static_cast<uint8_t>(1u << (7u - (y % 8u)));
      if (pixel & 2u) source[offset] |= mask;
      if (pixel & 1u) source[planeBytes + offset] |= mask;
    }
  }

  for (uint16_t firstGroup = 0; firstGroup < height / 8u; firstGroup += xtc_cover::kMaxBandGroups) {
    std::vector<uint8_t> firstColumns(bandPlaneBytes);
    std::vector<uint8_t> rows(stripBytes);
    xtc_cover::TwoBitCoverStripAssembler assembler(width, height, planeBytes, columnBytes,
                                                   bmpRowBytes, stripBytes, firstColumns.data(), rows.data());
    ASSERT_TRUE(assembler.begin(firstGroup));
    for (size_t offset = 0; offset < source.size(); offset += 3) {
      const size_t count = std::min<size_t>(3, source.size() - offset);
      ASSERT_TRUE(assembler.addChunk(source.data() + offset, count, offset));
    }
    ASSERT_TRUE(assembler.finish());
    for (uint16_t row = 0; row < assembler.rowCount(); ++row) {
      for (uint16_t x = 0; x < width; ++x) {
        const size_t byteIndex = static_cast<size_t>(row) * bmpRowBytes + x / 4u;
        const uint8_t shift = static_cast<uint8_t>(6u - (x % 4u) * 2u);
        const uint8_t pixel = static_cast<uint8_t>((rows[byteIndex] >> shift) & 0x03u);
        const uint16_t sourceY = static_cast<uint16_t>(assembler.firstRow() + row);
        const uint8_t sourcePixel = pixelAt(x, sourceY);
        const uint8_t expectedPaletteIndex = sourcePixel == 0 ? 3 : (sourcePixel == 3 ? 0 : sourcePixel);
        EXPECT_EQ(pixel, expectedPaletteIndex);
      }
      EXPECT_EQ(rows[static_cast<size_t>(row) * bmpRowBytes + 2u], 0u);
      EXPECT_EQ(rows[static_cast<size_t>(row) * bmpRowBytes + 3u], 0u);
    }
  }
}

TEST(XtcCoverStream, TwoBitBmpHeaderDefinesTheNativeGrayPaletteAndTopDownRows) {
  xtc_cover::TwoBitBmpHeader header{};
  ASSERT_TRUE(xtc_cover::makeTwoBitBmpHeader(5, 80, 4, header));

  EXPECT_EQ(header.bfType, 0x4D42u);
  EXPECT_EQ(header.bfSize, 70u + 4u * 80u);
  EXPECT_EQ(header.bfOffBits, 70u);
  EXPECT_EQ(header.biWidth, 5);
  EXPECT_EQ(header.biHeight, -80);
  EXPECT_EQ(header.biPlanes, 1u);
  EXPECT_EQ(header.biBitCount, 2u);
  EXPECT_EQ(header.biCompression, 0u);
  EXPECT_EQ(header.biSizeImage, 4u * 80u);
  EXPECT_EQ(header.biClrUsed, 4u);
  EXPECT_EQ(header.biClrImportant, 4u);

  // BMP entries are black-to-white, while XTH values are 0=white, 1=dark,
  // 2=light, 3=black. These entries preserve all four native display levels.
  constexpr uint8_t expectedGray[4] = {0, 85, 170, 255};
  for (size_t index = 0; index < 4; ++index) {
    EXPECT_EQ(header.palette[index * 4u + 0u], expectedGray[index]);
    EXPECT_EQ(header.palette[index * 4u + 1u], expectedGray[index]);
    EXPECT_EQ(header.palette[index * 4u + 2u], expectedGray[index]);
    EXPECT_EQ(header.palette[index * 4u + 3u], 0u);
  }
  EXPECT_FALSE(xtc_cover::makeTwoBitBmpHeader(0, 80, 4, header));
}

TEST(XtcCoverStream, RejectsUnsupportedOddHeightWithoutOverflowingPlaneLayout) {
  size_t plane = 0, column = 0, bandPlane = 0, row = 0, strip = 0, total = 0;
  EXPECT_FALSE(xtc_cover::makeTwoBitCoverWorkspace(480, 801, plane, column, bandPlane, row, strip, total));
  EXPECT_TRUE(xtc_cover::makeTwoBitCoverWorkspace(480, 800, plane, column, bandPlane, row, strip, total));
  EXPECT_EQ(plane, 480u * 800u / 8u);
  EXPECT_EQ(column, 100u);
  EXPECT_EQ(bandPlane, 480u * xtc_cover::kMaxBandGroups);
  EXPECT_EQ(row, 120u);
  EXPECT_EQ(strip, 120u * 8u * xtc_cover::kMaxBandGroups);
  EXPECT_EQ(total, bandPlane + strip);
}
