"""Contracts for reader-scoped Bluetooth controls.

These checks keep reader shortcuts and menu controls on the temporary session
preference, leaving the user's saved Bluetooth setting unchanged.
"""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
BLE_SOURCE = (REPO_ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
BLE_SETTINGS = (REPO_ROOT / "src/activities/settings/BluetoothSettingsActivity.cpp").read_text(
    encoding="utf-8"
)
ACTIVITY_MANAGER = (REPO_ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
MAIN = (REPO_ROOT / "src/main.cpp").read_text(encoding="utf-8")
XTC_READER = (REPO_ROOT / "src/activities/reader/XtcReaderActivity.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unclosed function body: {signature}")


class ReaderBluetoothToggleContract(unittest.TestCase):
    def test_xtc_reader_controls_are_session_scoped(self):
        self.assertNotIn("bleinput::toggleBluetooth()", XTC_READER)
        self.assertNotIn("bleinput::setBluetoothEnabled", XTC_READER)
        self.assertEqual(XTC_READER.count("bleinput::toggleReaderSessionBluetooth()"), 4)
        self.assertIn("bleinput::beginReaderBluetoothSession();", XTC_READER)
        self.assertIn("bleinput::endReaderBluetoothSession();", XTC_READER)

    def test_disabling_reader_bluetooth_cancels_return_before_stop(self):
        body = function_body(BLE_SOURCE, "void setReaderSessionBluetoothEnabled(")
        disabled = body[body.index("if (!enabled)") : body.index("} else {")]
        self.assertLess(disabled.index("clearReaderBleReturnPending();"),
                        disabled.index("requestLifecycleReevaluation();"))
        self.assertLess(disabled.index("requestLifecycleReevaluation();"), disabled.index("stop();"))
        self.assertNotIn("SETTINGS.bluetoothEnabled =", body)

    def test_bluetooth_settings_redraw_reads_the_current_reader_session_value(self):
        body = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::handleMenuConfirm()")
        self.assertIn("bleinput::setReaderSessionBluetoothEnabled(enabled);", body)
        render = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::render(")
        self.assertIn("bleinput::readerSessionBluetoothEnabled()", render)

    def test_failed_bluetooth_activation_waits_for_explicit_retry(self):
        render = function_body(ACTIVITY_MANAGER, "void ActivityManager::renderTaskLoop()")
        failure_gate = render.index("if (!bluetoothSettingsActivationFailed_.load")
        recheck = render.index("if (bluetoothSettingsActivationFailed_.load", failure_gate)
        self.assertLess(failure_gate, render.index("bleinput::requestLifecycleReevaluation();", failure_gate))
        self.assertIn("bleinput::clearReAdmissionPending();", render[recheck:])
        self.assertIn("bleinput::takeLifecycleReevaluation();", render[recheck:])

        lifecycle = function_body(MAIN, "void updateBluetoothLifecycle()")
        failure_gate = lifecycle.index("activityManager.bluetoothSettingsActivationFailed()")
        self.assertLess(failure_gate, lifecycle.index("bleinput::takeLifecycleReevaluation();", failure_gate))
        self.assertLess(failure_gate, lifecycle.index("bleinput::connectionAdmissionAllowed();", failure_gate))
        self.assertIn("bleinput::clearReAdmissionPending();", lifecycle[failure_gate:])

        preparation = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::beginPreparation(")
        self.assertIn("activityManager.setBluetoothSettingsActivationFailed(false);", preparation)
        result = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::onBluetoothActivationResult(")
        self.assertIn("activityManager.setBluetoothSettingsActivationFailed(!started);", result)

    def test_bluetooth_off_remains_available_after_activation_failure(self):
        loop = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::loop()")
        failed = loop[loop.index("if (activationState.load(std::memory_order_acquire) == ActivationState::Failed)") :]
        self.assertIn("const int optionCount = retryAvailable ? 3 : 2;", failed)
        self.assertIn("if (mode == Mode::ReaderSession)", failed)
        self.assertIn("bleinput::setReaderSessionBluetoothEnabled(false);", failed)
        self.assertIn("bleinput::setBluetoothEnabled(false);", failed)
        self.assertIn("activationState.store(ActivationState::Ready", failed)

        render = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::render(")
        failed_render = render[render.index("if (state == ActivationState::Failed)") :]
        self.assertIn("tr(STR_STATE_OFF)", failed_render)

    def test_bluetooth_off_remains_available_while_activation_is_pending(self):
        loop = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::loop()")
        preparing = loop[
            loop.index("if (activationState.load(std::memory_order_acquire) == ActivationState::Preparing)") :
            loop.index("if (activationState.load(std::memory_order_acquire) == ActivationState::Failed)")
        ]
        self.assertIn("handleListTouch(selectedOption, 2", preparing)
        self.assertIn("bleinput::setReaderSessionBluetoothEnabled(false);", preparing)
        self.assertIn("bleinput::setBluetoothEnabled(false);", preparing)

        render = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::render(")
        preparing_render = render[
            render.index("if (state == ActivationState::Preparing)") :
            render.index("if (state == ActivationState::Failed)")
        ]
        self.assertIn("GUI.drawList(renderer, listRect, 2", preparing_render)
        self.assertIn("tr(STR_STATE_OFF)", preparing_render)

    def test_failed_manual_scan_start_preserves_saved_bluetooth_preference(self):
        scan = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::startScanView()")
        failure = scan[scan.index("if (!BleHid.isRunning() && !bleinput::ensureStarted())") :]
        self.assertNotIn("SETTINGS.bluetoothEnabled = 0", failure)
        self.assertNotIn("SETTINGS.saveToFile()", failure)
        self.assertIn("setBanner(tr(STR_BT_START_FAILED));", failure)

    def test_no_device_timeout_stops_runtime_without_erasing_saved_preference(self):
        timeout = function_body(BLE_SOURCE, "void updateNoDeviceWindow(")
        self.assertIn("suppressNoDeviceRetryUntilLifecycleBoundary();", timeout)
        self.assertIn("requestLifecycleReevaluation();", timeout)
        self.assertNotIn("SETTINGS.bluetoothEnabled = 0", timeout)
        self.assertNotIn("SETTINGS.saveToFile()", timeout)
        self.assertIn("g_noDeviceTimeoutStoppedHost.store(true", BLE_SOURCE)
        self.assertIn("lifecycleReevaluationAllowed(", BLE_SOURCE)
        reevaluate = function_body(BLE_SOURCE, "void requestLifecycleReevaluation()")
        self.assertIn("portENTER_CRITICAL(&g_noDeviceTimeoutMux);", reevaluate)
        self.assertIn("portEXIT_CRITICAL(&g_noDeviceTimeoutMux);", reevaluate)
        self.assertNotIn("currentActivityName()", BLE_SOURCE[: BLE_SOURCE.index("void updateNoDeviceWindow(")])
        self.assertIn("bleinput::clearNoDeviceTimeoutSuppression();", BLE_SETTINGS)

    def test_committed_activity_changes_clear_timeout_pause_before_reentry(self):
        starts = []
        offset = 0
        while True:
            offset = ACTIVITY_MANAGER.find("currentActivity = std::move(", offset)
            if offset < 0:
                break
            starts.append(offset)
            offset += 1
        self.assertEqual(len(starts), 3)
        for start in starts:
            self.assertIn("bleinput::clearNoDeviceTimeoutSuppression();",
                          ACTIVITY_MANAGER[start : start + 220])
        self.assertIn("bleinput::requestLifecycleReevaluation();", ACTIVITY_MANAGER)


if __name__ == "__main__":
    unittest.main()
