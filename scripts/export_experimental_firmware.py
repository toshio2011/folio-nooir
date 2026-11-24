"""Export experimental Pro/Classic app images as non-flashing review artifacts."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

SUPPORTED_DEVICES = {"X4PRO", "X4CLASSIC"}
APP_ONLY = "ESP32-S3 application image only"
NOT_FOR_FLASHING = "EXPERIMENTAL-NOT-FOR-FLASHING"
RELEASE_VERSION = "1.6.7"


def partition_metadata(project_root: Path, device: str) -> dict:
    """Read the unchanged v1.6.6 partition table and expose its OTA app bound."""
    device_id = device.upper()
    if device_id not in SUPPORTED_DEVICES:
        raise ValueError(f"unsupported experimental device: {device}")
    table = Path(project_root) / "partitions.csv"
    if not table.is_file():
        raise ValueError("the original partitions.csv is missing")
    app_sizes = []
    for raw_line in table.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        columns = [part.strip() for part in line.split(",")]
        if len(columns) >= 5 and columns[1] == "app":
            app_sizes.append(int(columns[4], 0))
    if len(app_sizes) != 2 or app_sizes[0] != app_sizes[1]:
        raise ValueError("expected the original two equal v1.6.6 OTA app slots")
    return {
        "build_partition_table": "partitions.csv",
        "build_app_slot_size_bytes": app_sizes[0],
        "partition_table_sha256": hashlib.sha256(table.read_bytes()).hexdigest(),
    }


def _candidate_path(requested: Path, suffix: int) -> Path:
    if suffix == 1:
        return requested
    return requested.with_name(f"{requested.stem}-{suffix}{requested.suffix}")


def export_experimental_firmware(source: Path, requested_destination: Path, metadata: dict) -> tuple[Path, str]:
    """Copy one target-marked app image and a manifest without replacing files."""
    source = Path(source)
    requested_destination = Path(requested_destination)
    device = str(metadata.get("device", "")).upper()
    if device not in SUPPORTED_DEVICES or not metadata.get("experimental"):
        raise ValueError("Refusing to export: target must be an experimental Pro or Classic build")
    if metadata.get("flash_performed") is not False:
        raise ValueError("Refusing to export: artifact metadata must state flash_performed=false")
    app_size = metadata.get("build_app_slot_size_bytes")
    if not isinstance(app_size, int) or app_size <= 0:
        raise ValueError("Refusing to export without a bounded app-slot size")
    if not source.is_file():
        raise ValueError(f"firmware build output is missing: {source}")
    source_size = source.stat().st_size
    if source_size <= 0 or source_size > app_size:
        raise ValueError(f"Refusing to export: image exceeds app-slot size ({source_size} > {app_size})")
    marker = f"NOOIR_FIRMWARE_TARGET={device}".encode("ascii")
    image_bytes = source.read_bytes()
    if marker not in image_bytes:
        raise ValueError(f"Refusing to export: app image lacks target marker for {device}")
    digest = hashlib.sha256(image_bytes).hexdigest()

    requested_destination.parent.mkdir(parents=True, exist_ok=True)
    for suffix in range(1, 10000):
        destination = _candidate_path(requested_destination, suffix)
        manifest_path = destination.with_suffix(destination.suffix + ".manifest.json")
        try:
            with destination.open("xb") as output:
                output.write(image_bytes)
        except FileExistsError:
            continue
        try:
            document = {
                "device": device,
                "experimental": True,
                "release_version": RELEASE_VERSION,
                "label": NOT_FOR_FLASHING,
                "is_for_flashing": False,
                "flash_performed": False,
                "image_role": APP_ONLY,
                "file": destination.name,
                "size_bytes": source_size,
                "sha256": digest,
                **metadata,
                "physical_validation": "pending",
            }
            with manifest_path.open("x", encoding="utf-8", newline="\n") as manifest:
                json.dump(document, manifest, indent=2, sort_keys=True)
                manifest.write("\n")
        except FileExistsError:
            destination.unlink()
            continue
        except Exception:
            destination.unlink(missing_ok=True)
            raise
        return destination, digest
    raise FileExistsError("could not find an unused experimental artifact filename")


try:
    Import("env")  # noqa: F821 -- PlatformIO/SCons builtin
    _device = str(env.GetProjectOption("custom_nooir_device_id", "")).upper()  # noqa: F821
    if _device not in SUPPORTED_DEVICES:
        raise RuntimeError("experimental artifact export requires custom_nooir_device_id")
    _project_dir = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
    _source = Path(env.subst("$BUILD_DIR/firmware.bin"))  # noqa: F821
    _metadata = {
        "device": _device,
        "firmware_version": RELEASE_VERSION,
        "experimental": True,
        "flash_performed": False,
        "chip_target": "ESP32-S3",
        "board": env.GetProjectOption("board", ""),
        "flash_size_bytes": 16 * 1024 * 1024,
        "flash_mode": "DIO",
        "flash_frequency_hz": 80_000_000,
        "psram_size_bytes": 8 * 1024 * 1024,
        "psram_type": "OPI",
        "touch_enabled": _device == "X4PRO",
        "ble_enabled": False,
        **partition_metadata(_project_dir, _device),
    }
    _output_setting = os.environ.get("NOOIR_S3_OUTPUT_DIR")
    _output_root = Path(_output_setting).expanduser() if _output_setting else _project_dir / "artifacts"
    _target_label = {"X4PRO": "X4Pro", "X4CLASSIC": "X4Classic"}[_device]
    _requested = _output_root / f"FolioNooir_{RELEASE_VERSION}-{_target_label}-Experimental.bin"
    _git_head = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=_project_dir, check=True,
        capture_output=True, text=True,
    ).stdout.strip()
    _metadata.update({"source_commit": _git_head, "platformio_environment": env["PIOENV"]})

    def _export_after_build(source, target, env):  # noqa: ARG001
        output, digest = export_experimental_firmware(_source, _requested, _metadata)
        print(f"Experimental app-only image exported: {output}")
        print(f"SHA-256: {digest}")
        print("Flash and recovery validation: not performed")

    env.AddPostAction("$BUILD_DIR/firmware.bin", _export_after_build)  # noqa: F821
except NameError:
    # Host tests import this module without the PlatformIO/SCons runtime.
    pass
