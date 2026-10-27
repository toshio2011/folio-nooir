"""Keep optional HTTP download diagnostics valid in the wolfSSL build path."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/HttpDownloader.cpp").read_text(encoding="utf-8")


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


class HttpDownloaderDiagnosticsContract(unittest.TestCase):
    def test_wolfssl_diagnostics_report_the_redirect_count(self):
        body = function_body(
            SOURCE,
            "HttpDownloader::DownloadError runGetWolf(",
        )
        self.assertIn('fontNetworkDiagnostic("tls_begin", 0, static_cast<size_t>(redirectsFollowed))', body)
        self.assertIn('fontNetworkDiagnostic("tls_begin_fail", -1, static_cast<size_t>(redirectsFollowed))', body)
        self.assertIn('fontNetworkDiagnostic("tls_get", 0, static_cast<size_t>(redirectsFollowed))', body)
        self.assertNotIn(", hop)", body)


if __name__ == "__main__":
    unittest.main()
