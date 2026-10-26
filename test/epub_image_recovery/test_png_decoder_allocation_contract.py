"""Contracts for bounded PNG dimension probing and decoder allocation recovery."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONVERTER = (ROOT / "lib/Epub/Epub/converters/PngToFramebufferConverter.cpp").read_text(encoding="utf-8")


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


class PngDecoderAllocationContract(unittest.TestCase):
    def test_dimension_probe_reads_bounded_validated_header_only(self):
        dimensions = function_body(CONVERTER, "bool PngToFramebufferConverter::getDimensionsStatic(")
        self.assertIn("std::array<uint8_t, 33>", dimensions)
        self.assertIn("pngheader::parseDimensions", dimensions)
        self.assertNotIn("new (std::nothrow) PNG", dimensions)
        self.assertNotIn("png->open", dimensions)

    def test_decode_preflights_real_contiguous_decoder_size(self):
        fits = function_body(CONVERTER, "bool pngDecoderFitsHeap(")
        self.assertIn("sizeof(PNG)", fits)
        self.assertIn("largestBlock >= sizeof(PNG)", fits)
        allocation = function_body(CONVERTER, "bool PngToFramebufferConverter::decodeToFramebuffer(")
        self.assertIn("ESP.getMaxAllocHeap()", allocation)
        self.assertIn("releaseRebuildableReaderFonts(renderer)", allocation)
        self.assertIn("decoder_workspace_retry", allocation)

    def test_decoder_recovery_uses_existing_reader_font_cache_cleanup(self):
        cleanup = function_body(CONVERTER, "void releaseRebuildableReaderFonts(")
        self.assertIn("fontCache->releaseSdFontCaches()", cleanup)
        self.assertIn("sdFontSystem.releaseReaderCaches()", cleanup)


if __name__ == "__main__":
    unittest.main()
