#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  bool prepareRenderPath();
  void doSetSleepCover();

  std::string filePath;
  std::string renderPath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
