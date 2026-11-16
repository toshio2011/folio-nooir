#!/usr/bin/env python3
"""Write a small, redacted text-only report for a failed X3/X4 browser smoke."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

MAX_INPUT_BYTES = 128 * 1024
MAX_LOG_LINES = 80
MAX_LINE_CHARS = 1200

PRIVATE_KEY_BEGIN = re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")
PRIVATE_KEY_END = re.compile(r"-----END [A-Z0-9 ]*PRIVATE KEY-----")
ERROR_LINE = re.compile(
    r"\b(?:AssertionError|[A-Za-z_$][\w.$]*(?:Error|Exception)|Error)\s*:",
    re.IGNORECASE,
)
REDACTIONS = (
    (re.compile(r"(?im)^\s*(authorization|proxy-authorization)\s*:\s*.*$"), r"\1: [redacted]"),
    (re.compile(r"(?i)\bbearer\s+[A-Za-z0-9._~+/=-]+"), "Bearer [redacted]"),
    (re.compile(r"(?i)\b(token|access[_-]?token|secret|password|credential|api[_-]?key)\s*[=:]\s*[^,\s&]+"), r"\1=[redacted]"),
    (re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})\b"), "[redacted token]"),
    (re.compile(r"(?i)\b--user-data-dir(?:=|\s+)[^\s\"'<>]+"), "--user-data-dir=[redacted browser profile]"),
    (re.compile(r"(?i)wss?://(?:127\.0\.0\.1|localhost):\d+/devtools/[^\s\"'<>]+"), "[redacted CDP URL]"),
    (re.compile(r"(?i)(?:[A-Za-z]:\\Users\\[^\"'<>]*|/(?:home|root|Users|tmp|mnt)/[^\s\"'<>]*)"), "[redacted local path]"),
    (re.compile(r"(?i)[^\s\"'<>]*\.(?:bin|rom|elf)\b"), "[redacted firmware/ROM path]"),
)

def redact_line(line: str) -> str:
    for pattern, replacement in REDACTIONS:
        line = pattern.sub(replacement, line)
    return line

def read_tail(path: Path) -> list[str]:
    if not path.is_file():
        return ["[smoke log missing]"]
    with path.open("rb") as stream:
        size = path.stat().st_size
        if size > MAX_INPUT_BYTES:
            stream.seek(-MAX_INPUT_BYTES, 2)
            stream.readline()
        data = stream.read(MAX_INPUT_BYTES)
    lines = data.decode("utf-8", errors="replace").splitlines()
    return lines[-MAX_LOG_LINES:]

def sanitize(lines: list[str]) -> list[str]:
    output: list[str] = []
    in_private_key = False
    for original in lines:
        line = original[:MAX_LINE_CHARS * 4]
        if PRIVATE_KEY_BEGIN.search(line):
            if not in_private_key:
                output.append("[redacted private-key block]")
            in_private_key = not bool(PRIVATE_KEY_END.search(line))
            continue
        if in_private_key:
            if PRIVATE_KEY_END.search(line):
                in_private_key = False
            continue
        safe = redact_line(line)
        output.append(safe[:MAX_LINE_CHARS] + (" [truncated]" if len(safe) > MAX_LINE_CHARS else ""))
    return output[-MAX_LOG_LINES:]

def record(device: str, exit_status: int, log_path: Path, output_dir: Path) -> None:
    if device not in {"x3", "x4"}:
        raise ValueError("device must be x3 or x4")
    if exit_status < 0 or exit_status > 255:
        raise ValueError("exit status must be in the range 0..255")
    output_dir.mkdir(parents=True, exist_ok=True)
    report = output_dir / "summary.txt"
    if exit_status == 0:
        line = f"{device}: PASS (exit=0)"
    else:
        lines = read_tail(log_path)
        errors = [line for line in lines if ERROR_LINE.search(line)]
        safe_error = sanitize([errors[0].strip()]) if errors else []
        first_error = safe_error[0][:280] if safe_error else "not identified in bounded log"
        line = f"{device}: FAIL (exit={exit_status}; first_error={first_error})"
        safe_lines = sanitize(lines)
        (output_dir / f"{device}.log").write_text("\n".join(safe_lines) + "\n", encoding="utf-8")
    with report.open("a", encoding="utf-8") as stream:
        stream.write(line + "\n")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", choices=("x3", "x4"), required=True)
    parser.add_argument("--exit-status", type=int, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    record(args.device, args.exit_status, args.log, args.output_dir)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
