#pragma once
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
  void renderCoverSleepScreen(const std::string& bookPath = {}) const;
  void renderCoverOverlaySleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalStatsSleepScreen() const;
  void renderClippingCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  bool renderOverlaySleepScreen() const;
  bool renderOverlayPng(const std::string& path) const;

  bool fromTimeout = false;
};
