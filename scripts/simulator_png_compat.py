"""Extend the host PNGdec shim with the safety query used by Nooir.

The device PNGdec exposes ``PNG::isInterlaced()`` and SleepActivity rejects
interlaced overlays before decoding them.  The simulator's stb_image-backed
PNG shim decodes interlaced PNGs, but previously did not expose the PNG IHDR
interlace method.  Add that query only to the downloaded simulator header.
"""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


if env.get("PIOENV") == "simulator_x4":
    project_dir = Path(env.subst("$PROJECT_DIR"))
    png_path = (
        project_dir
        / ".pio"
        / "libdeps"
        / "simulator_x4"
        / "simulator"
        / "src"
        / "PNGdec.h"
    )

    if not png_path.is_file():
        print("Simulator PNG compatibility: simulator dependency not installed yet")
    else:
        text = png_path.read_text(encoding="utf-8")
        declaration = "  int isInterlaced() const { return interlaced_ ? 1 : 0; }\n"
        marker = "// Folio Nooir simulator PNG compatibility"
        helper = r'''  // Folio Nooir simulator PNG compatibility.
  static bool hasInterlaceMethod(const std::vector<uint8_t> &encoded) {
    // PNG signature (8), length (4), IHDR type (4), then IHDR data.
    // The interlace method is IHDR byte 12, absolute offset 28.
    if (encoded.size() < 29 || encoded[0] != 0x89 || encoded[1] != 'P' ||
        encoded[2] != 'N' || encoded[3] != 'G' || encoded[4] != 0x0D ||
        encoded[5] != 0x0A || encoded[6] != 0x1A || encoded[7] != 0x0A ||
        encoded[12] != 'I' || encoded[13] != 'H' || encoded[14] != 'D' ||
        encoded[15] != 'R') {
      return false;
    }
    return encoded[28] == 1;
  }
'''

        if "int isInterlaced() const" not in text:
            anchor = "  int getHeight() const { return image_.height; }\n"
            if anchor not in text:
                raise RuntimeError("Simulator PNGdec.h changed: cannot add isInterlaced")
            text = text.replace(anchor, anchor + declaration, 1)

        if marker not in text:
            anchor = "private:\n"
            if anchor not in text:
                raise RuntimeError("Simulator PNGdec.h changed: private section missing")
            text = text.replace(anchor, helper + "\n" + anchor, 1)

        if "bool interlaced_{false};" not in text:
            anchor = "  simulator_image::DecodedImage image_;\n"
            if anchor not in text:
                raise RuntimeError("Simulator PNGdec.h changed: image storage missing")
            text = text.replace(anchor, anchor + "  bool interlaced_{false};\n", 1)

        if "interlaced_ = hasInterlaceMethod(encoded);" not in text:
            anchor = "    std::vector<uint8_t> encoded(static_cast<size_t>(size));\n"
            if anchor not in text:
                raise RuntimeError("Simulator PNGdec.h changed: encoded buffer missing")
            text = text.replace(anchor, anchor + "    interlaced_ = false;\n", 1)
            anchor = "    closeCb(handle);\n\n    if (totalRead <= 0 ||\n"
            if anchor not in text:
                raise RuntimeError("Simulator PNGdec.h changed: decode path missing")
            text = text.replace(
                anchor,
                "    closeCb(handle);\n    interlaced_ = hasInterlaceMethod(encoded);\n\n    if (totalRead <= 0 ||\n",
                1,
            )

        text = text.replace(
            "  void close() { image_ = simulator_image::DecodedImage{}; }\n",
            "  void close() {\n"
            "    image_ = simulator_image::DecodedImage{};\n"
            "    interlaced_ = false;\n"
            "  }\n",
            1,
        )
        png_path.write_text(text, encoding="utf-8", newline="")
        print("Patched simulator PNGdec with IHDR interlace detection")
