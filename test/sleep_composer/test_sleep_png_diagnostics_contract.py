"""Keep sleep PNG I/O diagnostics specific enough to separate SD and PNG failures."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/boot_sleep/SleepActivity.cpp").read_text(encoding="utf-8")


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


class SleepPngDiagnosticsContract(unittest.TestCase):
    def test_sd_callbacks_retain_file_size_and_last_read_counts(self):
        open_callback = function_body(SOURCE, "void* overlayPngOpen(")
        self.assertIn("overlayPngIo.fileOpened = true", open_callback)
        self.assertIn("overlayPngIo.fileSizeKnown = true", open_callback)
        self.assertIn("overlayPngIo.fileSize = *size", open_callback)

        read_callback = function_body(SOURCE, "int32_t overlayPngRead(")
        self.assertIn("overlayPngIo.lastRequestedBytes = length", read_callback)
        self.assertIn("overlayPngIo.lastReadBytes = bytesRead", read_callback)
        self.assertIn("++overlayPngIo.shortReadCount", read_callback)

    def test_open_error_class_checks_callback_failure_and_short_reads_before_header_errors(self):
        classify = function_body(SOURCE, "const char* overlayPngOpenFailureReason(")
        self.assertIn('return "file_handle"', classify)
        self.assertIn('return "storage_open"', classify)
        self.assertIn('return "short_sd_read"', classify)
        self.assertIn('return "png_header_or_format"', classify)
        self.assertLess(classify.index("shortReadCount > 0"), classify.index('return "png_header_or_format"'))

        render = function_body(SOURCE, "bool SleepActivity::renderOverlayPngPass(")
        self.assertIn("overlayPngIo.openFailure != OverlayOpenFailure::None || openResult != PNG_SUCCESS", render)
        self.assertIn("const int openLastError = png->getLastError()", render)
        self.assertIn('logOverlayPngIoFailure("open", path.c_str(), reason, openResult, openLastError)', render)

    def test_failure_log_exposes_decoder_and_sd_evidence_and_decode_short_reads(self):
        log = function_body(SOURCE, "void logOverlayPngIoFailure(")
        for field in (
            "result=%d",
            "last_error=%d",
            "file_opened=%d",
            "file_size_known=%d",
            "file_size=%d",
            "read_req=%d",
            "read_got=%d",
            "short_reads=%u",
        ):
            self.assertIn(field, log)

        render = function_body(SOURCE, "bool SleepActivity::renderOverlayPngPass(")
        self.assertIn("const int decodeLastError = png->getLastError()", render)
        self.assertIn('"short_sd_read" : "png_decode"', render)
        self.assertIn('"sleep_asset_read_fail" : "sleep_asset_decode_fail"', render)


if __name__ == "__main__":
    unittest.main()
