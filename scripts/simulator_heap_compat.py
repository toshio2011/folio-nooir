"""Expose the DMA heap capability constant used by the unchanged app sources."""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


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
    simulator_header = (
        project_dir
        / ".pio"
        / "libdeps"
        / str(env.get("PIOENV"))
        / "simulator"
        / "src"
        / "esp_heap_caps.h"
    )
    if not simulator_header.is_file():
        print("Simulator heap compatibility: simulator dependency not installed yet")
    else:
        header = simulator_header.read_text(encoding="utf-8")
        if "MALLOC_CAP_DMA" not in header:
            anchor = "constexpr unsigned MALLOC_CAP_8BIT = 1U << 3;\n"
            if anchor not in header:
                raise RuntimeError("Simulator heap header changed: MALLOC_CAP_8BIT missing")
            header = header.replace(
                anchor,
                anchor + "constexpr unsigned MALLOC_CAP_DMA = 1U << 4;\n",
                1,
            )
            simulator_header.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator heap capabilities with MALLOC_CAP_DMA")
        else:
            print("Simulator heap compatibility: MALLOC_CAP_DMA already exists")
