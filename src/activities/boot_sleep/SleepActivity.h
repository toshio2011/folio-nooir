#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/SleepComposer.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  struct PreviewMode {
    uint8_t value;
  };

  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const PreviewMode previewMode)
      : Activity("Sleep", renderer, mappedInput), previewMode(previewMode.value), preview(true) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return preview; }
  bool acceptsBluetoothInput() const override { return false; }
  bool bluetoothResourceSensitive() const override { return true; }

 private:
  void renderDefaultSleepScreen() const;
  void renderSelectedSleepScreen() const;
  void renderPreviewScreen();
  void renderCustomSleepScreen() const;
  bool renderCustomSleepScreenAt(const SleepComposerRect* bounds, std::string* selectedPath) const;
  bool drawCustomImageInBounds(const std::string& path, const SleepComposerRect& bounds,
                               std::string* reusablePath = nullptr) const;
  bool drawSleepBookCoverInBounds(const std::string& coverPath, const SleepComposerRect& bounds,
                                  bool fullFrame) const;
  void renderFlexibleSleepScreen() const;
  bool renderCustomImage(const std::string& path) const;
  void renderCoverSleepScreen(const std::string& bookPath = {}, const std::string& overlayPath = {},
                             const std::string& clippingText = {}, const std::string& clippingTitle = {},
                             uint16_t clippingPage = 0, bool showBookProgress = false) const;
  void renderCoverOverlaySleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalStatsSleepScreen() const;
  void renderReadingCalendarSleepScreen() const;
  void renderReadingSummarySleepScreen() const;
  void renderClippingCoverSleepScreen() const;
  void renderToDoSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderBitmapSleepScreenWithOverlay(const Bitmap& bitmap, const std::string& overlayPath) const;
  void renderBitmapSleepScreenWithClipping(const Bitmap& bitmap, const std::string& text, const std::string& title,
                                           uint16_t page) const;
  void renderBitmapSleepScreenWithBookProgress(const Bitmap& bitmap, const std::string& title,
                                               uint8_t progress, uint32_t readingSeconds, uint16_t sessions) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  uint8_t effectiveSleepScreen = 0;
  bool currentBackgroundPreserved = false;
  bool renderOverlaySleepScreen() const;
  std::vector<std::string> findOverlayPngCandidates() const;
  std::string findOverlayPngPath() const;
  bool renderOverlayPngPass(const std::string& path, GfxRenderer::RenderMode mode, bool* transparencyDetected,
                            bool drawPixels = true, const SleepComposerRect* destination = nullptr) const;
  bool renderOverlayPngInBounds(const std::string& path, const SleepComposerRect& destination) const;
  bool renderOverlayPng(const std::string& path, bool allowOpaque = false) const;

  bool fromTimeout = false;
  uint8_t previewMode = 0;
  bool preview = false;
  bool ignoreInitialConfirm = false;
  bool previewPreviousDarkMode = false;
};
