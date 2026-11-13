"""Keep the current app wake-button API available in the host simulator."""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


SIMULATOR_ENVS = {
    "simulator_x4",
    "simulator_x3",
    "simulator_x4pro",
    "simulator_x4classic",
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
        / "HalGPIO.h"
    )
    if not simulator_header.is_file():
        print("Simulator GPIO compatibility: simulator dependency not installed yet")
    else:
        header = simulator_header.read_text(encoding="utf-8")
        signature = "  bool verifyPowerButtonWakeup(uint16_t, bool)"
        if signature not in header:
            anchor = "  bool verifyPowerButtonWakeup();\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalGPIO.h changed: wakeup verifier missing")
            header = header.replace(
                anchor,
                anchor
                + "  bool verifyPowerButtonWakeup(uint16_t, bool) { return true; }\n",
                1,
            )
            simulator_header.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator GPIO with current wake-button overload")
        else:
            print("Simulator GPIO compatibility: wake-button overload already exists")
