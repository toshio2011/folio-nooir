"""Keep Lite three-row shelf profiling bounded and tied to real work."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")


class ThreeRowShelfProfileContract(unittest.TestCase):
    def test_profile_reports_storage_header_decode_and_raster_costs(self):
        body = SOURCE.split("if (profiledThreeRowShelf && shelfDurationUs >= 100000)", 1)[1]
        for field in (
            "furniture_us",
            "resolve_us",
            "sd_open_us",
            "bmp_header_us",
            "bitmap_draw_us",
            "details_us",
        ):
            self.assertIn(field, body)

    def test_source_open_and_header_timers_only_run_during_three_row_profile(self):
        self.assertIn("activeThreeRowShelfProfile != nullptr ? micros() : 0", SOURCE)
        self.assertIn("activeThreeRowShelfProfile->sourceOpenUs += timing.openUs", SOURCE)
        self.assertIn("activeThreeRowShelfProfile->headerParseUs += timing.headerUs", SOURCE)
        self.assertIn("ThreeRowShelfRenderProfile* activeThreeRowShelfProfile", HEADER)


if __name__ == "__main__":
    unittest.main()
