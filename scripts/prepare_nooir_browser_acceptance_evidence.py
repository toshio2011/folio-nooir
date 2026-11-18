#!/usr/bin/env python3
"""Build a redacted X3/X4 acceptance evidence artifact outside the site package."""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import struct
import sys
import zlib
from pathlib import Path

MAX_SERIAL_LINES = 120
MAX_SERIAL_BYTES = 16_000
REQUIRED_ACCEPTANCE = (
    "home",
    "screenshotDownload",
    "library",
    "epubOpen",
    "forwardPageTurn",
    "backwardPageTurn",
    "readerExit",
    "sdPersistenceAfterReload",
    "progressPersistenceAfterReload",
)
SCREEN_FIELDS = {
    "home": "home",
    "library": "library",
    "libraryBookFolder": "library-book-folder",
    "readerFirst": "reader-first",
    "readerForward": "reader-forward",
    "readerBackward": "reader-backward",
    "readerForwardAfterBack": "reader-forward-after-back",
    "readerExit": "reader-exit",
    "recentProgressBeforeReload": "recent-progress-before-reload",
    "recentProgressAfterReload": "recent-progress-after-reload",
    "readerRestored": "reader-restored",
    "readerRestoredExit": "reader-restored-exit",
}
REQUIRED_SCREEN_FIELDS = (
    "home", "library", "readerFirst", "readerForward", "readerBackward",
    "readerForwardAfterBack", "readerExit", "recentProgressBeforeReload",
    "recentProgressAfterReload", "readerRestored", "readerRestoredExit",
)
SAFE_SERIAL_MARKERS = (
    re.compile(r"\bapp=1\.6\.6\+[0-9a-f]{8}\b", re.IGNORECASE),
    re.compile(r"\brender_ready\b"),
    re.compile(r"\[SHELF\] stage=on_enter_view"),
    re.compile(r"\bactivity=(?:FolioLibrary|EpubReader|RecentBooks)\b"),
    re.compile(r"\bboot_load=valid count=\d+\b"),
    re.compile(r"\breader_quiescent\b"),
    re.compile(r"\bFAT32\b", re.IGNORECASE),
)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def checked_png(path: Path, expected_sha256: str | None = None) -> dict[str, int | str]:
    data = path.read_bytes()
    if len(data) < 33 or not data.startswith(PNG_SIGNATURE):
        raise SystemExit(f"invalid screenshot PNG: {path.name}")
    length = struct.unpack_from(">I", data, 8)[0]
    if length != 13 or data[12:16] != b"IHDR":
        raise SystemExit(f"invalid screenshot IHDR: {path.name}")
    width, height = struct.unpack_from(">II", data, 16)
    if not width or not height or width * height > 5_000_000:
        raise SystemExit(f"unsafe screenshot dimensions: {path.name}")
    actual = hashlib.sha256(data).hexdigest()
    if expected_sha256 and actual != expected_sha256:
        raise SystemExit(f"screenshot hash differs from smoke result: {path.name}")
    return {"bytes": len(data), "width": width, "height": height, "sha256": actual}


def filtered_serial(source: Path) -> str:
    if not source.is_file():
        return "No serial log was produced.\n"
    safe_lines: list[str] = []
    for raw in source.read_text(encoding="utf-8", errors="replace").splitlines():
        markers = []
        for pattern in SAFE_SERIAL_MARKERS:
            markers.extend(match.group(0) for match in pattern.finditer(raw))
        if markers:
            safe_lines.append(" | ".join(dict.fromkeys(markers)))
    safe_lines = safe_lines[-MAX_SERIAL_LINES:]
    output = "\n".join(safe_lines) + ("\n" if safe_lines else "No allowlisted serial markers were found.\n")
    encoded = output.encode("utf-8")
    if len(encoded) > MAX_SERIAL_BYTES:
        output = encoded[-MAX_SERIAL_BYTES:].decode("utf-8", errors="ignore")
    return output


