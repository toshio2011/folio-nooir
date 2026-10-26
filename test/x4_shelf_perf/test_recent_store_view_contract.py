import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTIVITY_H = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")
ACTIVITY_CPP = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")
MANAGER_CPP = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")


class RecentBooksStoreViewContract(unittest.TestCase):
    def test_shelf_reuses_store_owned_book_records(self):
        self.assertIn("RecentBooksStore::RecentBooksReadView recentBooks;", ACTIVITY_H)
        self.assertNotIn("std::vector<RecentBook> recentBooks;", ACTIVITY_H)
        self.assertIn("recentBooks = RECENT_BOOKS.getBooks();", ACTIVITY_CPP)
        self.assertNotIn("getBooks().copyTo(recentBooks)", ACTIVITY_CPP)

    def test_manual_metadata_refresh_copies_only_one_record(self):
        start = ACTIVITY_CPP.index("void RecentBooksActivity::generateNextCover()")
        end = ACTIVITY_CPP.index("bool RecentBooksActivity::generateCarouselHqThumbnail", start)
        body = ACTIVITY_CPP[start:end]
        self.assertIn("recentBooks.reset();", body)
        self.assertIn("RECENT_BOOKS.getBookAt(bookIndex, book)", body)
        self.assertNotIn("recentBooks[bookIndex]", body)

    def test_parent_releases_view_before_child_push_and_reacquires_on_return(self):
        self.assertIn("releaseRecentBooksViewForChild()", MANAGER_CPP)
        self.assertIn("restoreRecentBooksViewAfterChild(true)", MANAGER_CPP)
        self.assertIn("recentBooks.reset();", ACTIVITY_CPP)
        self.assertIn("if (!recentBooks.ownsLock())", ACTIVITY_CPP)

    def test_store_mutations_release_view_first(self):
        action_start = ACTIVITY_CPP.index("bookActionsPopup.show(")
        action_end = ACTIVITY_CPP.index("void RecentBooksActivity::onEnter()", action_start)
        action_body = ACTIVITY_CPP[action_start:action_end]
        self.assertLess(action_body.index("recentBooks.reset();"), action_body.index("RECENT_BOOKS.recordReading"))
        self.assertLess(action_body.index("recentBooks.reset();"), action_body.index("RECENT_BOOKS.removeByPath"))


if __name__ == "__main__":
    unittest.main()
