#include "activities/network/UsbDriveActivity.h"
#include <Arduino.h>
#include <HalStorage.h>
#include <I18n.h>
#include <SdCardFontSystem.h>
#include <test_state.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
TestStorage Storage;
TestSdCardFontSystem sdFontSystem;
TestI18n I18N;
TestGui GUI;
static void reset() { usb_drive_test::resetTrace(); Storage=TestStorage{}; sdFontSystem=TestSdCardFontSystem{}; }
static size_t eventIndex(const std::string& event) {
  const auto it=std::find(usb_drive_test::events.begin(),usb_drive_test::events.end(),event);
  assert(it!=usb_drive_test::events.end());
  return static_cast<size_t>(it-usb_drive_test::events.begin());
}
static void testSetupOrdersFontReleaseBeforeExclusiveStorage() {
  reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
  activity.onEnter();
  assert(sdFontSystem.releaseCalls==1 && Storage.beginCalls==1);
  assert(eventIndex("fonts.release")<eventIndex("activity.render.wait"));
  assert(eventIndex("activity.render.wait")<eventIndex("storage.begin"));
  assert(activity.requiresExclusiveStorageLoop() && !activity.preventAutoSleep());
  activity.onExit(); assert(Storage.endCalls==1);
}
static void testPowerBackAndHomeExitWaitingStateOnce() {
  for(int trigger=0;trigger<3;++trigger) {
    reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
    activity.onEnter(); input.back=(trigger==0); input.power=(trigger==1); input.home=(trigger==2);
    activity.loop(); activity.loop();
    assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
    assert(eventIndex("storage.end")<eventIndex("restart.home") && usb_drive_test::nowMs==20);
    activity.onExit(); assert(Storage.endCalls==1);
  }
}
static void testConnectedStatePreventsSleepAndIgnoresExitKeys() {
  reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
  activity.onEnter(); Storage.state=UsbDriveState::Connected; input.power=true; activity.loop();
  assert(activity.preventAutoSleep() && Storage.endCalls==0 && usb_drive_test::restartCalls==0);
}
static void testWaitingTimeoutRestartsAtFiveMinutes() {
  reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
  activity.onEnter(); usb_drive_test::nowMs=299999; activity.loop(); assert(Storage.endCalls==0);
  usb_drive_test::nowMs=300000; activity.loop();
  assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
}
static void testStartFailureHasRetryWindowAndPowerExit() {
  reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
  Storage.beginResult=false; activity.onEnter(); usb_drive_test::nowMs=29999; activity.loop(); assert(Storage.endCalls==0);
  usb_drive_test::nowMs=30000; activity.loop(); assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
  reset(); GfxRenderer renderer2; MappedInputManager input2; UsbDriveActivity activity2(renderer2,input2);
  Storage.beginResult=false; activity2.onEnter(); input2.power=true; activity2.loop();
  assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
}
static void testSuspendedHostTimeoutRestartsAtTwoSeconds() {
  reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
  activity.onEnter(); Storage.state=UsbDriveState::Connected; Storage.suspended=true;
  usb_drive_test::nowMs=100; activity.loop(); usb_drive_test::nowMs=2099; activity.loop(); assert(Storage.endCalls==0);
  usb_drive_test::nowMs=2100; activity.loop(); assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
}
static void testIoErrorRequestsDisconnectThenTimesOut() {
  reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
  activity.onEnter(); Storage.state=UsbDriveState::IoError; Storage.disconnectResult=false; activity.loop();
  assert(Storage.disconnectCalls==1 && Storage.endCalls==0);
  usb_drive_test::nowMs=999; activity.loop(); assert(Storage.endCalls==0);
  usb_drive_test::nowMs=1000; activity.loop(); assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
}
static void testTerminalStorageStatesRestartImmediately() {
  for(const auto state:{UsbDriveState::Ejected,UsbDriveState::Disconnected,UsbDriveState::Unsupported}) {
    reset(); GfxRenderer renderer; MappedInputManager input; UsbDriveActivity activity(renderer,input);
    activity.onEnter(); Storage.state=state; activity.loop();
    assert(Storage.endCalls==1 && usb_drive_test::restartCalls==1);
  }
}
int main() {
  testSetupOrdersFontReleaseBeforeExclusiveStorage();
  testPowerBackAndHomeExitWaitingStateOnce();
  testConnectedStatePreventsSleepAndIgnoresExitKeys();
  testWaitingTimeoutRestartsAtFiveMinutes();
  testStartFailureHasRetryWindowAndPowerExit();
  testSuspendedHostTimeoutRestartsAtTwoSeconds();
  testIoErrorRequestsDisconnectThenTimesOut();
  testTerminalStorageStatesRestartImmediately();
  std::cout<<"Nooir USB Drive activity handoff, Power/Back/Home exit, timeout, eject, and disconnect tests passed\n";
}
