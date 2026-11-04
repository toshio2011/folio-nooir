#!/usr/bin/env python3
"""Package the Nooir shell around an exact, locally built eink-qemu web-dist."""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import PurePosixPath
from pathlib import Path

PINNED_EINK_QEMU = "d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc"
COPYING_FILES = ("COPYING", "COPYING.LIB", "LICENSE")
SUPPORT_BINARIES = {
    "boot/bootloader-esp32c3.bin": (18688, "46893df6619f0c7d7689fa309e76551e964e3d83d68c38ccb3e52ed8c3537226"),
    "boot/nvs-crosspoint.bin": (20480, "67c80ffb02df062522216e33553f0691a9ddea06be794404365a66c90bc8e94f"),
    "boot/partitions.bin": (3072, "bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce"),
}
ESP32C3_ROM_BYTES = 387784
ESP32C3_ROM_SHA256 = "3336ae473a063be8dae6790bd45fee4506f2cf273aa36f3dfd9e0495ecb665cc"
ROM_NOTICE_BLOBS = {
    "ESP32C3-ROM-ELF-README.md": ("esp-rom-elfs-README.md", "0df304ef2d7c2ba4009c3e9931d98a951321dec2"),
    "ESP32C3-ROM-APACHE-2.0.txt": ("Apache-2.0.txt", "261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64"),
    "ESP32C3-ROM-ESP-IDF-COPYRIGHT.rst": ("esp-idf-COPYRIGHT.rst", "d6f42432fec445e5f05971147a8a1de6d2cbf93e"),
    "ESP32C3-ROM-NEWLIB-COPYING.NEWLIB": ("Newlib-COPYING.NEWLIB", "d056d18f74c923772ee53fffc131cded0ef26c78"),
    "ESP32C3-ROM-BSD-3-CLAUSE.txt": ("BSD-3-Clause.txt", "086d3992cb3a8f7000127bda96d6cbe673c8e788"),
    "SPDX-CC0-1.0.txt": ("CC0-1.0.txt", "0e259d42c996742e9e3cba14c677129b2c1b6311"),
}
LICENSE_AUDIT_ROOTS = (
    ("eink-qemu-boot", "eink-qemu/boot"),
    ("eink-qemu-wasm", "eink-qemu/wasm"),
    ("linux-headers", "linux-headers"),
    ("qemu-subproject-wrappers", "subprojects"),
    ("build-subprojects", "build-wasm-32limit/subprojects"),
    ("wasm-build-dependencies/zlib", "build-wasm-deps/zlib"),
    ("wasm-build-dependencies/libffi", "build-wasm-deps/libffi"),
    ("wasm-build-dependencies/pixman", "build-wasm-deps/pixman"),
    ("wasm-build-dependencies/glib", "build-wasm-deps/glib"),
    ("wasm-build-dependencies/libgcrypt", "build-wasm-deps/libgcrypt"),
    ("wasm-build-dependencies/libgpg-error", "build-wasm-deps/libgpg-error"),
    ("wasm-build-dependencies/browser-net", "build-wasm-deps/browser-net"),
    ("wasm-build-dependencies/em-cache/ports/sdl2", "build-wasm-deps/em-cache/ports/sdl2"),
    ("desktop-npm", "eink-qemu/desktop/node_modules/.pnpm"),
)


