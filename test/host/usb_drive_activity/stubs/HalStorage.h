#pragma once
#include "test_state.h"
enum class UsbDriveState { WaitingForHost, Connected, IoError, Ejected, Disconnected, Unsupported };
class TestStorage {
 public:
  bool beginResult=true, disconnectResult=true, suspended=false;
  UsbDriveState state=UsbDriveState::WaitingForHost;
  int beginCalls=0, endCalls=0, disconnectCalls=0;
  bool beginUsbDrive() { ++beginCalls; usb_drive_test::record("storage.begin"); return beginResult; }
  void endUsbDrive() { ++endCalls; usb_drive_test::record("storage.end"); }
  UsbDriveState usbDriveState() const { return state; }
  bool usbDriveHostSuspended() const { return suspended; }
  bool disconnectUsbDriveHost() { ++disconnectCalls; usb_drive_test::record("storage.disconnect"); return disconnectResult; }
};
extern TestStorage Storage;
