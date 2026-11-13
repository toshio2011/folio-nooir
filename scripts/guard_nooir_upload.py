"""Block direct upload/erase targets for experimental Pro/Classic builds."""

FORBIDDEN_TARGETS = {
    "upload",
    "uploadfs",
    "uploadbootloader",
    "uploadota",
    "upload_and_monitor",
    "erase",
    "erase_flash",
    "eraseall",
    "flash",
    "write_flash",
}


def validate_targets(device_id: str, targets: list[str]) -> None:
    if device_id.upper() not in {"X4PRO", "X4CLASSIC"}:
        return
    requested = {str(target).lower() for target in targets}
    blocked = sorted(requested & FORBIDDEN_TARGETS)
    if blocked:
        joined = ", ".join(blocked)
        raise RuntimeError(
            f"Refusing hardware-writing target(s) for experimental {device_id}: {joined}. "
            "Build artifacts must be reviewed and flashed manually only after device-specific safety validation."
        )


try:
    Import("env")  # noqa: F821 -- PlatformIO/SCons builtin
    from SCons.Script import COMMAND_LINE_TARGETS  # type: ignore

    _device_id = env.GetProjectOption("custom_nooir_device_id", "")  # noqa: F821
    validate_targets(_device_id, list(COMMAND_LINE_TARGETS))
except NameError:
    # Host tests import this module without SCons.
    pass
