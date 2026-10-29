"""Source contracts for Library hover, first-open, and bulk book preparation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
LIBRARY = (ROOT / "src/activities/home/FolioLibraryActivity.cpp").read_text(encoding="utf-8")
READER = (ROOT / "src/activities/reader/ReaderActivity.cpp").read_text(encoding="utf-8")
RECENT = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")
EPUB = (ROOT / "lib/Epub/Epub.cpp").read_text(encoding="utf-8")
INFLATE = (ROOT / "lib/miniz/src/InflateStream.cpp").read_text(encoding="utf-8")
ZIP = (ROOT / "lib/ZipFile/ZipFile.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unclosed function body: {signature}")


class LibraryBookPreparationContract(unittest.TestCase):
    def test_hover_keeps_debounce_and_repairs_cache_with_missing_thumbnail(self):
        hover = function_body(LIBRARY, "void FolioLibraryActivity::loadSelectedMetadata()")
        self.assertIn("SELECTION_DEBOUNCE_MS = 1500", hover)
        self.assertIn("cacheIsComplete(cache.metadataValid, cache.thumbnailValid", hover)
        self.assertIn("shouldStartHoverWork(preview.metadataAttempted, true, false)", hover)
        self.assertIn("requestUpdateAndWait()", hover)
        self.assertIn("FrameBufferLoan loan(renderer)", hover)
        self.assertIn("supportsThumbnailSource()", hover)

    def test_first_open_prepares_supported_covers_before_reader_handoff(self):
        epub = function_body(READER, "std::unique_ptr<Epub> ReaderActivity::loadEpub(")
        xtc = function_body(READER, "std::unique_ptr<Xtc> ReaderActivity::loadXtc(")
        cbz = function_body(READER, "std::unique_ptr<Cbz> ReaderActivity::loadCbz(")
        for body in (epub, xtc, cbz):
            self.assertIn("isValidBookThumbnail", body)
            self.assertIn("generateThumbBmp", body)
            self.assertIn("FrameBufferLoan", body)
        self.assertIn("MAX_COVER_SOURCE_BYTES", epub)
        self.assertIn("MAX_COVER_SOURCE_BYTES", cbz)

    def test_no_cover_does_not_become_a_fake_empty_thumbnail_cache_hit(self):
        generator = function_body(EPUB, "bool Epub::generateThumbBmp(")
        self.assertIn("if (validThumbnail(outputPath)) return true", generator)
        self.assertIn("if (coverImageHref.empty())", generator)
        self.assertIn("return false", generator)
        self.assertNotIn("Write an empty bmp file", generator)
        self.assertNotIn("Storage.openFileForWrite(\"EBP\", getThumbBmpPath(height), thumbBmp);\n  return false", generator)

    def test_bulk_reports_outcomes_and_uses_shared_limit_and_framebuffer_scratch(self):
        bulk = function_body(LIBRARY, "void FolioLibraryActivity::processRetrieveAllBooks()")
        finish = function_body(LIBRARY, "void FolioLibraryActivity::finishRetrieveAllBooks(")
        self.assertIn("noteMetadataCache", bulk)
        self.assertIn("noteMetadataResult", bulk)
        self.assertIn("noteThumbnailOutcome", bulk)
        self.assertIn("MAX_COVER_SOURCE_BYTES", bulk)
        self.assertIn("FrameBufferLoan loan(renderer)", bulk)
        self.assertIn("completionMessage()", finish)
        self.assertNotIn("512u * 1024u", bulk)

    def test_recent_manual_refresh_borrows_scratch_and_uses_same_source_limit(self):
        recent = function_body(RECENT, "void RecentBooksActivity::generateNextCover()")
        self.assertIn("FrameBufferLoan", recent)
        self.assertIn("MAX_COVER_SOURCE_BYTES", recent)

    def test_zip_preflights_window_and_can_use_existing_build_scratch(self):
        init = function_body(INFLATE, "bool InflateStream::init(")
        self.assertLess(init.index("buildscratch::claim(needed)"), init.index("largestBlockHint < WINDOW_SIZE"))
        self.assertIn("init(true, largestBeforeInflate)", ZIP)
        self.assertIn("failure=%s free=%u largest=%u", ZIP)


if __name__ == "__main__":
    unittest.main()
