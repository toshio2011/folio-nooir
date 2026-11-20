import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HAL_STORAGE = (ROOT / "lib/hal/HalStorage.cpp").read_text(encoding="utf-8")
TIMING_HEADER = (ROOT / "lib/hal/NooirSdSpiDiagnostics.h").read_text(encoding="utf-8")
PATCH_SCRIPT = (ROOT / "scripts/patch_sdfat_sd_diagnostics.py").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")


class SdDiagnosticTimerContract(unittest.TestCase):
    def test_diagnostic_clock_uses_supported_monotonic_timer(self):
        self.assertIn("esp_timer_get_time()", TIMING_HEADER)
        self.assertIn("endedAtUs >= startedAtUs_", TIMING_HEADER)
        self.assertNotIn("rdcycle", TIMING_HEADER.lower())
        self.assertNotIn('asm volatile("rdcycle', HAL_STORAGE)

    def test_transfer_timing_wraps_buffers_equally_in_both_spi_modes(self):
        self.assertIn("SdSpiArduinoDriver::receive(uint8_t* buf, size_t count)", PATCH_SCRIPT)
        self.assertIn("SdSpiArduinoDriver::send(const uint8_t* buf, size_t count)", PATCH_SCRIPT)
        self.assertIn("kNooirSdSpiReceiveBuffer", PATCH_SCRIPT)
        self.assertIn("kNooirSdSpiSendBuffer", PATCH_SCRIPT)
        self.assertNotIn("spiTransferByteNL", PLATFORMIO)
        self.assertNotIn("spiTransferBytesNL", PLATFORMIO)

    def test_fat_allocation_diagnostics_count_each_candidate_and_report_scan_time(self):
        scan_count = "#if defined(NOOIR_SD_SPI_DIAGNOSTICS)\\n    nooirFatAllocationSearch.addUnits();\\n#endif\\n    int8_t fg = fatGet(find, &f);"
        self.assertIn(scan_count, PATCH_SCRIPT)
        self.assertLess(
            scan_count.index("nooirFatAllocationSearch.addUnits();"),
            scan_count.index("int8_t fg = fatGet(find, &f);"),
        )
        self.assertIn("fat_alloc_calls=%u clusters_scanned=%llu fat_scan_us=%llu", HAL_STORAGE)

    def test_reader_only_errors_when_first_page_is_blocked_after_reclaim(self):
        reader = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
        reader_header = (ROOT / "src/activities/reader/EpubReaderActivity.h").read_text(encoding="utf-8")
        policy = (ROOT / "src/util/ReaderBuildProgressPolicy.h").read_text(encoding="utf-8")
        self.assertIn("shouldShowFirstPageHeapError(section->pageCount, buildHeapPaused)", reader)
        self.assertIn("first_page_blocked_by_heap", reader)
        self.assertIn("showBuildError();", reader)
        self.assertIn("return suspendedAfterHeapRecovery && readablePages == 0", policy)
        self.assertIn("BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024", reader_header)

    def test_progress_safeguard_still_refuses_to_clear_without_candidate(self):
        cache = (ROOT / "src/util/BookCacheUtils.cpp").read_text(encoding="utf-8")
        self.assertIn("Aborting cache clear; no readable progress candidate", cache)
        self.assertLess(cache.index("if (!hasProgressCandidate)"), cache.index("if (!clearFn())"))


if __name__ == "__main__":
    unittest.main()
