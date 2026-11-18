#!/usr/bin/env python3
"""Materialize the exact Berkeley SoftFloat source named by QEMU's wrap file."""
from __future__ import annotations

import argparse
import configparser
import shutil
import subprocess
import tempfile
from pathlib import Path


EXPECTED_URL = "https://gitlab.com/qemu-project/berkeley-softfloat-3.git"
EXPECTED_REVISION = "b64af41c3276f97f0e181920400ee056b9c88037"


def read_pinned_wrap(engine_source: Path) -> tuple[str, str]:
    wrap = engine_source / "subprojects" / "berkeley-softfloat-3.wrap"
    if not wrap.is_file():
        raise SystemExit(f"pinned SoftFloat wrap file is missing: {wrap}")
    config = configparser.ConfigParser()
    try:
        config.read_string(wrap.read_text(encoding="utf-8"))
        section = config["wrap-git"]
        url = section["url"].strip()
        revision = section["revision"].strip()
    except (KeyError, configparser.Error) as exc:
        raise SystemExit(f"could not parse the pinned SoftFloat wrap file: {exc}") from exc
    if (url, revision) != (EXPECTED_URL, EXPECTED_REVISION):
        raise SystemExit(
            "SoftFloat wrap identity differs from the reviewed upstream pin "
            f"(url={url!r}, revision={revision!r})"
        )
    return url, revision


def run_git(args: list[str], cwd: Path) -> str:
    result = subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True, timeout=300
    )
    return result.stdout.strip()


def prepare_source(engine_source: Path, output: Path) -> None:
    engine_source = engine_source.resolve(strict=True)
    url, revision = read_pinned_wrap(engine_source)
    output = output.resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing SoftFloat source: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="nooir-softfloat-") as temporary:
        checkout = Path(temporary) / "checkout"
        checkout.mkdir()
        run_git(["init", "--quiet"], checkout)
        run_git(["remote", "add", "origin", url], checkout)
        run_git(["fetch", "--quiet", "--depth=1", "origin", revision], checkout)
        run_git(["checkout", "--quiet", "--detach", "FETCH_HEAD"], checkout)
        actual = run_git(["rev-parse", "HEAD"], checkout)
        if actual != revision:
            raise SystemExit(f"SoftFloat checkout mismatch: expected {revision}, got {actual}")
        license_file = checkout / "COPYING.txt"
        if not license_file.is_file():
            raise SystemExit(f"pinned SoftFloat source has no required license text: {license_file}")
        shutil.copytree(checkout, output, ignore=shutil.ignore_patterns(".git"))
        provenance = (
            "Folio Nooir corresponding source provenance\n"
            f"Project: {url}\n"
            f"Revision: {revision}\n"
            "Pin source: eink-qemu/subprojects/berkeley-softfloat-3.wrap\n"
            f"License text: COPYING.txt (from revision {revision})\n"
        )
        (output / "NOOIR-PROVENANCE.txt").write_text(provenance, encoding="utf-8")
    print(f"Prepared Berkeley SoftFloat source at exact revision {revision}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare_source(args.engine_source, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
