"""Contracts for bounded EPUB image extraction and low-memory recovery."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
IMAGE = (ROOT / "lib/Epub/Epub/blocks/ImageBlock.cpp").read_text(encoding="utf-8")
PAGE = (ROOT / "lib/Epub/Epub/Page.cpp").read_text(encoding="utf-8")
READER = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
EPUB = (ROOT / "lib/Epub/Epub.cpp").read_text(encoding="utf-8")
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


class EpubImageRecoveryContract(unittest.TestCase):
    def test_lazy_source_extraction_uses_framebuffer_scratch_only_during_warmup(self):
        warm = function_body(PAGE, "void Page::warmImageCaches(")
        self.assertIn("image.renderForCacheWarmup(renderer", warm)

        normal = function_body(IMAGE, "void ImageBlock::render(")
        self.assertIn("renderInternal(renderer, x, y, false);", normal)
        image_render = function_body(IMAGE, "void ImageBlock::renderForCacheWarmup(")
        self.assertIn("renderInternal(renderer, x, y, true);", image_render)

        render_internal = function_body(IMAGE, "void ImageBlock::renderInternal(")
        loan = render_internal.index("GfxRenderer::FrameBufferLoan loan(renderer);")
        extract = render_internal.index("extractFn(extractCtx, srcPath.c_str(), imagePath.c_str())", loan)
        restore = render_internal.index("loan.end();", extract)
        self.assertLess(loan, extract)
        self.assertLess(extract, restore)
        self.assertIn("if (!renderer.hasFrameBuffer())", render_internal[restore:])

        reader = function_body(READER, "bool EpubReaderActivity::renderContents(")
        self.assertLess(reader.index("page->warmImageCaches("), reader.index("renderer.clearScreen();"))
        self.assertLess(reader.index("renderer.clearScreen();"), reader.index("page->render(renderer"))

    def test_transient_extraction_failure_is_not_memoized_as_permanent_image_failure(self):
        render_internal = function_body(IMAGE, "void ImageBlock::renderInternal(")
        failure_branch = render_internal[
            render_internal.index("if (retryableExtractionFailure)") :
            render_internal.index("LOG_ERR(\"IMG\", \"Image source is missing or empty")
        ]
        self.assertIn("deferExtractionUntilWarmup = true;", failure_branch)
        self.assertNotIn("rememberImageFailure", failure_branch)

        retry = render_internal[
            render_internal.index("if (sourceWasPresent && fileSize <= 16") :
            render_internal.index("if (fileSize <= 16)")
        ]
        self.assertIn("sourceWasPresent", retry)
        self.assertIn("!retryableExtractionFailure", retry)

    def test_inflater_failure_log_identifies_entry_and_heap_shape(self):
        streaming = ZIP[
            ZIP.index("if (!inflate.init(true, largestBeforeInflate))") : ZIP.index(
                "inflate.setFill(zipFillCallback", ZIP.index("if (!inflate.init(true, largestBeforeInflate))")
            )
        ]
        self.assertIn("entry=%s", streaming)
        self.assertIn("free=%u", streaming)
        self.assertIn("largest=%u", streaming)
        self.assertIn("chunk=%u", streaming)
        self.assertIn("failure=%s", streaming)
        self.assertIn("inflate.initFailureName()", streaming)

    def test_failed_stream_extraction_removes_partial_destination(self):
        extract = function_body(EPUB, "bool Epub::extractItemToFile(")
        self.assertIn("out.flush();", extract)
        self.assertIn("out.close();", extract)
        self.assertIn("if (!ok)", extract)
        self.assertIn("Storage.remove(destPath.c_str());", extract)


if __name__ == "__main__":
    unittest.main()
