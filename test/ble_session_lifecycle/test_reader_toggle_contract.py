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


if __name__ == "__main__":
    unittest.main()
