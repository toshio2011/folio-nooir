"""Keep the in-memory framebuffer snapshot key aligned with disk validation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")


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


class HomeSnapshotPresentationContract(unittest.TestCase):
    def test_in_memory_snapshot_key_keeps_exact_theme_identity(self):
        body = function_body(SOURCE, "uint64_t RecentBooksActivity::snapshotPresentationKey() const")
        self.assertIn("static_cast<uint8_t>(SETTINGS.uiTheme)", body)
        self.assertNotIn("isFolioShelfTheme(SETTINGS.uiTheme)", body)

    def test_three_row_spine_appearance_settings_participate_in_key(self):
        body = function_body(SOURCE, "uint64_t RecentBooksActivity::snapshotPresentationKey() const")
        self.assertIn("usesThreeRowSpineLayout()", body)
        self.assertIn("SETTINGS.spineAppearanceMode", body)
        self.assertIn("SETTINGS.spineDecorationStyle", body)
        self.assertIn("spineSessionSeed", body)

    def test_dynamic_snapshot_restore_patches_lite_details_without_rebuilding_the_shelf(self):
        body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        fast_path = body.split("canUseRestoredSnapshotFastPath", 1)[1].split("const bool selectionChanged", 1)[0]
        self.assertIn("snapshotDynamicNeedsPatch", fast_path)
        self.assertIn("liteSpineMode", fast_path)
        self.assertIn("drawLiteSelectedBookDetails", fast_path)
        self.assertIn("drawLiteShelfStats", fast_path)
        self.assertIn("drawSnapshotSelectionOutline", fast_path)
        self.assertIn("Other Lite layouts", body)

    def test_spine_geometry_uses_one_active_layout_cache(self):
        header = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")
        self.assertIn("std::variant<std::monostate, SpinePageWindow, TwoRowSpinePageWindow, ThreeRowSpinePageWindow>",
                      header)
        self.assertNotIn("TwoRowSpinePageWindow twoRowSpinePageWindow", header)
        self.assertNotIn("ThreeRowSpinePageWindow threeRowSpinePageWindow", header)


if __name__ == "__main__":
    unittest.main()
