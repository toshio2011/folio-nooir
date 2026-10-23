"""Source contract for the spine shelf clearing optimization.

The shelf painter follows a full-frame clear, so broad panel clears are
redundant. Each cover face still needs clearing because bitmap white pixels are
transparent to the renderer and shelf furniture is painted behind the cover.
"""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "src/activities/home/RecentBooksActivity.cpp"
TEXT = SOURCE.read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    start = TEXT.index(signature)
    open_brace = TEXT.index("{", start)
    depth = 0
    for index in range(open_brace, len(TEXT)):
        if TEXT[index] == "{":
            depth += 1
        elif TEXT[index] == "}":
            depth -= 1
            if depth == 0:
                return TEXT[open_brace + 1 : index]
    raise AssertionError(f"unclosed function body: {signature}")


class RedundantSpineClearContract(unittest.TestCase):
    def test_spine_cover_faces_are_cleared_before_bitmap(self):
        body = function_body("void drawSpineBook(")
        self.assertIn("if (coverFace)", body)
        face_clear = body.index("renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false)")
        cover_bitmap = body.index("renderer.drawBitmap(")
        self.assertLess(face_clear, cover_bitmap)

    def test_lite_full_frame_clears_once_before_shelf_paint(self):
        body = function_body("bool RecentBooksActivity::renderCarousel(")
        self.assertLess(body.index("renderer.clearScreen();"), body.index("drawSpineShelf("))
        self.assertLess(body.index("renderer.clearScreen();"), body.index("drawTwoRowSpineShelf("))
        self.assertLess(body.index("renderer.clearScreen();"), body.index("drawThreeRowSpineShelf("))
        self.assertNotIn("fillRect(0, displayLayout.gridTop, pageWidth, displayLayout.gridHeight", body)
        self.assertNotIn("fillRect(carouselLayout.carouselRegion.x", body)

    def test_main_spine_painter_is_guarded_by_non_snapshot_full_clear(self):
        full_clear = TEXT.index("if (!renderedFromSnapshot) {\n    renderer.clearScreen();")
        spine_branch = TEXT.index("} else if (spineLayout)", full_clear)
        spine_guard = TEXT.index("if (!renderedFromSnapshot) {", spine_branch)
        spine_painter = TEXT.index("drawSpineShelf(renderer, spineWindow", spine_guard)
        two_row_branch = TEXT.index("} else if (twoRowSpineLayout)", spine_painter)
        two_row_guard = TEXT.index("if (!renderedFromSnapshot) {", two_row_branch)
        two_row_painter = TEXT.index("drawTwoRowSpineShelf(renderer, spineWindow", two_row_guard)
        self.assertLess(full_clear, spine_painter)
        self.assertLess(full_clear, two_row_painter)
        self.assertNotIn("renderer.fillRect(0, layout.gridTop, pageWidth, layout.gridHeight, false)", TEXT)


if __name__ == "__main__":
    unittest.main()
