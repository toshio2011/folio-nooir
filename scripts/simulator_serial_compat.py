"""Provide the variadic serial method used by shared Nooir logging."""

from pathlib import Path

Import("env")  # noqa: F821 -- provided by PlatformIO

SIMULATOR_ENVS = {
    "simulator_x4",
    "simulator_x3",
    "simulator_x4pro",
    "simulator_x4classic",
    "simulator_nooir167_x4pro",
    "simulator_nooir167_x4classic",
}

if str(env.get("PIOENV") or "") in SIMULATOR_ENVS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    header_path = (
        project_dir
        / ".pio"
        / "libdeps"
        / str(env.get("PIOENV"))
        / "simulator"
        / "src"
        / "HardwareSerial.h"
    )
    if header_path.is_file():
        header = header_path.read_text(encoding="utf-8")
        if "int vprintf(const char *format, va_list args)" not in header:
            include_anchor = "#include <cstdio>\n"
            if include_anchor not in header:
                raise RuntimeError("Simulator HardwareSerial.h changed: cstdio include missing")
            header = header.replace(
                include_anchor,
                "#include <cstdarg>\n" + include_anchor,
                1,
            )
            method_anchor = "  void begin(unsigned long baud) {}\n"
            if method_anchor not in header:
                raise RuntimeError("Simulator HardwareSerial.h changed: begin() missing")
            header = header.replace(
                method_anchor,
                method_anchor
                + "  int vprintf(const char *format, va_list args) {\n"
                + "    return std::vfprintf(stderr, format, args);\n"
                + "  }\n",
                1,
            )
            header_path.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator serial with vprintf logging support")
    if str(env.get("PIOENV") or "") in {
        "simulator_nooir167_x4pro",
        "simulator_nooir167_x4classic",
    }:
        stub_path = header_path.parent / "firmware_link_stubs.cpp"
        if stub_path.is_file():
            stub = stub_path.read_text(encoding="utf-8")
            start_marker = "// ---------------------------------------------------------------------------\n// MySerialImpl\n"
            end_marker = "// ---------------------------------------------------------------------------\n// uzlib checksum stubs"
            if "MySerialImpl is defined in the project Logging.cpp" not in stub:
                start = stub.find(start_marker)
                end = stub.find(end_marker, start + len(start_marker))
                if start < 0 or end < 0:
                    raise RuntimeError("Simulator firmware_link_stubs.cpp changed: serial block markers missing")
                stub = (
                    stub[:start]
                    + "// MySerialImpl is defined in the project Logging.cpp.\n\n"
                    + stub[end:]
                )
                stub_path.write_text(stub, encoding="utf-8", newline="")
                print("Removed simulator link-stub serial duplicates for current Nooir logger")