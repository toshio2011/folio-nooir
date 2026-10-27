"""Keep an established BLE host alive across the Bluetooth Settings child."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SETTINGS = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(encoding="utf-8")


class SettingsChildBleReuseContract(unittest.TestCase):
    def test_running_enabled_host_is_kept_and_parent_marked_stable(self):
        bluetooth_case = SETTINGS.split("case SettingAction::Bluetooth:", 1)[1].split(
            "case SettingAction::ClockWeatherSync:", 1
        )[0]
        self.assertIn("if (SETTINGS.bluetoothEnabled && BleHid.isRunning())", bluetooth_case)
        self.assertIn("markBluetoothAdmitted();", bluetooth_case)
        self.assertIn('armBluetoothReturn("BluetoothSettings");', bluetooth_case)
        self.assertLess(
            bluetooth_case.index("if (SETTINGS.bluetoothEnabled && BleHid.isRunning())"),
            bluetooth_case.index('armBluetoothReturn("BluetoothSettings");'),
        )
        admitted = SETTINGS.split("void SettingsActivity::markBluetoothAdmitted()", 1)[1].split(
            "void SettingsActivity::reclaimStableBluetoothTransientCaches()", 1
        )[0]
        self.assertIn("BluetoothLifecycleState::StableIdle", admitted)
        self.assertIn("bluetoothReturnArmed = false;", admitted)
        self.assertIn("bluetoothTransientFontCachesReleased = false;", admitted)

    def test_stopped_enabled_host_keeps_safe_readmission_path(self):
        bluetooth_case = SETTINGS.split("case SettingAction::Bluetooth:", 1)[1].split(
            "case SettingAction::ClockWeatherSync:", 1
        )[0]
        condition = bluetooth_case.index("if (SETTINGS.bluetoothEnabled && BleHid.isRunning())")
        fallback = bluetooth_case.index("} else {", condition)
        self.assertIn('armBluetoothReturn("BluetoothSettings");', bluetooth_case[fallback:])

    def test_cancelled_child_semantics_are_preserved(self):
        bluetooth_case = SETTINGS.split("case SettingAction::Bluetooth:", 1)[1].split(
            "case SettingAction::ClockWeatherSync:", 1
        )[0]
        cancelled = bluetooth_case.index("if (result.isCancelled)")
        save = bluetooth_case.index("SETTINGS.saveToFile();")
        self.assertLess(cancelled, save)
        self.assertIn("BluetoothLifecycleState::StableIdle", bluetooth_case[cancelled:save])
        self.assertIn("bleinput::clearReAdmissionPending();", bluetooth_case[cancelled:save])


if __name__ == "__main__":
    unittest.main()
