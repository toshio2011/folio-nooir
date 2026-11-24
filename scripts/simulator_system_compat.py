"""Expose the fixed power-on reset reason used by the host simulator."""

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
        / "esp_system.h"
    )
    if not simulator_header.is_file():
        print("Simulator system compatibility: simulator dependency not installed yet")
    else:
        header = simulator_header.read_text(encoding="utf-8")
        if "esp_reset_reason(" not in header:
            anchor = "inline void esp_restart() {}\n"
            if anchor not in header:
                raise RuntimeError("Simulator esp_system.h changed: restart stub missing")
            header = header.replace(
                anchor,
                anchor
                + "// The native simulator always starts from a cold power-on.\n"
                + "inline int esp_reset_reason() { return 1; }\n",
                1,
            )
            simulator_header.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator reset reason to report power-on")
        else:
            print("Simulator system compatibility: reset reason already exists")

        if "esp_random(" not in header:
            anchor = "inline int esp_reset_reason() { return 1; }\n"
            if anchor not in header:
                raise RuntimeError("Simulator esp_system.h changed: reset-reason stub missing")
            header = header.replace(
                anchor,
                anchor
                + "// Deterministic host-only random source; no security use.\n"
                + "inline uint32_t esp_random() {\n"
                + "  static uint32_t state = 0x6D2B79F5U;\n"
                + "  state ^= state << 13;\n"
                + "  state ^= state >> 17;\n"
                + "  state ^= state << 5;\n"
                + "  return state;\n"
                + "}\n",
                1,
            )
            simulator_header.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator with deterministic esp_random()")
