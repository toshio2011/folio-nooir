"""Keep the in-memory framebuffer snapshot key aligned with disk validation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/util/HomeSnapshotPolicy.h").read_text(encoding="utf-8")


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
        signature = function_body(
            SOURCE,
            "HomeSnapshotPolicy::VisualSettingsSignature RecentBooksActivity::visualSettingsSignature() const",
        )
        self.assertIn("visualSettingsSignature()", body)
        self.assertIn("static_cast<uint8_t>(SETTINGS.uiTheme)", signature)
        self.assertNotIn("isFolioShelfTheme(SETTINGS.uiTheme)", signature)

    def test_three_row_spine_appearance_settings_participate_in_key(self):
        signature = function_body(
            SOURCE,
            "HomeSnapshotPolicy::VisualSettingsSignature RecentBooksActivity::visualSettingsSignature() const",
        )
        presentation = function_body(SOURCE, "uint64_t RecentBooksActivity::snapshotPresentationKey() const")
        self.assertIn("usesThreeRowSpineLayout()", signature)
        self.assertIn("SETTINGS.spineAppearanceMode", signature)
        self.assertIn("SETTINGS.spineDecorationStyle", signature)
        self.assertIn("spineSessionSeed", presentation)

    def test_dynamic_snapshot_restore_patches_lite_details_without_rebuilding_the_shelf(self):
        body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        fast_path = body.split("canUseRestoredSnapshotFastPath", 1)[1].split("const bool selectionChanged", 1)[0]
        self.assertIn("snapshotDynamicNeedsPatch", fast_path)
        self.assertIn("liteSpineMode", fast_path)
        self.assertIn("drawLiteSelectedBookDetails", fast_path)
        self.assertIn("drawLiteShelfStats", fast_path)
        self.assertIn("drawSnapshotSelectionOutline", fast_path)
        self.assertIn("faces can patch progress, status, and moved identities", body)

    def test_lite_three_row_patches_changed_book_identities_and_geometry(self):
        render_body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("bookCoverVisualKey(recentBooks[recentIndex])", render_body)
        self.assertIn("HomeSnapshotPolicy::slotNeedsPatch", render_body)
        self.assertIn("snapshotBookPathKeys", render_body)
        self.assertIn("snapshotBookRects", render_body)
        self.assertIn("drawSpineBook(renderer, spineWindow.books[localIndex]", render_body)
        self.assertIn("renderer.fillRect(oldRect.x, oldRect.y, oldRect.width, oldRect.height, false)", render_body)

    def test_lite_three_row_reorders_and_finished_filter_changes_use_geometry_snapshots(self):
        key_body = function_body(SOURCE, "uint64_t RecentBooksActivity::snapshotKey(const bool exactThemeKey) const")
        restore_body = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render_body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("if (!patchLiteThreeRowSlots)", key_body)
        self.assertIn("savedSelectedPathKey == hashText(book.path)", restore_body)
        self.assertIn("canPatchMovedThreeRowPage", restore_body)
        self.assertIn("savedGeometryValid", restore_body)
        self.assertIn("captureSnapshotRects(currentPageStart, currentPageItems, currentRects)", restore_body)
        self.assertIn("currentPageStart != pageStart", restore_body)
        self.assertIn("snapshotRewriteAfterRestore", render_body)
        self.assertIn("snapshotWritePending = true", render_body)

    def test_snapshot_v11_persists_visible_slot_geometry_and_names_invalidations(self):
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        write = function_body(SOURCE, "void RecentBooksActivity::writeSnapshot()")
        self.assertIn("FOLIO_HOME_VERSION = 11", SOURCE)
        self.assertIn("file.read(bookRects.data(), sizeof(bookRects))", restore)
        self.assertIn("file.write(snapshotBookRects.data(), sizeof(snapshotBookRects))", write)
        self.assertIn("classifySnapshotInvalidation", restore)
        self.assertIn("layout_identity_mismatch", restore)
        for reason in ("visual_or_structural_key_mismatch", "visible_page_not_patchable",
                       "slot_geometry_unavailable", "current_page_geometry_mismatch"):
            self.assertIn(reason, POLICY)

    def test_popup_dismissal_restores_clean_snapshot_before_fast_home_redraw(self):
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshotAfterOverlayDismissal()")
        self.assertGreaterEqual(SOURCE.count("restoreSnapshotAfterOverlayDismissal()"), 3)
        self.assertIn("const bool restored = restoreSnapshot()", restore)
        self.assertIn("currentPageStart != snapshotPageStart", restore)
        self.assertIn("snapshotDynamicNeedsPatch = true", restore)
        self.assertIn("snapshotFastPathUsed = false", restore)

    def test_home_visual_signature_excludes_system_and_reader_only_preferences(self):
        body = function_body(SOURCE, "HomeSnapshotPolicy::VisualSettingsSignature RecentBooksActivity::visualSettingsSignature() const")
        for field in ("SETTINGS.uiTheme", "SETTINGS.orientation", "SETTINGS.uiScalePercent",
                      "SETTINGS.coverProgressStyle", "SETTINGS.language", "SETTINGS.spineAppearanceMode",
                      "SETTINGS.spineDecorationStyle"):
            self.assertIn(field, body)
        for field in ("SETTINGS.bluetoothEnabled", "SETTINGS.wifiEnabled", "SETTINGS.readerFontSize"):
            self.assertNotIn(field, body)

    def test_settings_reload_only_when_theme_visual_setting_changed(self):
        settings = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(encoding="utf-8")
        body = function_body(settings, "void SettingsActivity::onExit()")
        self.assertIn("settingsUiThemeAtEntry = SETTINGS.uiTheme", settings)
        self.assertIn("if (SETTINGS.uiTheme != settingsUiThemeAtEntry)", body)
        self.assertIn("UITheme::getInstance().reload()", body)
        self.assertIn("skipped=unchanged", body)

    def test_three_row_slot_visual_key_covers_face_fields_and_allows_identity_patching(self):
        visual_key = function_body(SOURCE, "uint64_t RecentBooksActivity::bookCoverVisualKey(")
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        for field in ("book.title", "book.author", "book.coverBmpPath", "book.progressPercent"):
            self.assertIn(field, visual_key)
        self.assertIn("savedSelectedPathKey == hashText(book.path)", restore)
        self.assertIn("HomeSnapshotPolicy::slotNeedsPatch", render)

    def test_spine_geometry_uses_one_active_layout_cache(self):
        header = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")
        self.assertIn("std::variant<std::monostate, SpinePageWindow, TwoRowSpinePageWindow, ThreeRowSpinePageWindow>",
                      header)
        self.assertNotIn("TwoRowSpinePageWindow twoRowSpinePageWindow", header)
        self.assertNotIn("ThreeRowSpinePageWindow threeRowSpinePageWindow", header)


if __name__ == "__main__":
    unittest.main()
