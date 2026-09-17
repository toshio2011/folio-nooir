"""Provide the tiny BoardConfig surface used by the host simulator build.

The physical BoardConfig library is intentionally excluded from native builds.
Current shared settings/startup code still needs its touch capability query and
power-rail hook, so create a host-only header inside the downloaded simulator
dependency when that dependency does not provide one.
"""

from pathlib import Path


Import("env")  # noqa: F821 -- provided by PlatformIO


if str(env.get("PIOENV") or "") in ("simulator_x4", "simulator_x3"):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = project_dir / ".pio" / "libdeps" / str(env.get("PIOENV")) / "simulator"
    header_path = simulator_dir / "src" / "BoardConfig.h"

    if not header_path.is_file():
        header_path.write_text(
            """#pragma once

// Host-only compatibility for current Nooir shared startup/settings code.
// Physical BoardConfig remains excluded from native simulator builds.
namespace BoardConfig {
inline bool hasTouch() { return false; }
inline void holdPowerRails() {}
}  // namespace BoardConfig
""",
            encoding="utf-8",
            newline="",
        )
        print("Created simulator BoardConfig compatibility header")
