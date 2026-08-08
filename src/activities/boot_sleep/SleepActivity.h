#pragma once
#include <string>
#include <vector>

#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  bool renderCustomImage(const std::string& path) const;
  void renderCoverSleepScreen(const std::string& bookPath = {}, const std::string& overlayPath = {},
                             const std::string& clippingText = {}, const std::string& clippingTitle = {},
                             uint16_t clippingPage = 0) const;
  void renderCoverOverlaySleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalStatsSleepScreen() const;
  void renderClippingCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderBitmapSleepScreenWithOverlay(const Bitmap& bitmap, const std::string& overlayPath) const;
  void renderBitmapSleepScreenWithClipping(const Bitmap& bitmap, const std::string& text, const std::string& title,
                                           uint16_t page) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  bool renderOverlaySleepScreen() const;
  std::vector<std::string> findOverlayPngCandidates() const;
  std::string findOverlayPngPath() const;
  bool renderOverlayPngPass(const std::string& path, GfxRenderer::RenderMode mode, bool* transparencyDetected) const;
  bool renderOverlayPng(const std::string& path, bool allowOpaque = false) const;

  bool fromTimeout = false;
};
