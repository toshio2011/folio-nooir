#!/usr/bin/env python3
"""Prepare the licensed ESP32-C3 ROM image for isolated emulator validation.

The pinned eink-qemu revision contains upstream ROM blobs without an explicit
per-file redistribution license. This script instead takes Espressif's
Apache-2.0 ESP32-C3 rev3 ROM ELF release and verifies its archive, ELF and
flat-output hashes. The ordinary mode creates the original PT_LOAD flat image.
The explicit --experimental-licensed-elf-startup mode reconstructs the ROM's
copy-table source data and aliased layout section from that licensed ELF. The
result may be redistributed with all pinned notices in LICENSES/; this does
not clear the complete emulator package until its GPL corresponding source
and package audit pass.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import subprocess
import struct
import tarfile
import tempfile
from pathlib import Path
from urllib.request import urlopen

from elftools.elf.elffile import ELFFile

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
EXPERIMENTAL_ROM_BYTES = 0x60000
EXPERIMENTAL_ROM_SHA256 = "3a69ac75662367dc99fbd5b8ddf265515b6288e79e51e4c4168c597aced7b79d"
COPY_TABLE_BASE = 0x40059200
COPY_TABLE_BYTES = 0x200
ROM_BASE = 0x40000000
DRAM_START = 0x3FC80000
DRAM_END = 0x3FCE0000
LAYOUT_SECTION_SHA256 = "61d28bfe60e74152e262f11758fe4c0fb5eb794e9ae3b80a62f0631cae7b221b"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def flat_rom_from_elf(elf_bytes: bytes) -> bytes:
    """Reconstruct the flat image from file-backed, allocated ELF sections.

    A section's load address is taken from the unique file-backed PT_LOAD that
    contains its file range. This is intentionally limited to the exact pinned
    ELF and is checked against OUTPUT_BYTES and OUTPUT_SHA256 by the caller.
    """
    elf = ELFFile(io.BytesIO(elf_bytes))
    segments = [
        segment
        for segment in elf.iter_segments()
        if segment["p_type"] == "PT_LOAD" and segment["p_filesz"] > 0
    ]
    sections: list[tuple[int, bytes, str]] = []
    for section in elf.iter_sections():
        header = section.header
        if header["sh_type"] != "SHT_PROGBITS" or not (header["sh_flags"] & 0x2) or header["sh_size"] == 0:
            continue
        start = header["sh_offset"]
        end = start + header["sh_size"]
        lmas = [
            segment["p_paddr"] + (header["sh_addr"] - segment["p_vaddr"])
            for segment in segments
            if start >= segment["p_offset"]
            and end <= segment["p_offset"] + segment["p_filesz"]
        ]
        if len(lmas) != 1:
            raise SystemExit(
                f"allocated ELF section {section.name} has {len(lmas)} file-backed PT_LOAD mappings"
            )
        sections.append((lmas[0], section.data(), section.name))
    if not sections:
        raise SystemExit("licensed ROM ELF has no allocated file-backed sections")

    load_base = min(address for address, _data, _name in sections)
    load_end = max(address + len(data) for address, data, _name in sections)
    if load_base != ROM_BASE or load_end - load_base != OUTPUT_BYTES:
        raise SystemExit(
            "licensed ROM ELF load range differs from the reviewed flat image: "
            f"base={load_base:#x}, bytes={load_end - load_base}"
        )

    output = bytearray(load_end - load_base)
    written: list[tuple[int, int, str]] = []
    for address, data, name in sorted(sections):
        start = address - load_base
        end = start + len(data)
        for prior_start, prior_end, prior_name in written:
            if start < prior_end and prior_start < end:
                raise SystemExit(f"ELF load sections overlap: {name} and {prior_name}")
        output[start:end] = data
        written.append((start, end, name))
    return bytes(output)


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


def _one_symbol(symtab, name: str):
    symbols = symtab.get_symbol_by_name(name) or []
    if len(symbols) != 1:
        raise SystemExit(f"expected exactly one {name} symbol, found {len(symbols)}")
    return symbols[0]


def build_experimental_rom(elf_bytes: bytes, flat_rom: bytes) -> tuple[bytes, dict]:
    """Reconstruct reviewed ROM copy-table data and the C3 ROM layout pointer.

    This mode is intentionally opt-in. It uses only bytes from Espressif's
    hash-verified Apache-2.0 ROM ELF and refuses any unreviewed layout.
    """
    if len(flat_rom) != OUTPUT_BYTES or sha256(flat_rom) != OUTPUT_SHA256:
        raise SystemExit("experimental construction requires the exact reviewed PT_LOAD flat ROM")
    elf = ELFFile(io.BytesIO(elf_bytes))
    if elf.elfclass != 32 or not elf.little_endian or elf.header["e_machine"] != "EM_RISCV":
        raise SystemExit("unexpected Espressif ROM ELF class, byte order, or architecture")

    sections = list(elf.iter_sections())
    section_indexes = {section.name: index for index, section in enumerate(sections)}
    symtab = elf.get_section_by_name(".symtab")
    if symtab is None:
        raise SystemExit("licensed ROM ELF has no symbol table")

    # The ROM startup copy table contains ROM-source/D-RAM-destination rows.
    # Match each initialized row only to its exact ELF PROGBITS section.
    table_offset = COPY_TABLE_BASE - ROM_BASE
    if table_offset < 0 or table_offset + COPY_TABLE_BYTES > len(flat_rom):
        raise SystemExit("startup copy table is outside the reviewed flat ROM")
    rows = []
    source_ranges = []
    destination_ranges = []
    patched_sections = []
    for row_index in range(COPY_TABLE_BYTES // 16):
        destination, end, source, _metadata = struct.unpack_from(
            "<IIII", flat_rom, table_offset + row_index * 16
        )
        if end < destination:
            raise SystemExit(f"malformed startup copy-table row {row_index}")
        if end == destination:
            continue
        size = end - destination
        if not (DRAM_START <= destination < end <= DRAM_END):
            raise SystemExit(f"copy-table row {row_index} destination is outside C3 DRAM")
        if not (ROM_BASE <= source and source + size <= ROM_BASE + len(flat_rom)):
            raise SystemExit(f"copy-table row {row_index} source is outside the ROM image")
        matches = [
            section for section in sections
            if section["sh_addr"] == destination and section["sh_size"] == size
        ]
        if len(matches) != 1:
            raise SystemExit(
                f"copy-table row {row_index} must match one exact ELF section; found {len(matches)}"
            )
        section = matches[0]
        flags = int(section["sh_flags"])
        if section["sh_type"] != "SHT_PROGBITS" or not flags & 0x1 or flags & 0x2:
            raise SystemExit(f"copy-table row {row_index} is not writable non-alloc PROGBITS data")
        source_offset = source - ROM_BASE
        if any(flat_rom[source_offset:source_offset + size]):
            raise SystemExit(f"copy-table row {row_index} source slot is not zero-filled")
        data = section.data()
        if len(data) != size:
            raise SystemExit(f"copy-table row {row_index} ELF section data has the wrong size")
        source_ranges.append((source, source + size))
        destination_ranges.append((destination, end))
        rows.append({
            "row": row_index,
            "section": section.name,
            "destination": hex(destination),
            "source": hex(source),
            "size": size,
            "section_sha256": sha256(data),
        })
        patched_sections.append((source_offset, data))

    for ranges, description in (
        (sorted(source_ranges), "ROM source"),
        (sorted(destination_ranges), "DRAM destination"),
    ):
        if any(previous[1] > current[0] for previous, current in zip(ranges, ranges[1:])):
            raise SystemExit(f"startup copy-table {description} ranges overlap")
    if len(rows) != 14 or sum(row["size"] for row in rows) != 1316:
        raise SystemExit("startup copy-table section inventory differs from reviewed ELF mapping")

    candidate = bytearray(EXPERIMENTAL_ROM_BYTES)
    candidate[:len(flat_rom)] = flat_rom
    for (source_offset, data), row in zip(patched_sections, rows):
        candidate[source_offset:source_offset + len(data)] = data
    copydata_sha256 = sha256(candidate[:len(flat_rom)])
    if copydata_sha256 != "2202e4f5c946fb2f8fc3d77e48f71ce29758b42913e7da868b955f0f6813fb72":
        raise SystemExit("copy-table-only ROM reconstruction did not match the reviewed hash")

    # .rodata.interface has a zero-sized PT_LOAD physical-address anchor. The
    # anchor places this data in IROM, whose DROM alias is the section's VMA.
    interface = elf.get_section_by_name(".rodata.interface")
    if interface is None or interface["sh_type"] != "SHT_PROGBITS":
        raise SystemExit("licensed ELF .rodata.interface section is missing or has the wrong type")
    if (
        int(interface["sh_flags"]) != 0x1
        or interface["sh_addr"] != 0x3FF1EE3C
        or interface["sh_size"] != 0x11C4
        or sha256(interface.data()) != LAYOUT_SECTION_SHA256
    ):
        raise SystemExit("licensed ELF .rodata.interface layout or hash differs from review")
    anchor_address = 0x4005EE3C
    anchors = [
        segment for segment in elf.iter_segments()
        if segment["p_type"] == "PT_LOAD"
        and segment["p_paddr"] == anchor_address
        and segment["p_filesz"] == 0
        and segment["p_memsz"] == 0
    ]
    if len(anchors) != 1 or anchor_address != 0x40040000 + (interface["sh_addr"] - 0x3FF00000):
        raise SystemExit(".rodata.interface has no unique reviewed DROM-alias load anchor")
    interface_offset = anchor_address - ROM_BASE
    interface_data = interface.data()
    if interface_offset < len(flat_rom) or interface_offset + len(interface_data) != EXPERIMENTAL_ROM_BYTES:
        raise SystemExit(".rodata.interface mapping overlaps flat ROM or exceeds the C3 IROM window")
    if any(candidate[interface_offset:interface_offset + len(interface_data)]):
        raise SystemExit(".rodata.interface destination is not zero-filled")

    pointer = _one_symbol(symtab, "ets_rom_layout_p")
    layout = _one_symbol(symtab, "ets_rom_layout")
    if pointer["st_value"] != 0x3FF1FFFC or layout["st_value"] != 0x3FF1BE3C:
        raise SystemExit("licensed ELF ROM layout symbols differ from the reviewed addresses")
    if pointer["st_shndx"] != section_indexes[".rodata.interface"] or layout["st_shndx"] != section_indexes[".rodata"]:
        raise SystemExit("licensed ELF ROM layout symbols map to unexpected sections")
    pointer_offset = pointer["st_value"] - interface["sh_addr"]
    pointer_bytes = interface_data[pointer_offset:pointer_offset + 4]
    if len(pointer_bytes) != 4 or int.from_bytes(pointer_bytes, "little") != layout["st_value"]:
        raise SystemExit("licensed ELF ets_rom_layout_p value does not point to ets_rom_layout")
    candidate[interface_offset:interface_offset + len(interface_data)] = interface_data
    if candidate[interface_offset + pointer_offset:interface_offset + pointer_offset + 4] != pointer_bytes:
        raise SystemExit("ROM layout pointer was not preserved in the constructed image")

    output = bytes(candidate)
    if len(output) != EXPERIMENTAL_ROM_BYTES or sha256(output) != EXPERIMENTAL_ROM_SHA256:
        raise SystemExit("experimental C3 ROM reconstruction did not match the reviewed output hash")
    manifest = {
        "purpose": "experimental licensed-ELF startup correction; Nooir beta candidate",
        "esp_rom_elf_release": RELEASE,
        "archive_sha256": ARCHIVE_SHA256,
        "esp_rom_elf_sha256": ELF_SHA256,
        "base_pt_load_rom_bytes": len(flat_rom),
        "base_pt_load_rom_sha256": sha256(flat_rom),
        "startup_copy_table": {
            "address": hex(COPY_TABLE_BASE),
            "matched_sections": len(rows),
            "initialized_bytes": sum(row["size"] for row in rows),
            "rows": rows,
            "copydata_only_sha256": copydata_sha256,
        },
        "layout_pointer_section": {
            "name": interface.name,
            "vma": hex(interface["sh_addr"]),
            "rom_paddr_anchor": hex(anchor_address),
            "rom_offset": hex(interface_offset),
            "size": interface["sh_size"],
            "sha256": sha256(interface_data),
            "pointer_symbol": pointer.name,
            "pointer_address": hex(pointer["st_value"]),
            "pointer_value": hex(int.from_bytes(pointer_bytes, "little")),
            "target_symbol": layout.name,
            "target_address": hex(layout["st_value"]),
        },
        "output_bytes": len(output),
        "output_sha256": sha256(output),
        "stock_rom_bytes_used": False,
        "redistribution_review_complete": True,
        "redistribution_basis": "Espressif Apache-2.0 ROM ELF; ship all pinned ROM and embedded-component notices from LICENSES/ESP32C3-ROM-NOTICES.md",
    }
    return output, manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--objcopy", default=os.environ.get("RISCV_OBJCOPY", "riscv64-unknown-elf-objcopy"))
    parser.add_argument(
        "--archive",
        type=Path,
        help="use a previously downloaded official archive (still hash-verified)",
    )
    parser.add_argument(
        "--experimental-licensed-elf-startup",
        action="store_true",
        help="include the reviewed Espressif ELF copy-table data and ROM layout section for the experimental Nooir beta",
    )
    parser.add_argument(
        "--python-elf-conversion",
        action="store_true",
        help="reconstruct the flat ROM from file-backed PT_LOAD sections using the pinned ELF metadata",
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
        if args.python_elf_conversion:
            result = flat_rom_from_elf(elf)
        else:
            elf_path = temp / ELF_NAME
            bin_path = temp / "esp32c3-rom.bin"
            elf_path.write_bytes(elf)
            subprocess.run([args.objcopy, "-O", "binary", str(elf_path), str(bin_path)], check=True)
            result = bin_path.read_bytes()
            python_result = flat_rom_from_elf(elf)
            if result != python_result:
                raise SystemExit("objcopy output differs from the section-mapped Python reconstruction")

    digest = sha256(result)
    if len(result) != OUTPUT_BYTES or digest != OUTPUT_SHA256:
        raise SystemExit(
            "converted ESP32-C3 ROM did not match the reviewed output: "
            f"bytes={len(result)}, sha256={digest}"
        )
    manifest_path = output.with_suffix(output.suffix + ".manifest.json")
    if args.experimental_licensed_elf_startup:
        result, manifest = build_experimental_rom(elf, result)
        if output.exists() or manifest_path.exists():
            raise SystemExit(f"refusing to overwrite experimental ROM or manifest: {output}")
        temporary = output.with_name(output.name + ".tmp")
        temporary_manifest = manifest_path.with_name(manifest_path.name + ".tmp")
        if temporary.exists() or temporary_manifest.exists():
            raise SystemExit("refusing to overwrite an existing temporary ROM output or manifest")
        temporary.write_bytes(result)
        temporary_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, output)
        os.replace(temporary_manifest, manifest_path)
        print(f"Experimental manifest: {manifest_path}")
        print("ROM redistribution review: complete with pinned notices; full package audit remains required")
    else:
        if output.exists():
            raise SystemExit(f"refusing to overwrite existing ROM output: {output}")
        temporary = output.with_name(output.name + ".tmp")
        if temporary.exists():
            raise SystemExit(f"refusing to overwrite temporary ROM output: {temporary}")
        temporary.write_bytes(result)
        os.replace(temporary, output)
    print(f"Prepared licensed ESP32-C3 ROM: {output}")
    print(f"Espressif release: {RELEASE}; ELF SHA-256: {ELF_SHA256}")
    print(f"Output: {len(result)} bytes; SHA-256: {sha256(result)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
