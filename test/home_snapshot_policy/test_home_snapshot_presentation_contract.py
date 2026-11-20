"""Keep the in-memory framebuffer snapshot key aligned with disk validation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/util/HomeSnapshotPolicy.h").read_text(encoding="utf-8")
SETTINGS = (ROOT / "src/CrossPointSettings.h").read_text(encoding="utf-8")
LITE_POLICY = (ROOT / "src/util/FolioLiteLayoutPolicy.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str, last: bool = False) -> str:
    start = source.rindex(signature) if last else source.index(signature)
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
        self.assertIn("faces can patch progress and status when saved identities", body)

    def test_lite_three_row_reorder_rebuilds_when_identity_or_geometry_changes(self):
        restore_body = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render_body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("bookCoverVisualKey(recentBooks[recentIndex])", render_body)
        self.assertIn("HomeSnapshotPolicy::slotNeedsPatch", render_body)
        self.assertIn("snapshotBookPathKeys", render_body)
        self.assertIn("snapshotBookRects", render_body)
        self.assertIn("drawSpineBook(renderer, spineWindow.books[localIndex]", render_body)
        self.assertIn("renderer.fillRect(oldRect.x, oldRect.y, oldRect.width, oldRect.height, false)", render_body)
        self.assertIn("HomeSnapshotPolicy::threeRowPageStructureMatches", restore_body)
        self.assertIn('invalidate("lite_three_row_structure_changed")', restore_body)

    def test_lite_three_row_reorders_with_changed_structure_invalidate_the_saved_frame(self):
        key_body = function_body(
            SOURCE,
            "uint64_t RecentBooksActivity::snapshotKey(const bool exactThemeKey, const bool includeSpineDynamic) const",
        )
        restore_body = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render_body = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("if (!patchLiteThreeRowSlots)", key_body)
        self.assertIn("savedSelectedPathKey == hashText(book.path)", restore_body)
        self.assertIn("canPatchMovedThreeRowPage", restore_body)
        self.assertIn("savedGeometryValid", restore_body)
        self.assertIn("captureSnapshotRects(currentPageStart, currentPageItems, currentRects)", restore_body)
        self.assertIn("currentPageStart == pageStart", restore_body)
        self.assertIn('invalidate("lite_three_row_structure_changed")', restore_body)
        self.assertIn("snapshotRewriteAfterRestore", render_body)
        self.assertIn("snapshotWritePending = true", render_body)

    def test_v13_snapshot_preserves_prior_geometry_and_names_invalidations(self):
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        write = function_body(SOURCE, "void RecentBooksActivity::writeSnapshot()")
        self.assertIn("FOLIO_HOME_VERSION = 13", SOURCE)
        self.assertIn("FOLIO_HOME_CAROUSEL_SLOTS_VERSION = 13", SOURCE)
        self.assertIn("file.read(bookRects.data(), sizeof(bookRects))", restore)
        self.assertIn("file.write(snapshotBookRects.data(), sizeof(snapshotBookRects))", write)
        self.assertIn("classifySnapshotInvalidation", restore)
        self.assertIn("layout_identity_mismatch", restore)
        for reason in ("visual_or_structural_key_mismatch", "visible_page_not_patchable",
                       "slot_geometry_unavailable", "current_page_geometry_mismatch"):
            self.assertIn(reason, POLICY)

    def test_v12_spine_snapshot_separates_structural_identity_from_dynamic_progress(self):
        key_body = function_body(
            SOURCE,
            "uint64_t RecentBooksActivity::snapshotKey(const bool exactThemeKey, const bool includeSpineDynamic) const",
        )
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("FOLIO_HOME_GEOMETRY_VERSION = 11", SOURCE)
        self.assertIn("FOLIO_HOME_DYNAMIC_SPINE_VERSION = 12", SOURCE)
        self.assertIn("includeSpineDynamic", key_body)
        self.assertIn("const bool finished = state ? state->status == BookStatus::Finished", key_body)
        self.assertIn("version >= FOLIO_HOME_GEOMETRY_VERSION", restore)
        self.assertIn("HomeSnapshotPolicy::spinePageStructureMatches", restore)
        self.assertIn("savedGeometryValid && currentGeometryAvailable", restore)
        self.assertIn(
            "snapshot_validation theme=%u layout=%u version=%u key_semantics=%s key_match=%u",
            restore,
        )
        self.assertIn("twoRowSpinePageForSelection(bounds)", render)
        self.assertIn("bookCoverVisualKey(book)", render)
        self.assertIn("spineShelfMode", render)
        self.assertIn("!noOverlay || !noBackgroundWork, liteSpineMode", render)

    def test_legacy_progress_key_mismatch_requires_a_cached_matching_structure(self):
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        self.assertIn("legacyKeyedSpineSnapshot", restore)
        self.assertIn("snapshotStructuralKeyValue != 0", restore)
        self.assertIn("canPatchLegacySpineDynamicMismatch", restore)
        self.assertIn("legacy_cached_patch=%u", restore)
        self.assertIn("snapshotKeyAccepted ? currentSnapshotKey : key", restore)
        self.assertIn("cachedStructureMatches && dynamicChanged && sameVisiblePage && pageRangeValid", POLICY)

    def test_lite_three_row_invalidation_logs_the_exact_structural_mismatch(self):
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        self.assertIn("snapshot_validation_detail theme=Lite layout=%u", restore)
        self.assertIn("saved_start=%lu current_start=%lu", restore)
        self.assertIn("identity_match=%u geometry_match=%u", restore)

    def test_nooir_carousel_restore_patches_only_a_validated_center_book(self):
        render = function_body(SOURCE, "void RecentBooksActivity::render(RenderLock&&)", last=True)
        capture = function_body(SOURCE, "void RecentBooksActivity::captureSnapshotVisualState(")
        shelf = function_body(SOURCE, "void RecentBooksActivity::renderFolioCarouselShelf(")
        self.assertIn("classifyNooirMainCarouselPatch", render)
        self.assertIn("snapshotCarouselSlotMetadataAvailable", render)
        self.assertIn("renderFolioCarouselShelf(layout.gridTop, layout.gridHeight, threeCover, true)", render)
        self.assertIn("changedSlotMask", render)
        self.assertIn("snapshotBookPathKeys[0]", capture)
        self.assertIn("snapshotBookRects[metadataSlot] = savedRect", capture)
        self.assertIn("if (!centerOnly) renderer.fillRect", shelf)
        self.assertIn("centerOnly && slotIndex != 4", shelf)

    def test_lite_refreshes_dynamic_snapshot_signatures_only_after_a_successful_frame(self):
        render = function_body(SOURCE, "void RecentBooksActivity::render(RenderLock&&)", last=True)
        self.assertIn("const bool liteSnapshotKeyRefreshPending = initialRenderPending || snapshotDynamicNeedsPatch", render)
        self.assertIn("if (liteSnapshotKeyRefreshPending && snapshotRestored)", render)
        self.assertIn("snapshotDynamicKeyValue = snapshotDynamicKey()", render)

    def test_folio_nooir_layouts_patch_restored_spines_without_touching_lite_fast_paths(self):
        nooir_render = function_body(SOURCE, "void RecentBooksActivity::render(RenderLock&&)", last=True)
        lite_render = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        self.assertIn("FOLIO_LAYOUT_GRID = 0", SETTINGS)
        self.assertIn("FOLIO_LAYOUT_THREE_COVERS = 1", SETTINGS)
        self.assertIn("FOLIO_LAYOUT_THREE_COVER_CAROUSEL = 2", SETTINGS)
        self.assertIn("FOLIO_LAYOUT_FIVE_COVER_CAROUSEL = 3", SETTINGS)
        self.assertIn("FOLIO_LAYOUT_SPINE = 4", SETTINGS)
        self.assertIn("FOLIO_LAYOUT_TWO_ROW_SPINE = 5", SETTINGS)
        self.assertIn("else if (spineLayout)", nooir_render)
        self.assertIn("else if (twoRowSpineLayout)", nooir_render)
        self.assertIn("patchRestoredNooirSpineFaces(spineWindow)", nooir_render)
        self.assertIn("stage=nooir_spine_incremental_patch theme=%u layout=%u", nooir_render)
        self.assertIn("snapshotBookVisualKeys[selectedSnapshotSlot] != bookCoverVisualKey(selected)", nooir_render)
        self.assertIn("FolioLiteLayoutPolicy::canPatchSpineSelection", lite_render)
        self.assertIn("!noOverlay || !noBackgroundWork, liteSpineMode", lite_render)

    def test_folio_lite_supported_layouts_keep_their_own_theme_path(self):
        active_layout = function_body(SOURCE, "uint8_t RecentBooksActivity::activeBookLayout() const")
        three_row = function_body(SOURCE, "bool RecentBooksActivity::usesThreeRowSpineLayout() const")
        snapshot = function_body(SOURCE, "bool RecentBooksActivity::usesPersistentHomeSnapshot() const")
        for layout in (
            "LITE_LAYOUT_THREE_COVER_CAROUSEL = 0",
            "LITE_LAYOUT_FIVE_COVER_CAROUSEL = 1",
            "LITE_LAYOUT_SPINE = 2",
            "LITE_LAYOUT_TWO_ROW_SPINE = 3",
            "LITE_LAYOUT_THREE_ROW_SPINE = 4",
        ):
            self.assertIn(layout, LITE_POLICY)
        self.assertIn("SETTINGS.liteFinishedBookLayout", active_layout)
        self.assertIn("SETTINGS.liteRecentBookLayout", active_layout)
        self.assertIn("isFolioNooirLiteTheme(SETTINGS.uiTheme)", three_row)
        self.assertIn("isFolioShelfTheme(SETTINGS.uiTheme)", snapshot)

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

    def test_three_row_slot_visual_key_covers_face_fields_for_stable_slot_patching(self):
        visual_key = function_body(SOURCE, "uint64_t RecentBooksActivity::bookCoverVisualKey(")
        restore = function_body(SOURCE, "bool RecentBooksActivity::restoreSnapshot()")
        render = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        for field in ("book.title", "book.author", "book.coverBmpPath", "book.progressPercent"):
            self.assertIn(field, visual_key)
        self.assertIn("savedSelectedPathKey == hashText(book.path)", restore)
        self.assertIn("HomeSnapshotPolicy::slotNeedsPatch", render)

    def test_all_lite_shelf_layouts_share_bounded_header_detail_rendering(self):
        render = function_body(SOURCE, "bool RecentBooksActivity::renderCarousel(const bool threeCover)")
        details = render.index("drawLiteSelectedBookDetails(renderer, folioPresentation, carouselLayout, selected, false)")
        layout_start = render.index("const uint32_t shelfStartedUs = micros();", details)
        layout_branches = render[layout_start:]
        self.assertIn("if (usesSpineLayout())", layout_branches)
        self.assertIn("else if (usesTwoRowSpineLayout())", layout_branches)
        self.assertIn("else if (usesThreeRowSpineLayout())", layout_branches)
        self.assertIn("authorTextUpdate(clearPreviousDetails, !selected.author.empty())", SOURCE)
        self.assertIn("renderer.truncatedText(UI_10_FONT_ID, selected.author.c_str()", SOURCE)
        self.assertIn("renderer.getTextWidth(UI_10_FONT_ID, author.c_str())", SOURCE)
        self.assertIn("renderer.getLineHeight(SMALL_FONT_ID)", SOURCE)

    def test_lite_header_repaints_a_bounded_full_band_on_selection_changes(self):
        details = SOURCE.split("void drawLiteSelectedBookDetails", 1)[1].split("\n}", 1)[0]
        layout = (ROOT / "src/components/themes/folio_nooir/CarouselTheme.cpp").read_text(encoding="utf-8")
        policy = (ROOT / "src/util/FolioLiteLayoutPolicy.h").read_text(encoding="utf-8")
        self.assertIn("carouselLayout.headerRegion", details)
        self.assertIn("clearPreviousDetails", details)
        self.assertIn("renderer.truncatedText(UI_12_FONT_ID, selected.title.c_str()", details)
        self.assertIn("renderer.truncatedText(UI_10_FONT_ID, selected.author.c_str()", details)
        self.assertIn("featuredFormatBadgeWidth", details)
        self.assertIn("layout.headerRegion = Rect{0, FolioLiteLayoutPolicy::LITE_BOOK_HEADER_CLEAR_TOP", layout)
        self.assertIn("LITE_BOOK_HEADER_CLEAR_HEIGHT = LITE_CAROUSEL_TOP - LITE_BOOK_HEADER_CLEAR_TOP", policy)

    def test_spine_geometry_uses_one_active_layout_cache(self):
        header = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")
        self.assertIn("std::variant<std::monostate, SpinePageWindow, TwoRowSpinePageWindow, ThreeRowSpinePageWindow>",
                      header)
        self.assertNotIn("TwoRowSpinePageWindow twoRowSpinePageWindow", header)
        self.assertNotIn("ThreeRowSpinePageWindow threeRowSpinePageWindow", header)


if __name__ == "__main__":
    unittest.main()
