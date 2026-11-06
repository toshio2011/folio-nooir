#!/usr/bin/env python3
"""Audit a packaged Nooir emulator before sharing the static bundle."""
from __future__ import annotations

import hashlib
import json
import sys
import tarfile
from pathlib import Path

PINNED_ENGINE = "d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc"
REQUIRED = (
    "index.html",
    "app.js",
    "style.css",
    "screenshot.css",
    "README.md",
    "dependencies.json",
    "engine/index.html",
    "engine/runtime.html",
    "engine/coi-serviceworker.js",
    "engine/coi-serviceworker.LICENSE.txt",
    "coi-serviceworker.js",
    "licenses/eink-qemu/COPYING",
    "licenses/eink-qemu/COPYING.LIB",
    "licenses/eink-qemu/LICENSE",
    "licenses/eink-qemu/README.md",
    "licenses/folio-nooir/LICENSE",
    "licenses/NOOIR-REFERENCE-PROJECTS.md",
    "licenses/EINK-QEMU-WEB-BUNDLE.md",
    "licenses/eink-qemu-components/README.md",
    "licenses/eink-qemu-boot/README.md",
    "licenses/eink-qemu-boot/LICENSE",
    "licenses/eink-qemu-boot/LICENSE.partitions",
    "licenses/CORRESPONDING-SOURCE.md",
    "licenses/ESP32C3-ROM-NOTICES.md",
    "licenses/esp32c3-rom/esp-rom-elfs-README.md",
    "licenses/esp32c3-rom/esp32c3-rom.bin.manifest.json",
    "licenses/esp32c3-rom/Apache-2.0.txt",
    "licenses/esp32c3-rom/esp-idf-COPYRIGHT.rst",
    "licenses/esp32c3-rom/Newlib-COPYING.NEWLIB",
    "licenses/esp32c3-rom/BSD-3-Clause.txt",
    "licenses/esp32c3-rom/CC0-1.0.txt",
    "licenses/emscripten/LICENSE",
    "sources/eink-qemu-d1b24a5f-corresponding-source.tar.gz",
    "NOOIR-WEB-EMULATOR-NOTICE.md",
)
COMPONENT_LICENSES = {
    "wasm-build-dependencies/zlib": {"LICENSE"},
    "wasm-build-dependencies/libffi": {"LICENSE"},
    "wasm-build-dependencies/pixman": {"COPYING"},
    "wasm-build-dependencies/glib": {"COPYING"},
    "wasm-build-dependencies/libgcrypt": {"COPYING"},
    "wasm-build-dependencies/libgpg-error": {"COPYING"},
    "wasm-build-dependencies/browser-net": {"COPYING", "LICENSE"},
    "wasm-build-dependencies/em-cache/ports/sdl2": {"LICENSE.txt"},
    "build-subprojects/berkeley-softfloat-3": {"COPYING.txt"},
    "build-subprojects/keycodemapdb": {"LICENSE.GPL2", "LICENSE.BSD"},
    "build-subprojects/dtc": {"GPL", "BSD-2-Clause", "README.license"},
    "linux-headers": {"COPYING"},
}
PACKAGE_ROOT_FILES = {
    "index.html", "app.js", "style.css", "screenshot.css", "README.md", "dependencies.json",
    "coi-serviceworker.js", "NOOIR-WEB-EMULATOR-NOTICE.md",
}
PACKAGE_ROOT_DIRECTORIES = {"engine", "licenses", "sources"}
TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".css", ".h", ".html", ".js", ".json", ".md",
    ".mjs", ".py", ".rst", ".sh", ".toml", ".txt", ".yaml", ".yml",
}
SECRET_PATTERNS = (
    ("GitHub token", r"(?:gh[pousr]_[A-Za-z0-9_]{30,}|github_pat_[A-Za-z0-9_]{30,})"),
    ("AWS access key", r"(?:AKIA|ASIA)[0-9A-Z]{16}"),
    ("private key block", r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    ("Slack token", r"xox[baprs]-[A-Za-z0-9-]{20,}"),
)
ALLOWED_SUPPORT_BINS = {
    "engine/boot/bootloader-esp32c3.bin",
    "engine/boot/nvs-crosspoint.bin",
    "engine/boot/partitions.bin",
    "engine/esp32c3-rom.bin",
}
SUPPORT_BIN_HASHES = {
    "engine/esp32c3-rom.bin": (393216, "3a69ac75662367dc99fbd5b8ddf265515b6288e79e51e4c4168c597aced7b79d"),
    "engine/boot/bootloader-esp32c3.bin": (18688, "46893df6619f0c7d7689fa309e76551e964e3d83d68c38ccb3e52ed8c3537226"),
    "engine/boot/nvs-crosspoint.bin": (20480, "67c80ffb02df062522216e33553f0691a9ddea06be794404365a66c90bc8e94f"),
    "engine/boot/partitions.bin": (3072, "bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce"),
}
ROM_SOURCE_ARCHIVE_SHA256 = "caa463d3cbef2430a5a35847c1d9f2f152403b17a802050927ff60c8da54fe46"
ROM_ELF_SHA256 = "19ac22e08707df926fb0cf4c54795d4067b983fae8635f396ded173a6d78fc3c"
LICENSE_FILE_BLOBS = {
    "licenses/esp32c3-rom/esp-rom-elfs-README.md": "0df304ef2d7c2ba4009c3e9931d98a951321dec2",
    "licenses/esp32c3-rom/Apache-2.0.txt": "261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64",
    "licenses/esp32c3-rom/esp-idf-COPYRIGHT.rst": "d6f42432fec445e5f05971147a8a1de6d2cbf93e",
    "licenses/esp32c3-rom/Newlib-COPYING.NEWLIB": "d056d18f74c923772ee53fffc131cded0ef26c78",
    "licenses/esp32c3-rom/BSD-3-Clause.txt": "086d3992cb3a8f7000127bda96d6cbe673c8e788",
    "licenses/esp32c3-rom/CC0-1.0.txt": "0e259d42c996742e9e3cba14c677129b2c1b6311",
}


def git_blob_sha1(data: bytes) -> str:
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_nooir_web_package.py PACKAGE_DIRECTORY")
    package = Path(sys.argv[1]).resolve(strict=True)
    errors: list[str] = []

    for entry in package.iterdir():
        if entry.is_dir() and entry.name not in PACKAGE_ROOT_DIRECTORIES:
            errors.append(f"unexpected top-level package directory: {entry.name}")
        elif entry.is_file() and entry.name not in PACKAGE_ROOT_FILES:
            errors.append(f"unexpected top-level package file: {entry.name}")
        elif not entry.is_file() and not entry.is_dir():
            errors.append(f"unexpected non-file package entry: {entry.name}")

    for relative in REQUIRED:
        if not (package / relative).is_file():
            errors.append(f"missing required package/license file: {relative}")
    for relative, expected_blob in LICENSE_FILE_BLOBS.items():
        path = package / relative
        if path.is_file() and git_blob_sha1(path.read_bytes()) != expected_blob:
            errors.append(f"upstream license/notice file differs from its pinned Git object: {relative}")

    component_root = package / "licenses/eink-qemu-components"
    for relative, expected_names in COMPONENT_LICENSES.items():
        directory = component_root / relative
        available_names = {
            path.name for path in directory.rglob("*") if path.is_file()
        } if directory.is_dir() else set()
        missing_names = expected_names - available_names
        if missing_names:
            errors.append(
                f"missing license/notice file(s) under {directory.relative_to(package).as_posix()}: "
                + ", ".join(sorted(missing_names))
            )

    metadata_path = package / "dependencies.json"
    if metadata_path.is_file():
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if metadata.get("engineRevision") != PINNED_ENGINE:
            errors.append("dependency metadata does not name the pinned eink-qemu revision")
        if metadata.get("verifiedNooirDevices") != ["x3", "x4"]:
            errors.append("dependency metadata must enable only the verified X3/X4 profiles")
        if metadata.get("firmwareDistribution") != "excluded; the user selects a local file":
            errors.append("dependency metadata must state that reader firmware is excluded")

    root_worker = package / "coi-serviceworker.js"
    engine_worker = package / "engine/coi-serviceworker.js"
    if root_worker.is_file() and engine_worker.is_file():
        if hashlib.sha256(root_worker.read_bytes()).digest() != hashlib.sha256(engine_worker.read_bytes()).digest():
            errors.append("root Pages isolation worker differs from the pinned engine worker")

    page = package / "index.html"
    if page.is_file() and "./coi-serviceworker.js" not in page.read_text(encoding="utf-8"):
        errors.append("Nooir shell does not load the Pages isolation worker")

    component_notices = package / "licenses/eink-qemu-components"
    if component_notices.is_dir():
        notice_texts = [
            path for path in component_notices.rglob("*")
            if path.is_file() and path.name.lower().startswith(("copying", "license", "notice"))
        ]
        if len(notice_texts) < 15:
            errors.append(f"only {len(notice_texts)} third-party component license texts were packaged")
        if not any(path.is_file() for path in (component_notices / "desktop-npm").rglob("*")):
            errors.append("browser frontend dependency license texts are missing")

    for path in package.rglob("*"):
        if not path.is_file():
            continue
        lower = path.name.lower()
        relative = path.relative_to(package).as_posix()
        if lower.endswith(".xota"):
            errors.append(f"reader firmware update image must not be bundled: {relative}")
        if lower.endswith(".elf"):
            errors.append(f"firmware/debug ELF must not be bundled: {relative}")
        if lower.endswith(".bin") and relative not in ALLOWED_SUPPORT_BINS:
            errors.append(f"binary is not on the reviewed emulator-support allowlist: {relative}")
        if lower.endswith(".pem") or lower in {".env", "id_rsa", "id_ed25519"}:
            errors.append(f"secret/key file must not be bundled: {relative}")
        if lower.endswith((".p12", ".pfx", ".key")) or lower == ".npmrc":
            errors.append(f"credential-bearing file type must not be bundled: {relative}")

    for relative, (expected_size, expected_sha256) in SUPPORT_BIN_HASHES.items():
        path = package / relative
        if not path.is_file():
            continue
        data = path.read_bytes()
        actual_sha256 = hashlib.sha256(data).hexdigest()
        if len(data) != expected_size or actual_sha256 != expected_sha256:
            errors.append(
                f"support binary provenance hash mismatch: {relative} "
                f"(bytes={len(data)}, sha256={actual_sha256})"
            )

    rom_manifest_path = package / "licenses/esp32c3-rom/esp32c3-rom.bin.manifest.json"
    if rom_manifest_path.is_file():
        try:
            rom_manifest = json.loads(rom_manifest_path.read_text(encoding="utf-8"))
            if (
                rom_manifest.get("esp_rom_elf_release") != "20260528"
                or rom_manifest.get("esp_rom_elf_sha256") != ROM_ELF_SHA256
                or rom_manifest.get("archive_sha256") != ROM_SOURCE_ARCHIVE_SHA256
                or rom_manifest.get("output_bytes") != SUPPORT_BIN_HASHES["engine/esp32c3-rom.bin"][0]
                or rom_manifest.get("output_sha256") != SUPPORT_BIN_HASHES["engine/esp32c3-rom.bin"][1]
                or rom_manifest.get("stock_rom_bytes_used") is not False
                or rom_manifest.get("redistribution_review_complete") is not True
                or rom_manifest.get("startup_copy_table", {}).get("matched_sections") != 14
                or rom_manifest.get("startup_copy_table", {}).get("initialized_bytes") != 1316
                or rom_manifest.get("layout_pointer_section", {}).get("name") != ".rodata.interface"
            ):
                errors.append("Espressif ROM build manifest does not match the reviewed source/output identity")
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"could not parse Espressif ROM build manifest: {exc}")

    import re

    def scan_text(relative: str, content: str) -> None:
        for label, pattern in SECRET_PATTERNS:
            if re.search(pattern, content):
                errors.append(f"possible {label} in packaged text: {relative}")

    for path in package.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        if path.stat().st_size > 8 * 1024 * 1024:
            continue
        relative = path.relative_to(package).as_posix()
        scan_text(relative, path.read_text(encoding="utf-8", errors="replace"))

    source_archive = package / "sources/eink-qemu-d1b24a5f-corresponding-source.tar.gz"
    if source_archive.is_file():
        try:
            with tarfile.open(source_archive, "r:gz") as archive:
                members = archive.getmembers()
                member_names = {member.name for member in members}
                for required_source in (
                    "nooir-corresponding-source/eink-qemu/COPYING",
                    "nooir-corresponding-source/eink-qemu/configure",
                    "nooir-corresponding-source/eink-qemu/docs/devel/build-system.rst",
                    "nooir-corresponding-source/eink-qemu/eink-qemu/wasm/build.sh",
                    "nooir-corresponding-source/eink-qemu/eink-qemu/wasm/build-deps.sh",
                    "nooir-corresponding-source/eink-qemu/eink-qemu/desktop/pnpm-lock.yaml",
                    "nooir-corresponding-source/meson-subprojects",
                    "nooir-corresponding-source/meson-subprojects/berkeley-softfloat-3",
                    "nooir-corresponding-source/meson-subprojects/dtc",
                    "nooir-corresponding-source/meson-subprojects/keycodemapdb",
                ):
                    if not any(name == required_source or name.startswith(required_source + "/") for name in member_names):
                        errors.append(f"corresponding-source archive is missing: {required_source}")
                allowed_source_bins = {
                    "nooir-corresponding-source/emulator-support/esp32c3-rom.bin",
                    "nooir-corresponding-source/emulator-support/esp32c3-rom.bin.manifest.json",
                    "nooir-corresponding-source/emulator-support/bootloader-esp32c3.bin",
                    "nooir-corresponding-source/emulator-support/nvs-crosspoint.bin",
                    "nooir-corresponding-source/emulator-support/partitions.bin",
                }
                for member in members:
                    name = member.name.lower()
                    if name.endswith((".elf", ".xota", ".pem", ".p12", ".pfx", ".key")) or Path(name).name.lower() in {".env", ".npmrc", "id_rsa", "id_ed25519"}:
                        errors.append(f"non-source or secret file in corresponding-source archive: {member.name}")
                    if name.endswith(".bin") and member.name not in allowed_source_bins:
                        errors.append(f"unreviewed binary in corresponding-source archive: {member.name}")
                    if Path(member.name).name.lower() in {"firmware.bin", "boot_app0.bin", "firmware.xota"}:
                        errors.append(f"reader firmware in corresponding-source archive: {member.name}")
                    if member.isfile() and Path(name).suffix in TEXT_SUFFIXES and member.size <= 8 * 1024 * 1024:
                        stream = archive.extractfile(member)
                        if stream is not None:
                            with stream:
                                scan_text(member.name, stream.read().decode("utf-8", errors="replace"))
                manifest_member = archive.extractfile(
                    "nooir-corresponding-source/emulator-support/esp32c3-rom.bin.manifest.json"
                ) if "nooir-corresponding-source/emulator-support/esp32c3-rom.bin.manifest.json" in member_names else None
                if manifest_member is not None and rom_manifest_path.is_file():
                    with manifest_member:
                        if manifest_member.read() != rom_manifest_path.read_bytes():
                            errors.append("corresponding-source ROM manifest differs from the packaged manifest")
        except (OSError, tarfile.TarError) as exc:
            errors.append(f"could not inspect corresponding-source archive: {exc}")

    if errors:
        print("Nooir emulator package audit failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"Nooir emulator package audit passed at {package}")
    print(f"eink-qemu revision: {PINNED_ENGINE}")
    print("Reader firmware binaries: none")
    print("Hash-locked C3 ROM, bootloader, NVS and partition assets: verified")
    print("Corresponding-source, license texts, and attribution inventory: present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
