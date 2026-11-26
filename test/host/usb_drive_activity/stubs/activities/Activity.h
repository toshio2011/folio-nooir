#pragma once
#include <string>
#include <utility>
#include <GfxRenderer.h>
#include <MappedInputManager.h>
#include <RenderLock.h>
#include <test_state.h>
class Activity {
 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
 public:
  Activity(std::string activityName,GfxRenderer& graphics,MappedInputManager& input)
      : name(std::move(activityName)),renderer(graphics),mappedInput(input) {}
  virtual ~Activity()=default;
  virtual void onEnter() { usb_drive_test::record("activity.enter"); }
  virtual void onExit() { usb_drive_test::record("activity.exit"); }
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  virtual bool preventAutoSleep() { return false; }
  virtual bool requiresExclusiveStorageLoop() const { return false; }
  void requestUpdate(bool=false) { usb_drive_test::record("activity.update"); }
  void requestUpdateAndWait() { usb_drive_test::record("activity.render.wait"); }
};
