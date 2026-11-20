"""Copy the finished PlatformIO firmware to the project's artifacts folder."""

from pathlib import Path
import os
import shutil


Import("env")  # noqa: F821  -- provided by PlatformIO at script load


def copy_firmware(source, target, env):  # noqa: F811, ARG001
    """Keep a convenient, stable copy of the successful build output."""
    # SCons passes ``source`` as a node list.  Use PlatformIO's resolved build
    # path directly so this also works when the callback receives a single node.
    source_path = Path(env.subst("$BUILD_DIR/firmware.bin"))
    if not source_path.exists() and source:
        candidate = source[0] if isinstance(source, (list, tuple)) else source
        source_path = Path(str(candidate))
    configured_output = os.environ.get("NOOIR_FIRMWARE_OUTPUT")
    if configured_output:
        destination = Path(configured_output).expanduser()
    else:
        project_dir = Path(env.subst("$PROJECT_DIR"))
        artifacts_dir = project_dir / "artifacts"
        destination = artifacts_dir / "firmware.bin"
    if destination.exists():
        raise RuntimeError(f"Refusing to overwrite existing firmware artifact: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_path, destination)
    print(f"Firmware copied to: {destination}")


env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware)  # noqa: F821