def build_acceptance_evidence(input_root: Path, output: Path) -> Path:
    if output.exists():
        raise SystemExit(f"refusing to overwrite acceptance evidence directory: {output}")
    inputs = {device: input_root / device for device in ("x3", "x4")}
    results = {}
    for device, directory in inputs.items():
        result_path = directory / "result.json"
        if not result_path.is_file():
            raise SystemExit(f"missing {device} smoke result: {result_path}")
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if result.get("device") != device or result.get("result") != "PASS":
            raise SystemExit(f"{device} is not a passing smoke result")
        acceptance = result.get("acceptance")
        if not isinstance(acceptance, dict) or any(acceptance.get(key) is not True for key in REQUIRED_ACCEPTANCE):
            raise SystemExit(f"{device} has incomplete browser acceptance gates")
        screenshots = result.get("screenshots")
        if not isinstance(screenshots, dict) or any(not isinstance(screenshots.get(key), dict) for key in REQUIRED_SCREEN_FIELDS):
            raise SystemExit(f"{device} is missing required acceptance screenshots")
        results[device] = result

    output.mkdir(parents=True)
    try:
        manifest: dict[str, object] = {"schemaVersion": 1, "result": "PASS", "devices": {}}
        for device, result in results.items():
            target = output / device
            target.mkdir()
            copied: dict[str, dict[str, int | str]] = {}
            screenshots = result.get("screenshots", {})
            for field, stem in SCREEN_FIELDS.items():
                record = screenshots.get(field) if isinstance(screenshots, dict) else None
                if not isinstance(record, dict):
                    continue
                page_name = f"{device}-{stem}.png"
                page_source = inputs[device] / page_name
                if page_source.is_file():
                    info = checked_png(page_source, record.get("pageSha256"))
                    if info["width"] != record.get("pageWidth") or info["height"] != record.get("pageHeight"):
                        raise SystemExit(f"{device} page screenshot dimensions differ from the smoke result: {page_name}")
                    shutil.copyfile(page_source, target / page_name)
                    copied[field] = info
                else:
                    raise SystemExit(f"{device} acceptance screenshot is missing: {page_name}")
                panel_name = record.get("panelFile")
                expected_panel_name = f"{device}-{stem}-panel.png"
                if panel_name == expected_panel_name:
                    panel_source = inputs[device] / panel_name
                    if panel_source.is_file():
                        info = checked_png(panel_source, record.get("panelSha256"))
                        if info["width"] != record.get("panelWidth") or info["height"] != record.get("panelHeight"):
                            raise SystemExit(f"{device} panel screenshot dimensions differ from the smoke result: {panel_name}")
                        shutil.copyfile(panel_source, target / panel_name)
                        copied[f"{field}Panel"] = info
                    else:
                        raise SystemExit(f"{device} acceptance panel screenshot is missing: {panel_name}")
                else:
                    raise SystemExit(f"{device} acceptance panel filename is unexpected: {panel_name}")

            export = result.get("screenshotExport", {})
            download = export.get("downloadedFile") if isinstance(export, dict) else None
            expected_download = f"folio-nooir-{device}-screen.png"
            if not isinstance(download, dict) or download.get("name") != expected_download:
                raise SystemExit(f"{device} screenshot download result is missing or unexpected")
            download_source = inputs[device] / expected_download
            if not download_source.is_file():
                raise SystemExit(f"{device} downloaded screenshot is missing")
            download_info = checked_png(download_source, download.get("sha256"))
            if download_info["bytes"] != download.get("bytes") or download_info["width"] != download.get("width") or download_info["height"] != download.get("height"):
                raise SystemExit(f"{device} downloaded screenshot metadata differs from the recorded result")
            shutil.copyfile(download_source, target / expected_download)
            copied["screenshotDownload"] = download_info

            safe_result = {
                "device": device,
                "result": "PASS",
                "acceptance": {key: True for key in REQUIRED_ACCEPTANCE},
                "screenshots": copied,
            }
            (target / "result.json").write_text(json.dumps(safe_result, indent=2) + "\n", encoding="utf-8")
            (target / "serial-summary.txt").write_text(filtered_serial(inputs[device] / "serial.log"), encoding="utf-8")
            manifest["devices"][device] = {
                "result": "PASS",
                "acceptance": safe_result["acceptance"],
                "evidenceFiles": sorted(path.name for path in target.iterdir()),
            }
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    except Exception:
        # Leave partial evidence for local diagnosis; never delete generated files.
        raise
    return output


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: prepare_nooir_browser_acceptance_evidence.py INPUT_EVIDENCE_DIR OUTPUT_EVIDENCE_DIR")
    build_acceptance_evidence(Path(sys.argv[1]), Path(sys.argv[2]))
    print("Prepared separate, filtered X3/X4 acceptance evidence (no firmware or SD image copied).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
