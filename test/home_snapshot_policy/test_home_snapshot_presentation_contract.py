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
        self.assertIn("covers can update progress within their own slots", body)

    def test_lite_three_row_progress_is_dynamic_and_patches_only_changed_cover_slots(self):
        key_body = function_body(SOURCE, "uint64_t RecentBooksActivity::snapshotKey(const bool exactThemeKey) const")
        render_body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("threeRowProgressIsDynamic", key_body)
        self.assertIn("bookCoverVisualKey(recentBooks[recentIndex])", render_body)
        self.assertIn("drawSpineBook(renderer, spineWindow.books[localIndex]", render_body)
        self.assertIn("threeRowCoverProgressPatched", render_body)

    def test_lite_three_row_recent_changes_are_checked_and_repaired_per_slot(self):
        key_body = function_body(SOURCE, "uint64_t RecentBooksActivity::snapshotKey(const bool exactThemeKey) const")
        restore_body = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render_body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("if (!patchLiteThreeRowSlots)", key_body)
        self.assertIn("canPatchLiteThreeRowVisibleSlots", restore_body)
        self.assertIn("sameBookInSlot = bookPathKeys[slot] == hashText(book.path)", restore_body)
        self.assertIn("snapshotBookVisualKeys[slot] = ~uint64_t{0}", restore_body)
        self.assertIn("snapshotRewriteAfterRestore", render_body)
        self.assertIn("snapshotWritePending = true", render_body)

    def test_three_row_slot_visual_key_covers_face_fields_and_rejects_replaced_books(self):
        visual_key = function_body(SOURCE, "uint64_t RecentBooksActivity::bookCoverVisualKey(")
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        for field in ("book.title", "book.author", "book.coverBmpPath", "book.progressPercent"):
            self.assertIn(field, visual_key)
        self.assertIn("canPatchLiteThreeRowVisibleSlot", restore)
        self.assertIn("sameBookInSlot", restore)

    def test_spine_geometry_uses_one_active_layout_cache(self):
        header = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")
        self.assertIn("std::variant<std::monostate, SpinePageWindow, TwoRowSpinePageWindow, ThreeRowSpinePageWindow>",
                      header)
        self.assertNotIn("TwoRowSpinePageWindow twoRowSpinePageWindow", header)
        self.assertNotIn("ThreeRowSpinePageWindow threeRowSpinePageWindow", header)


if __name__ == "__main__":
    unittest.main()
