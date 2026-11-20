"""Keep the experimental raw HID capture build on the SPI-array EPUB source."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")


def environment_body(name: str) -> str:
    marker = f"[env:{name}]"
    start = PLATFORMIO.index(marker) + len(marker)
    end = PLATFORMIO.find("\n[", start)
    return PLATFORMIO[start:] if end < 0 else PLATFORMIO[start:end]


class BleHidDiagnosticProfileContract(unittest.TestCase):
    def test_raw_hid_capture_profile_inherits_spi_array_page_aware_build(self):
        body = environment_body("gh_release_spiarray_ble_epub_hidraw_20261111_01")
        self.assertIn("extends = env:gh_release_spiarray_ble_epub_pageaware_20261106_01", body)
        self.assertIn("-DNOOIR_BLE_DIAGNOSTICS=1", body)
        self.assertIn("-DFREEINK_BLE_HID_RAW_DIAGNOSTICS=1", body)
        self.assertIn("-DFREEINK_BLE_HID_REPORT_DEBUG=1", body)
        self.assertNotIn("NOOIR_HEAP_LIFETIME_TRACE=1", body)

    def test_preserved_page_aware_profile_is_not_redefined_or_overwritten(self):
        body = environment_body("gh_release_spiarray_ble_epub_pageaware_20261106_01")
        self.assertIn("extends = env:gh_release_spiarray_ble_resourcediag_02", body)
        self.assertIn("-DNOOIR_EPUB_BLE_PAGE_AWARE_EXPERIMENT=1", body)


if __name__ == "__main__":
    unittest.main()
