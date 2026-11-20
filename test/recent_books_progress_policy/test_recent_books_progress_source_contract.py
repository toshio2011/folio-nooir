from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")


def test_finished_filter_and_counters_use_authoritative_completion():
    assert "RecentBooksViewPolicy::tabIncludes(activeTab, bookProgress(book).finished)" in SOURCE
    assert SOURCE.count("if (bookProgress(book).finished && finishedCount < UINT16_MAX)") == 2


def test_featured_and_cover_badges_use_authoritative_progress():
    assert SOURCE.count("bookProgress(selected).percent") >= 3
    assert SOURCE.count("bookProgress(book).percent") >= 5
    assert "selected.progressPercent, selectedState ? selectedState->progressPercent" not in SOURCE


def test_snapshot_keys_track_authoritative_progress_and_repatch_covers():
    cover_key = SOURCE.split("uint64_t RecentBooksActivity::bookCoverVisualKey", 1)[1].split(
        "uint64_t RecentBooksActivity::snapshotDynamicKey", 1
    )[0]
    assert "bookProgress(book)" in cover_key
    assert "bookVisualKeys[slot] != currentVisualKeys[slot]" in SOURCE
    assert "snapshotBookVisualKeys[static_cast<size_t>(slot)] != coverVisualKey" in SOURCE
    assert "const auto progress = bookProgress(book);" in SOURCE


def test_render_progress_fix_does_not_add_recent_store_writes():
    helper = SOURCE.split("RecentBooksProgressPolicy::Value bookProgress", 1)[1].split("\n}", 1)[0]
    assert "RECENT_BOOKS.recordReading" not in helper
    assert "BOOK_STATES.setStatus" not in helper


def test_spine_planner_receives_authoritative_progress_without_render_lookup():
    assert SOURCE.count("book.path.c_str(), book.title.c_str(), bookProgress(book).percent") == 4
    draw = SOURCE.split("void drawSpineBook(", 1)[1].split("void drawSpineShelf(", 1)[0]
    assert "const uint8_t progressPercent = book.progressPercent;" in draw
    assert "bookProgress(book)" not in draw
