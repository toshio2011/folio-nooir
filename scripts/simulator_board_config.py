"""Provide the tiny BoardConfig surface used by the host simulator build.

The physical BoardConfig library is intentionally excluded from native builds.
Current shared settings/startup code still needs its touch capability query and
power-rail hook, so create a host-only header inside the downloaded simulator
dependency when that dependency does not provide one.
"""

from pathlib import Path


Import("env")  # noqa: F821 -- provided by PlatformIO


SIMULATOR_ENVS = (
    "simulator_x4",
    "simulator_x3",
    "simulator_nooir167_x4pro",
    "simulator_nooir167_x4classic",
)

simulator_env = str(env.get("PIOENV") or "")
if simulator_env in SIMULATOR_ENVS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = project_dir / ".pio" / "libdeps" / simulator_env / "simulator"
    header_path = simulator_dir / "src" / "BoardConfig.h"

    if not header_path.is_file():
        has_touch = simulator_env == "simulator_nooir167_x4pro"
        touch_value = "true" if has_touch else "false"
        header_path.write_text(
            """#pragma once

// Host-only compatibility for current Nooir shared startup/settings code.
// Physical BoardConfig remains excluded from native simulator builds.
namespace BoardConfig {
inline bool hasTouch() { return TOUCH_VALUE; }
inline void holdPowerRails() {}
}  // namespace BoardConfig
""".replace("TOUCH_VALUE", touch_value),
            encoding="utf-8",
            newline="",
        )
        print("Created simulator BoardConfig compatibility header")
