"""Source contract for bounded BLE connection-stage diagnostics."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BLE_PATCH = (ROOT / "diagnostics/ble/ble-connect-timing.patch").read_text(encoding="utf-8")
BLE_INPUT = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")


class BleConnectionTimingContract(unittest.TestCase):
    def test_patch_timestamps_connection_stages_and_logs_only_first_advertisements(self):
        for stage in (
            "scan_start_pre",
            "first_hid_advert",
            "target_advert",
            "connect_failure",
            "link_connected",
            "hid_service_found",
            "security_ready",
            "hid_input_ready",
        ):
            self.assertIn(stage, BLE_PATCH)
        self.assertIn("static_cast<unsigned long>(millis())", BLE_PATCH)
        self.assertIn("!g_firstHidAdvertisementLogged", BLE_PATCH)
        self.assertIn("!g_targetAdvertisementLogged", BLE_PATCH)
        self.assertIn('lifecycleStage("first_hid_advert", rssi)', BLE_PATCH)
        self.assertIn('lifecycleStage("target_advert", rssi)', BLE_PATCH)

    def test_app_timestamps_request_and_terminal_no_device_timeout(self):
        self.assertIn('"stage=connection_request ms=%lu', BLE_INPUT)
        self.assertIn('"stage=no_device_timeout ms=%lu', BLE_INPUT)
        self.assertIn('"stage=connect_failure ms=%lu', BLE_INPUT)


if __name__ == "__main__":
    unittest.main()
