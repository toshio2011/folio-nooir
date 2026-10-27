"""Home BLE reclaim releases only caches and fonts proven unused by UI."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
RECLAIMER = (ROOT / "src/util/MemoryPressureReclaimer.cpp").read_text(encoding="utf-8")
FONT_MANAGER = (ROOT / "lib/EpdFont/SdCardFontManager.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")


class HomeMemoryReclaimContract(unittest.TestCase):
    def test_cold_home_ble_reclaim_uses_renderer_ownership_check(self):
        self.assertIn("reclaimForHeadroom(\n          renderer, bleinput::kReaderBleStartMinFreeHeap", MAIN)
        self.assertIn("sdFontSystem.releaseReaderFontOwner(renderer);", RECLAIMER)

    def test_reader_font_is_retained_when_ui_mapping_references_it(self):
        start = FONT_MANAGER.index("void SdCardFontManager::releaseReaderFontOwner")
        end = FONT_MANAGER.index("void SdCardFontManager::beginAllocationRetryBoundary", start)
        body = FONT_MANAGER[start:end]
        self.assertIn("renderer.isUiFontReferenced(selected.fontId)", body)
        self.assertIn("if (sharedWithUiFallback)", body)
        self.assertIn("renderer.removeFont(selected.fontId)", body)

    def test_reclaimer_stops_once_disposable_caches_meet_both_heap_floors(self):
        self.assertLess(
            RECLAIMER.index("if (targetsMet(report.freeAfter, report.largestAfter, targetFree, targetLargest))"),
            RECLAIMER.index("sdFontSystem.releaseReaderFontOwner(renderer);"),
        )


if __name__ == "__main__":
    unittest.main()
