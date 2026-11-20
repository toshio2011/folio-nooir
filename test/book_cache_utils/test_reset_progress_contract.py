from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ResetProgressContract(unittest.TestCase):
    def test_all_reset_entry_points_use_complete_persisted_reset(self):
        sources = (
            "src/activities/home/HomeActivity.cpp",
            "src/activities/home/RecentBooksActivity.cpp",
            "src/activities/home/FolioLibraryActivity.cpp",
            "src/network/CrossPointWebServer.cpp",
        )
        for relative in sources:
            with self.subTest(source=relative):
                source = (ROOT / relative).read_text(encoding="utf-8")
                self.assertIn("resetBookReadingProgress(", source)
                self.assertNotIn("resetBookProgress(", source)

    def test_web_reset_refuses_live_reader_and_reports_persistence_failure(self):
        source = (ROOT / "src/network/CrossPointWebServer.cpp").read_text(encoding="utf-8")
        reset = source[source.index('if (action == "reset")'):]
        reset = reset[: reset.index("const BookState* existing")]
        self.assertIn("activityManager.isReaderActivity()", reset)
        self.assertIn("server->send(409", reset)
        self.assertIn("if (!resetBookReadingProgress(path))", reset)
        self.assertIn("server->send(500", reset)

    def test_remove_from_list_preserves_book_state_history(self):
        for relative in (
            "src/activities/home/HomeActivity.cpp",
            "src/activities/home/RecentBooksActivity.cpp",
        ):
            with self.subTest(source=relative):
                source = (ROOT / relative).read_text(encoding="utf-8")
                start = source.index("if (action == 7)")
                end = source.index("if (action == 8)", start)
                remove_action = source[start:end]
                self.assertIn("RECENT_BOOKS.removeByPath", remove_action)
                self.assertNotIn("BOOK_STATES.removeByPath", remove_action)


if __name__ == "__main__":
    unittest.main()
