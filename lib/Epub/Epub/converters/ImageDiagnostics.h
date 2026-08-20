#pragma once

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Temporary, bounded diagnostics used by the CBZ image investigation. The
// pointers are null for normal EPUB/TXT/XTC rendering, so production output
// and the normal image pipeline remain unchanged when diagnostics are off.
struct ImageRenderDiagnostics {
  const char* decoder = nullptr;
  uint32_t decodeMs = 0;
  uint32_t resampleUs = 0;
  uint32_t pixelEmitUs = 0;
  uint32_t cacheWriteUs = 0;
  bool areaResampling = false;
  const char* resampleMode = "direct";
  // Bounded stage counters used by the CBZ quality investigation. The gray
  // range/bins are sampled at the converter output immediately before the
  // 4-level quantizer; quantizedBins are the exact logical levels sent to
  // both PixelCache and DirectPixelWriter. No pixel buffer is retained.
  uint8_t grayMin = 255;
  uint8_t grayMax = 0;
  uint32_t grayBins[4]{};
  uint32_t quantizedBins[4]{};
  uint32_t emittedPixels = 0;
};

// A 16x16 sampled comparison plus small histograms is enough to compare the
// grayscale values immediately before Bayer quantization with the final 2-bit
// levels, without retaining a full source or destination framebuffer.
struct ImageQualityProbe {
  static constexpr int GRID = 16;

  int width = 0;
  int height = 0;
  uint8_t grayMin = 255;
  uint8_t grayMax = 0;
  uint32_t grayBins[16]{};
  uint32_t levelBins[4]{};
  uint8_t graySamples[GRID * GRID]{};
  uint8_t levelSamples[GRID * GRID]{};
  bool sampleSet[GRID * GRID]{};

  void reset(const int w, const int h) {
    width = std::max(1, w);
    height = std::max(1, h);
    grayMin = 255;
    grayMax = 0;
    std::memset(grayBins, 0, sizeof(grayBins));
    std::memset(levelBins, 0, sizeof(levelBins));
    std::memset(graySamples, 0, sizeof(graySamples));
    std::memset(levelSamples, 0, sizeof(levelSamples));
    std::memset(sampleSet, 0, sizeof(sampleSet));
  }

  void record(const int localX, const int localY, const uint8_t gray, const uint8_t level) {
    if (gray < grayMin) grayMin = gray;
    if (gray > grayMax) grayMax = gray;
    grayBins[gray >> 4]++;
    levelBins[level > 3 ? 3 : level]++;
    if (localX < 0 || localY < 0 || width <= 0 || height <= 0) return;

    const int sx = std::min(GRID - 1, (localX * GRID) / width);
    const int sy = std::min(GRID - 1, (localY * GRID) / height);
    const int index = sy * GRID + sx;
    if (!sampleSet[index]) {
      sampleSet[index] = true;
      graySamples[index] = gray;
      levelSamples[index] = level;
    }
  }

  void logSummary(const char* sourcePath) const {
    uint64_t grayTotal = 0;
    uint64_t graySum = 0;
    for (int i = 0; i < 16; ++i) {
      grayTotal += grayBins[i];
      graySum += static_cast<uint64_t>(grayBins[i]) * static_cast<uint64_t>(i * 16 + 8);
    }
    const unsigned grayMean = grayTotal ? static_cast<unsigned>(graySum / grayTotal) : 0;

    char grayText[GRID * 3 + 1]{};
    char levelText[GRID * 2 + 1]{};
    size_t grayPos = 0;
    size_t levelPos = 0;
    for (int i = 0; i < GRID && grayPos + 3 < sizeof(grayText); ++i) {
      grayPos += static_cast<size_t>(std::snprintf(grayText + grayPos, sizeof(grayText) - grayPos, "%02X ",
                                                    graySamples[i]));
    }
    for (int i = 0; i < GRID && levelPos + 2 < sizeof(levelText); ++i) {
      levelPos += static_cast<size_t>(std::snprintf(levelText + levelPos, sizeof(levelText) - levelPos, "%u ",
                                                     static_cast<unsigned>(levelSamples[i])));
    }

    LOG_DBG("CBZQ", "path=%s gray[min=%u max=%u mean=%u bins=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u] levels=%u,%u,%u,%u",
            sourcePath ? sourcePath : "", static_cast<unsigned>(grayMin), static_cast<unsigned>(grayMax), grayMean,
            grayBins[0], grayBins[1], grayBins[2], grayBins[3], grayBins[4],
            grayBins[5], grayBins[6], grayBins[7], grayBins[8], grayBins[9], grayBins[10], grayBins[11], grayBins[12],
            grayBins[13], grayBins[14], grayBins[15], levelBins[0], levelBins[1], levelBins[2], levelBins[3]);
    LOG_DBG("CBZQ", "path=%s sample_gray_row0=%s sample_level_row0=%s", sourcePath ? sourcePath : "", grayText,
            levelText);
  }
};

struct ScopedImagePixelTimer {
  ImageRenderDiagnostics* diagnostics = nullptr;
  uint32_t startedUs = 0;

  explicit ScopedImagePixelTimer(ImageRenderDiagnostics* value) : diagnostics(value) {
    if (diagnostics) startedUs = micros();
  }

  ~ScopedImagePixelTimer() {
    if (diagnostics) diagnostics->pixelEmitUs += micros() - startedUs;
  }
};
