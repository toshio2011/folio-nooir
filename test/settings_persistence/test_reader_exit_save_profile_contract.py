"""Protect streamed, validated reader-state writes and save-phase diagnostics."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STORE = (ROOT / "lib/Serialization/PersistableStore.cpp").read_text(encoding="utf-8")
READER = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
PROGRESS = (ROOT / "src/activities/reader/ProgressFile.h").read_text(encoding="utf-8")
EPUB_PROGRESS = (ROOT / "src/activities/reader/EpubReaderUtils.h").read_text(encoding="utf-8")


class ReaderExitSaveProfileContract(unittest.TestCase):
    def test_non_settings_stores_keep_the_atomic_stream_validation_path(self):
        body = STORE.split("static bool writeDocToFileInternal", 1)[1]
        self.assertIn("if (!settingsPath)", body)
        self.assertIn("writeJsonDocumentToFile(tmpPath.c_str(), doc, expectedBytes", body)
        self.assertIn("temporaryFileMatches(tmpPath.c_str(), doc, expectedBytes, &profile)", body)
        self.assertIn("Storage.rename(tmpPath.c_str(), path)", body)

    def test_profile_reports_write_validation_and_each_promotion_phase(self):
        for field in (
            "open_us",
            "write_call_us",
            "sync_flush_us",
            "close_us",
            "validation_us",
            "backup_us",
            "primary_rename_us",
            "temp_rename_us",
            "cleanup_us",
            "total_us",
        ):
            self.assertIn(field, STORE)

    def test_exit_summary_separates_request_wait_save_and_cleanup(self):
        for field in ("request_to_on_exit_ms", "save_us", "cleanup_us", "total_us"):
            self.assertIn(field, READER)

    def test_reader_exit_times_each_distinct_persistent_store(self):
        for operation in ("recent", "book_state", "reading_stats", "app_recovery_state", "footnote_progress"):
            self.assertIn(f"reader_exit_save kind={operation}", READER)

    def test_epub_progress_save_profiles_atomic_sd_phases_without_changing_order(self):
        self.assertIn("writeAtomic(epub.getCachePath(), data, sizeof(data), true)", EPUB_PROGRESS)
        self.assertIn("save_profile file=progress.bin", PROGRESS)
        self.assertIn("backup_rename_us", PROGRESS)
        self.assertIn("publish_rename_us", PROGRESS)
        self.assertLess(PROGRESS.index("Storage.rename(finalPath.c_str(), backup.c_str())"),
                        PROGRESS.index("Storage.rename(tmpPath.c_str(), finalPath.c_str())"))
        self.assertIn("rollback_us", PROGRESS)


if __name__ == "__main__":
    unittest.main()
