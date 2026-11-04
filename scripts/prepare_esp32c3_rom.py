#!/usr/bin/env python3
"""Prepare the licensed ESP32-C3 ROM candidate for compatibility smoke tests.

The pinned eink-qemu revision contains upstream ROM blobs without an explicit
per-file redistribution license. This script instead takes Espressif's
Apache-2.0 ESP32-C3 rev3 ROM ELF release, verifies both archive and ELF hashes,
and converts it to the flat binary expected by eink-qemu. The pinned engine's
flat-ROM startup currently fails with this candidate; the result is not cleared
for packaging unless the unchanged X3/X4 smoke passes.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import tarfile
import tempfile
from pathlib import Path
from urllib.request import urlopen

RELEASE = "20260528"
ARCHIVE_URL = (
    "https://github.com/espressif/esp-rom-elfs/releases/download/"
    f"{RELEASE}/esp-rom-elfs-{RELEASE}.tar.gz"
)
ARCHIVE_SHA256 = "caa463d3cbef2430a5a35847c1d9f2f152403b17a802050927ff60c8da54fe46"
ELF_NAME = "esp32c3_rev3_rom.elf"
ELF_SHA256 = "19ac22e08707df926fb0cf4c54795d4067b983fae8635f396ded173a6d78fc3c"
OUTPUT_BYTES = 387784
OUTPUT_SHA256 = "3336ae473a063be8dae6790bd45fee4506f2cf273aa36f3dfd9e0495ecb665cc"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_archive(path: Path) -> bytes:
    if not path.exists():
        print(f"Downloading Espressif ROM ELF release {RELEASE}")
        with urlopen(ARCHIVE_URL, timeout=120) as response:
            payload = response.read()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    payload = path.read_bytes()
    digest = sha256(payload)
    if digest != ARCHIVE_SHA256:
        raise SystemExit(
            f"Espressif ROM archive hash mismatch: expected {ARCHIVE_SHA256}, got {digest}"
        )
    return payload


def extract_rom_elf(payload: bytes) -> bytes:
    with tarfile.open(fileobj=__import__("io").BytesIO(payload), mode="r:gz") as archive:
        matches = [member for member in archive.getmembers() if Path(member.name).name == ELF_NAME]
        if len(matches) != 1 or not matches[0].isfile():
            raise SystemExit(f"expected exactly one regular {ELF_NAME} in Espressif release archive")
        stream = archive.extractfile(matches[0])
        if stream is None:
            raise SystemExit(f"could not read {ELF_NAME} from Espressif release archive")
        elf = stream.read()
    digest = sha256(elf)
    if digest != ELF_SHA256:
        raise SystemExit(f"Espressif ROM ELF hash mismatch: expected {ELF_SHA256}, got {digest}")
    return elf


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--objcopy", default=os.environ.get("RISCV_OBJCOPY", "riscv64-unknown-elf-objcopy"))
    parser.add_argument(
        "--archive",
        type=Path,
        help="use a previously downloaded official archive (still hash-verified)",
    )
    args = parser.parse_args()

    output = args.output.resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing ROM output: {output}")
    archive_path = args.archive.resolve() if args.archive else output.parent / f"esp-rom-elfs-{RELEASE}.tar.gz"
    elf = extract_rom_elf(read_archive(archive_path))
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="nooir-c3-rom-") as temp_dir:
        temp = Path(temp_dir)
        elf_path = temp / ELF_NAME
        bin_path = temp / "esp32c3-rom.bin"
        elf_path.write_bytes(elf)
        subprocess.run([args.objcopy, "-O", "binary", str(elf_path), str(bin_path)], check=True)
        result = bin_path.read_bytes()

    digest = sha256(result)
    if len(result) != OUTPUT_BYTES or digest != OUTPUT_SHA256:
        raise SystemExit(
            "converted ESP32-C3 ROM did not match the reviewed output: "
            f"bytes={len(result)}, sha256={digest}"
        )
    temporary = output.with_name(output.name + ".tmp")
    if temporary.exists():
        raise SystemExit(f"refusing to overwrite temporary ROM output: {temporary}")
    temporary.write_bytes(result)
    os.replace(temporary, output)
    print(f"Prepared licensed ESP32-C3 ROM: {output}")
    print(f"Espressif release: {RELEASE}; ELF SHA-256: {ELF_SHA256}")
    print(f"Output: {len(result)} bytes; SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
