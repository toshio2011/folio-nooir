"""Extend the host PNGdec shim with Nooir's interlace safety query.

The patch is intentionally best-effort: current CrossPoint simulator releases
may already provide ``PNG::isInterlaced()`` or may have changed their internal
decoder layout. In either case leave the dependency untouched and let the
normal compiler report a genuinely incompatible API instead of aborting all
other simulator compatibility scripts.
"""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


def patch_png_header(text):
    if "isInterlaced(" in text:
        return text, "already provides isInterlaced()"

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

    anchor = "  int getHeight() const { return image_.height; }\n"
    if anchor not in text:
        return None, "PNGdec API shape changed"
    text = text.replace(anchor, anchor + declaration, 1)

    if marker not in text:
        anchor = "private:\n"
        if anchor not in text:
            return None, "PNGdec private section missing"
        text = text.replace(anchor, helper + "\n" + anchor, 1)

    if "bool interlaced_{false};" not in text:
        anchor = "  simulator_image::DecodedImage image_;\n"
        if anchor not in text:
            return None, "PNGdec image storage shape changed"
        text = text.replace(anchor, anchor + "  bool interlaced_{false};\n", 1)

    if "interlaced_ = hasInterlaceMethod(encoded);" not in text:
        anchor = "    std::vector<uint8_t> encoded(static_cast<size_t>(size));\n"
        if anchor not in text:
            return None, "PNGdec decode buffer shape changed"
        text = text.replace(anchor, anchor + "    interlaced_ = false;\n", 1)
        anchor = "    closeCb(handle);\n\n    if (totalRead <= 0 ||\n"
        if anchor not in text:
            return None, "PNGdec decode close path changed"
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
    return text, "patched with IHDR interlace detection"


simulator_env = str(env.get("PIOENV") or "")
if simulator_env in ("simulator_x4", "simulator_x3"):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    png_path = project_dir / ".pio" / "libdeps" / simulator_env / "simulator" / "src" / "PNGdec.h"

    if not png_path.is_file():
        print("Simulator PNG compatibility: simulator dependency not installed yet")
    else:
        original = png_path.read_text(encoding="utf-8")
        patched, status = patch_png_header(original)
        if patched is None:
            print(f"Simulator PNG compatibility: {status}; leaving it unchanged")
        elif patched != original:
            png_path.write_text(patched, encoding="utf-8", newline="")
            print(f"Simulator PNG compatibility: {status}")
        else:
            print(f"Simulator PNGdec {status}")
