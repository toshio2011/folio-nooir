#include "UsbDriveActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "components/UITheme.h"

void UsbDriveActivity::onEnter() {
  Activity::onEnter();
  preparing = true;
  startFailed = false;
  restartRequested = false;
  forcedDisconnectRequested = false;
  hostSuspendPending = false;

  // Release SD-backed font handles and finish the warning render before the
  // filesystem is unmounted and the USB host receives raw-sector access.
  {
    RenderLock lock;
    sdFontSystem.releaseForStorage(renderer);
  }
  requestUpdateAndWait();

  if (!Storage.beginUsbDrive()) {
    LOG_ERR("USB", "Unable to start USB Drive");
    preparing = false;
    startFailed = true;
    state = UsbDriveState::IoError;
    startFailureStartedAt = millis();
    requestUpdate();
    return;
  }

  preparing = false;
  state = UsbDriveState::WaitingForHost;
  hostWaitStartedAt = millis();
  requestUpdate();
}

void UsbDriveActivity::onExit() {
  if (!restartRequested) Storage.endUsbDrive();
  Activity::onExit();
}

bool UsbDriveActivity::preventAutoSleep() {
  return state == UsbDriveState::Connected || (!startFailed && state == UsbDriveState::IoError);
}

void UsbDriveActivity::loop() {
  if (!startFailed) {
    const UsbDriveState nextState = Storage.usbDriveState();
    if (nextState != state) {
      state = nextState;
      requestUpdate();
    }
  }

  // S3 USB OTG may not have a routed VBUS sense input. A sustained suspend is
  // therefore treated as a disconnect hint, with a delay to tolerate brief idle.
  if (state == UsbDriveState::Connected && Storage.usbDriveHostSuspended()) {
    if (!hostSuspendPending) {
      hostSuspendPending = true;
      hostSuspendStartedAt = millis();
    } else if (millis() - hostSuspendStartedAt >= HOST_SUSPEND_TIMEOUT_MS) {
      LOG_INF("USB", "USB Drive host suspend timed out; ending session");
      restartAfterStorageHandoff();
      return;
    }
  } else {
    hostSuspendPending = false;
  }

  if (state == UsbDriveState::WaitingForHost && millis() - hostWaitStartedAt >= HOST_WAIT_TIMEOUT_MS) {
    LOG_INF("USB", "USB Drive host wait timed out");
    restartAfterStorageHandoff();
    return;
  }

  if (startFailed && millis() - startFailureStartedAt >= START_FAILURE_TIMEOUT_MS) {
    LOG_INF("USB", "USB Drive startup failure timed out");
    restartAfterStorageHandoff();
    return;
  }

  if (!startFailed && state == UsbDriveState::IoError) {
    if (!forcedDisconnectRequested) {
      forcedDisconnectRequested = true;
      forcedDisconnectRequestedAt = millis();
      LOG_ERR("USB", "USB Drive I/O error; requesting host disconnect");
      if (!Storage.disconnectUsbDriveHost()) {
        LOG_ERR("USB", "Unable to request USB Drive host disconnect");
      }
    } else if (millis() - forcedDisconnectRequestedAt >= FORCED_DISCONNECT_TIMEOUT_MS) {
      LOG_ERR("USB", "USB Drive host disconnect timed out; restarting");
      restartAfterStorageHandoff();
    }
    return;
  }

  const bool canExitWithInput = state == UsbDriveState::WaitingForHost || startFailed;
  if (canExitWithInput && (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Power) || mappedInput.wasHomeGesture())) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Power)) mappedInput.consumePowerGesture();
    restartAfterStorageHandoff();
    return;
  }

  if (state == UsbDriveState::Ejected || state == UsbDriveState::Disconnected ||
      state == UsbDriveState::Unsupported) {
    restartAfterStorageHandoff();
  }
}

void UsbDriveActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_USB_DRIVE));

  StrId message = StrId::STR_USB_DRIVE_WAITING;
  StrId detail = StrId::STR_USB_DRIVE_EJECT_HINT;
  if (preparing) {
    message = StrId::STR_USB_DRIVE_PREPARING;
  } else if (startFailed) {
    message = StrId::STR_USB_DRIVE_START_ERROR;
    detail = StrId::STR_NONE_OPT;
  } else {
    switch (state) {
      case UsbDriveState::WaitingForHost:
        message = StrId::STR_USB_DRIVE_WAITING;
        detail = StrId::STR_NONE_OPT;
        break;
      case UsbDriveState::Connected:
        message = StrId::STR_USB_DRIVE_CONNECTED;
        detail = StrId::STR_USB_DRIVE_EJECT_HINT;
        break;
      case UsbDriveState::IoError:
        message = StrId::STR_USB_DRIVE_ERROR;
        detail = StrId::STR_NONE_OPT;
        break;
      case UsbDriveState::Ejected:
      case UsbDriveState::Disconnected:
      case UsbDriveState::Unsupported:
        return;
    }
  }

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, 1, 0,
               [message](int) { return std::string(I18N.get(message)); },
               [detail](int) { return detail == StrId::STR_NONE_OPT ? std::string() : std::string(I18N.get(detail)); },
               [](int) { return UIIcon::Library; });

  if (state == UsbDriveState::WaitingForHost || startFailed) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}

void UsbDriveActivity::restartAfterStorageHandoff() {
  if (restartRequested) return;
  restartRequested = true;
  Storage.endUsbDrive();
  delay(20);
  restartToHomeAfterStorageHandoff();
}