def git_head(path: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def ignore_firmware(directory: str, names: list[str]) -> set[str]:
    ignored: set[str] = set()
    for name in names:
        if name == "nooir" and Path(directory).name == "web-dist":
            # Local preview copies sometimes place the Nooir shell beside the
            # engine output. The package root is the single canonical shell.
            ignored.add(name)
            continue
        lower = name.lower()
        if lower.endswith((".xota", ".bin", ".elf", ".pem")):
            # All binary assets are re-added below from a hash-locked allowlist.
            # In particular, do not copy the unlicensed pc-bios ROM blobs from
            # the upstream emulator distribution.
            ignored.add(name)
    return ignored


def copy_verified_binary(source: Path, destination: Path, expected_size: int, expected_sha256: str) -> None:
    if not source.is_file():
        raise SystemExit(f"required emulator support binary is missing: {source}")
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != expected_size or digest != expected_sha256:
        raise SystemExit(
            f"support binary did not match its reviewed origin: {source} "
            f"(bytes={len(data)}, sha256={digest})"
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def add_source_tree(
    archive: tarfile.TarFile,
    source: Path,
    archive_name: str,
    *,
    include_node_modules: bool = False,
) -> None:
    excluded_directories = {".git", "build-wasm-32limit", "build-wasm", "build-wasm-deps", "__pycache__"}
    if not include_node_modules:
        excluded_directories.add("node_modules")
    excluded_suffixes = {".bin", ".elf", ".xota", ".pem", ".p12", ".key", ".wasm", ".o", ".a", ".so", ".dll", ".exe"}

    def include(member: tarfile.TarInfo) -> tarfile.TarInfo | None:
        parts = PurePosixPath(member.name).parts
        if any(part in excluded_directories for part in parts):
            return None
        if not member.isdir() and PurePosixPath(member.name).suffix.lower() in excluded_suffixes:
            return None
        return member

    archive.add(source, arcname=archive_name, recursive=True, filter=include)


def create_corresponding_source_archive(source: Path, engine_dist: Path, rom: Path, output: Path) -> Path:
    build_root = source / "build-wasm-32limit"
    dependency_root = source / "build-wasm-deps"
    required_paths = (
        source / "eink-qemu" / "desktop" / "node_modules" / ".pnpm",
        build_root / "subprojects",
        dependency_root / "src",
        dependency_root / "em-cache" / "ports" / "sdl2",
    )
    missing = [str(path) for path in required_paths if not path.exists()]
    if missing:
        raise SystemExit("corresponding source inputs are missing: " + ", ".join(missing))

    source_dir = output / "sources"
    source_dir.mkdir(parents=True, exist_ok=True)
    destination = source_dir / f"eink-qemu-{PINNED_EINK_QEMU[:8]}-corresponding-source.tar.gz"
    if destination.exists():
        raise SystemExit(f"refusing to overwrite source archive: {destination}")
    temp_path = destination.with_name(destination.name + ".tmp")
    if temp_path.exists():
        raise SystemExit(f"refusing to overwrite temporary source archive: {temp_path}")

    with tempfile.NamedTemporaryFile(prefix="nooir-source-", suffix=".tar.gz", dir=source_dir, delete=False) as temp:
        temporary = Path(temp.name)
    try:
        with tarfile.open(temporary, mode="w:gz", compresslevel=6) as archive:
            readme = (
                f"Folio Nooir v1.6.6 browser emulator corresponding source\n"
                f"eink-qemu commit: {PINNED_EINK_QEMU}\n\n"
                "Contents: the pinned engine source, resolved Meson subproject source, "
                "downloaded source archives used by its pinned build scripts, the "
                "Emscripten SDL2 port source, locked frontend package sources, and the "
                "four hash-reviewed emulator support binaries. All other .bin, .elf, "
                ".xota, and private-key files are omitted. See licenses/CORRESPONDING-SOURCE.md "
                "in the adjacent web package for the full source and license map.\n"
            ).encode("utf-8")
            info = tarfile.TarInfo("nooir-corresponding-source/README.txt")
            info.size = len(readme)
            archive.addfile(info, io.BytesIO(readme))
            add_source_tree(archive, source, "nooir-corresponding-source/eink-qemu")
            add_source_tree(archive, build_root / "subprojects", "nooir-corresponding-source/meson-subprojects")
            add_source_tree(archive, dependency_root / "src", "nooir-corresponding-source/build-source-archives")
            package_cache = dependency_root / "glib" / "subprojects" / "packagecache"
            if package_cache.exists():
                add_source_tree(archive, package_cache, "nooir-corresponding-source/glib-package-cache")
            add_source_tree(archive, dependency_root / "em-cache" / "ports" / "sdl2", "nooir-corresponding-source/emscripten-sdl2")
            add_source_tree(
                archive,
                source / "eink-qemu" / "desktop" / "node_modules" / ".pnpm",
                "nooir-corresponding-source/frontend-package-sources",
                include_node_modules=True,
            )
            for relative, _metadata in SUPPORT_BINARIES.items():
                original = engine_dist / "boot" / Path(relative).name
                target_name = f"nooir-corresponding-source/emulator-support/{Path(relative).name}"
                archive.add(original, arcname=target_name, recursive=False)
            archive.add(rom, arcname="nooir-corresponding-source/emulator-support/esp32c3-rom.bin", recursive=False)
        os.replace(temporary, destination)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    return destination


def is_license_text(path: Path) -> bool:
    name = path.name.lower()
    return (
        name.startswith(("copying", "license", "notice"))
        or ".license." in name
        or name in {"gpl", "bsd-2-clause", "readme.license"}
    )


def copy_component_license_texts(source: Path, output: Path) -> int:
    destination_root = output / "licenses" / "eink-qemu-components"
    copied = 0
    missing_roots: list[str] = []
    for label, relative_root in LICENSE_AUDIT_ROOTS:
        root = source / relative_root
        if not root.is_dir():
            missing_roots.append(relative_root)
            continue
        for license_file in sorted(root.rglob("*")):
            if not license_file.is_file() or not is_license_text(license_file):
                continue
            try:
                if license_file.stat().st_size > 2 * 1024 * 1024:
                    continue
                contents = license_file.read_bytes()
            except OSError:
                continue
            if b"\0" in contents[:4096]:
                continue
            relative = license_file.relative_to(root)
            destination = destination_root / label / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(license_file, destination)
            copied += 1
    if missing_roots:
        raise SystemExit("required source/license roots are missing: " + ", ".join(missing_roots))
    if copied < 15:
        raise SystemExit(f"component license scan found only {copied} texts; refusing incomplete package")
    index = destination_root / "README.md"
    index.write_text(
        "# Bundled eink-qemu component license texts\n\n"
        "This directory preserves license and notice files found with the exact "
        "pinned engine source, its configured build-dependency sources, and "
        "the browser frontend's installed package dependencies. The source "
        "paths are grouped by origin. Extra files can cover build-only or "
        "test-only packages; this collection is not a complete file-to-binary "
        "license map. See `../EINK-QEMU-WEB-BUNDLE.md` and the exact source "
        "revision in `../eink-qemu/README.md`.\n",
        encoding="utf-8",
    )
    return copied


def copy_emscripten_license_texts(emsdk_root: Path, output: Path) -> None:
    candidates = (
        (emsdk_root / "upstream" / "emscripten" / "LICENSE", output / "licenses" / "emscripten" / "LICENSE"),
        (emsdk_root / "upstream" / "LICENSE.TXT", output / "licenses" / "llvm" / "LICENSE.TXT"),
    )
    copied = 0
    for source_file, target in candidates:
        if source_file.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, target)
            copied += 1
    if copied == 0 or not (output / "licenses" / "emscripten" / "LICENSE").is_file():
        raise SystemExit(f"Emscripten SDK license text is missing under {emsdk_root}")


def copy_esp32c3_rom_notices(repository: Path, output: Path) -> None:
    destination_root = output / "licenses" / "esp32c3-rom"
    destination_root.mkdir(parents=True, exist_ok=True)
    for name, (packaged_name, expected_blob) in ROM_NOTICE_BLOBS.items():
        source_file = repository / "LICENSES" / name
        if not source_file.is_file():
            raise SystemExit(f"required Espressif ROM notice/license is missing: {source_file}")
        actual_blob = git_blob_sha1(source_file.read_bytes())
        if actual_blob != expected_blob:
            raise SystemExit(
                f"Espressif ROM notice/license changed from pinned upstream text: {name} "
                f"(expected {expected_blob}, got {actual_blob})"
            )
        shutil.copy2(source_file, destination_root / packaged_name)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine-source", type=Path, required=True)
    parser.add_argument("--engine-dist", type=Path, required=True)
    parser.add_argument("--esp32c3-rom", type=Path, required=True)
    parser.add_argument("--emsdk-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.engine_source.resolve(strict=True)
    engine_dist = args.engine_dist.resolve(strict=True)
    esp32c3_rom = args.esp32c3_rom.resolve(strict=True)
    emsdk_root = args.emsdk_root.resolve(strict=True)
    output = args.output.resolve()
    if git_head(source) != PINNED_EINK_QEMU:
        raise SystemExit(f"eink-qemu source must be pinned at {PINNED_EINK_QEMU}")
    if not (engine_dist / "index.html").is_file() or not (engine_dist / "runtime.html").is_file():
        raise SystemExit("engine-dist must be the built eink-qemu browser web-dist directory")
    if output.exists() and any(output.iterdir()):
        raise SystemExit(f"refusing to overwrite non-empty output directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    repository = Path(__file__).resolve().parents[1]

    engine_output = output / "engine"
    shutil.copytree(engine_dist, engine_output, ignore=ignore_firmware, dirs_exist_ok=False)
    for relative, (expected_size, expected_sha256) in SUPPORT_BINARIES.items():
        copy_verified_binary(
            engine_dist / "boot" / Path(relative).name,
            engine_output / "boot" / Path(relative).name,
            expected_size,
            expected_sha256,
        )
    copy_verified_binary(esp32c3_rom, engine_output / "esp32c3-rom.bin", ESP32C3_ROM_BYTES, ESP32C3_ROM_SHA256)
    source_archive = create_corresponding_source_archive(source, engine_dist, esp32c3_rom, output)

    boot_source = source / "eink-qemu" / "boot"
    for name in ("README.md", "LICENSE", "LICENSE.partitions"):
        original = boot_source / name
        if not original.is_file():
            raise SystemExit(f"required boot asset provenance/license is missing: {original}")
        destination = output / "licenses" / "eink-qemu-boot" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, destination)
    isolation_script = engine_output / "coi-serviceworker.js"
    isolation_license = engine_output / "coi-serviceworker.LICENSE.txt"
    if not isolation_script.is_file() or not isolation_license.is_file():
        raise SystemExit("pinned engine distribution must contain its MIT isolation script and license")
    # GitHub Pages cannot set COOP/COEP response headers. Serve the pinned
    # upstream worker at the package root so its scope covers both the shell
    # and /engine/; localhost preview already gets isolation headers.
    shutil.copy2(isolation_script, output / "coi-serviceworker.js")
    for name in COPYING_FILES:
        original = source / name
        if not original.is_file():
            raise SystemExit(f"required upstream license file is missing: {original}")
        target = output / "licenses" / "eink-qemu" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
    upstream_readme = source / "README.md"
    if not upstream_readme.is_file():
        raise SystemExit(f"required upstream attribution README is missing: {upstream_readme}")
    shutil.copy2(upstream_readme, output / "licenses" / "eink-qemu" / "README.md")

    component_inventory = repository / "LICENSES" / "EINK-QEMU-WEB-BUNDLE.md"
    if not component_inventory.is_file():
        raise SystemExit(f"Nooir engine component inventory is missing: {component_inventory}")
    shutil.copy2(component_inventory, output / "licenses" / "EINK-QEMU-WEB-BUNDLE.md")
    license_count = copy_component_license_texts(source, output)
    copy_emscripten_license_texts(emsdk_root, output)
    copy_esp32c3_rom_notices(repository, output)

    noir_license = repository / "LICENSE"
    if not noir_license.is_file():
        raise SystemExit(f"Nooir license file is missing: {noir_license}")
    noir_license_target = output / "licenses" / "folio-nooir" / "LICENSE"
    noir_license_target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(noir_license, noir_license_target)

    attribution_inventory = repository / "LICENSES" / "REFERENCE-PROJECTS.md"
    if not attribution_inventory.is_file():
        raise SystemExit(f"Nooir attribution inventory is missing: {attribution_inventory}")
    shutil.copy2(attribution_inventory, output / "licenses" / "NOOIR-REFERENCE-PROJECTS.md")

    corresponding_source = repository / "LICENSES" / "CORRESPONDING-SOURCE.md"
    if not corresponding_source.is_file():
        raise SystemExit(f"corresponding-source inventory is missing: {corresponding_source}")
    shutil.copy2(corresponding_source, output / "licenses" / "CORRESPONDING-SOURCE.md")

    rom_notices = repository / "LICENSES" / "ESP32C3-ROM-NOTICES.md"
    if not rom_notices.is_file():
        raise SystemExit(f"Espressif ROM notice is missing: {rom_notices}")
    shutil.copy2(rom_notices, output / "licenses" / "ESP32C3-ROM-NOTICES.md")

    shell_source = Path(__file__).resolve().parents[1] / "web-emulator"
    for name in ("index.html", "app.js", "style.css", "README.md", "dependencies.json"):
        shutil.copy2(shell_source / name, output / name)

    notice = f"""# Folio Nooir Browser Emulator package notice

Nooir shell source: the isolated Folio Nooir v1.6.6 port at
`b6d428a3187afc25d434fcb475e9ab436099c1ba`.

The browser engine is built from
[serialx/eink-qemu](https://github.com/serialx/eink-qemu/tree/{PINNED_EINK_QEMU})
at commit `{PINNED_EINK_QEMU}`. Its GPL-2.0, LGPL-2.1, MIT, and other component
notices remain in this package. Top-level upstream texts are under
`licenses/eink-qemu/`; dependency license texts scanned from the pinned build
tree and frontend dependencies are under `licenses/eink-qemu-components/`.
The exact corresponding-source pins and build inputs are recorded in
`licenses/CORRESPONDING-SOURCE.md`.
The Nooir MIT license and upstream attribution inventory are under `licenses/`.
The pinned MIT `coi-serviceworker.js` is copied to the package root for GitHub
Pages isolation; its upstream license remains beside the engine copy.

The C3 ROM is converted from Espressif's Apache-2.0-licensed
`esp32c3_rev3_rom.elf`; its provenance, source/archive hashes, conversion
notice, and ROM third-party notices are in `licenses/ESP32C3-ROM-NOTICES.md`.
Only the reviewed C3 ROM, C3 bootloader, CrossPoint NVS seed and CrossPoint
partition table are included. Upstream QEMU ROM blobs, S3 support binaries,
Picco partitions and all reader firmware are excluded.

The adjacent `sources/` directory contains the corresponding-source archive
for the engine, linked build dependencies, frontend dependencies, and reviewed
emulator support inputs.

No Nooir firmware image is included. The page reads stable release metadata
and runs firmware in the user's browser. Custom files are not uploaded to a
server by this package.
"""
    (output / "NOOIR-WEB-EMULATOR-NOTICE.md").write_text(notice, encoding="utf-8")
    print(f"Packaged local Nooir browser shell at {output}")
    print(f"eink-qemu source: {PINNED_EINK_QEMU}")
    print(f"Third-party license/notice texts copied: {license_count}")
    print("No reader firmware binaries were copied.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
