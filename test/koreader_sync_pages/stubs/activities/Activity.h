#pragma once

#include <string>

#include "GfxRenderer.h"
#include "MappedInputManager.h"

struct RenderLock {};

class Activity {
 public:
  Activity(const char*, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  virtual bool preventAutoSleep() { return false; }

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
};
