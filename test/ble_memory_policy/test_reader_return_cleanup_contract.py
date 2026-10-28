"""Guard the quiescent Reader cleanup and Home readmission boundaries."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
READER = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
READER_H = (ROOT / "src/activities/reader/EpubReaderActivity.h").read_text(encoding="utf-8")
FONT_CACHE = (ROOT / "lib/GfxRenderer/FontCacheManager.cpp").read_text(encoding="utf-8")
BLE_INPUT = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")


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


class ReaderReturnCleanupContract(unittest.TestCase):
    def test_reader_scan_storage_is_freed_only_when_scan_mode_is_idle(self):
        body = function_body(FONT_CACHE, "size_t FontCacheManager::releaseReaderScanStorage()")
        self.assertIn("if (scanMode_ != ScanMode::None) return 0", body)
        self.assertIn("entry.text.swap(empty)", body)
        self.assertNotIn("scanTextByteLimit_ =", body)
        self.assertNotIn("setPageGlyphLimit", body)

    def test_quiescent_ble_cleanup_reclaims_scan_capacity_before_gray_scratch(self):
        body = function_body(READER, "void EpubReaderActivity::runBleReturnCleanupIfNeeded()")
        self.assertIn("releaseReaderScanStorage()", body)
        self.assertLess(body.index("releaseReaderScanStorage()"), body.index("grayscale_strip_scratch"))
        self.assertIn('admitReaderBleIfReady(1, "reader_scan_capacity"', body)

    def test_reader_entry_reserves_grayscale_scratch_only_for_text_aa(self):
        body = function_body(READER, "void EpubReaderActivity::onEnter()")
        self.assertIn("renderer.supportsStripGrayscale() && SETTINGS.textAntiAliasing != 0", body)

    def test_exit_requests_ble_recheck_and_cold_home_start_waits_for_render(self):
        exit_body = function_body(READER, "void EpubReaderActivity::requestExitToHome(")
        self.assertIn("bleinput::requestLifecycleReevaluation()", exit_body)
        admission_body = function_body(BLE_INPUT, "bool connectionAdmissionAllowed()")
        self.assertLess(admission_body.index("if (BleHid.isRunning()) return true"),
                        admission_body.index("homeBleColdStartRenderWorkSafe"))
        self.assertIn("activityManager.hasPendingRenderWork()", admission_body)
        self.assertIn("exitHomeRequestedAtMs", READER_H)


if __name__ == "__main__":
    unittest.main()
