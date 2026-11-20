import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SD_FONT_H = (ROOT / "lib/EpdFont/SdCardFont.h").read_text(encoding="utf-8")
SD_FONT_CPP = (ROOT / "lib/EpdFont/SdCardFont.cpp").read_text(encoding="utf-8")
FONT_CACHE_CPP = (ROOT / "lib/GfxRenderer/FontCacheManager.cpp").read_text(encoding="utf-8")
FONT_DECOMPRESSOR_CPP = (ROOT / "lib/EpdFont/FontDecompressor.cpp").read_text(encoding="utf-8")
READER_CPP = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")


class SdFontPrewarmAdmissionContract(unittest.TestCase):
    def test_reader_prewarm_hook_reaches_sd_font_and_denial_replays_render(self):
        self.assertIn("scope.setAdmissionHook(&EpubReaderActivity::admitReaderFontPrewarm, this)", READER_CPP)
        self.assertIn("admissionHook_, admissionContext_", FONT_CACHE_CPP)
        self.assertIn("if (missed == -2) return false;", FONT_CACHE_CPP)
        self.assertIn("return -2;  // The caller will replay this page", FONT_DECOMPRESSOR_CPP)

    def test_sd_font_allocation_sites_are_admitted_before_growth(self):
        for stage in (
            "codepoint_workspace",
            "kern_left_classes",
            "kern_right_classes",
            "ligature_pairs",
            "mini_mapping",
            "mini_intervals",
            "mini_glyphs",
            "mini_read_order",
            "mini_bitmap",
            "bitmap_read_order",
            "mini_kern_left",
            "mini_kern_right",
            "mini_kern_matrix",
        ):
            with self.subTest(stage=stage):
                self.assertIn(f'"{stage}"', SD_FONT_CPP)
        self.assertIn("PREWARM_HEAP_RESERVE_BYTES + requestedBytes", SD_FONT_CPP)
        self.assertIn("static constexpr size_t PREWARM_HEAP_RESERVE_BYTES = 24 * 1024", SD_FONT_H)

    def test_sd_allocation_requests_are_included_in_reader_pressure_diagnostics(self):
        self.assertIn("largestAllocationRequestBytes = 0", SD_FONT_H)
        self.assertIn("font->getStats().largestAllocationRequestBytes", FONT_CACHE_CPP)
        self.assertIn("font->getStats().failedAllocationCount", FONT_CACHE_CPP)


if __name__ == "__main__":
    unittest.main()
