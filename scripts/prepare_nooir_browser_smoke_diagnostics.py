#!/usr/bin/env python3
"""Write a small, redacted text-only report for a failed X3/X4 browser smoke."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

MAX_INPUT_BYTES = 128 * 1024
MAX_LOG_LINES = 80
MAX_LINE_CHARS = 1200
MAX_CONTEXT_BYTES = 128 * 1024
MAX_FRAME_SAMPLES = 16
OFFICIAL_V166_SHA256 = "badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108"
SAFE_BUTTONS = {"Back", "Left", "Right", "Up", "Down", "Confirm"}
SAFE_CODES = {"Enter", "Escape", "Backspace", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"}
SAFE_ACCEPTANCE = {"home", "screenshotDownload", "library", "epubOpen", "forwardPageTurn", "backwardPageTurn", "readerExit", "sdPersistenceAfterReload", "progressPersistenceAfterReload"}
SAFE_EVENTS = {"pointerdown", "pointerup", "pointercancel", "lostpointercapture", "key-click", "key-held-class", "runtime-key-message", "runtime-keydown", "runtime-keyup"}

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

def _bounded_int(value: object, maximum: int = 120_000) -> int | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if value < 0 or value > maximum:
        return None
    return int(value)

def _screen_difference(value: object) -> dict[str, object]:
    if not isinstance(value, dict):
        return {"available": False}
    available = value.get("available") is True
    fraction = value.get("differentFraction")
    if isinstance(fraction, bool) or not isinstance(fraction, (int, float)) or not 0 <= fraction <= 1:
        return {"available": available}
    return {"available": available, "differentFraction": round(float(fraction), 6)}

def _safe_qom_value(value: object) -> bool | None:
    if isinstance(value, dict) and isinstance(value.get("value"), bool):
        return value["value"]
    return None

def _safe_reader_position(value: object) -> dict[str, int | str] | None:
    if not isinstance(value, dict):
        return None
    reason, spine, page = value.get("reason"), value.get("spine"), value.get("page")
    if not isinstance(reason, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,40}", reason):
        return None
    if any(
        isinstance(item, bool) or not isinstance(item, int) or item < -1 or item > 65535
        for item in (spine, page)
    ):
        return None
    return {"reason": reason, "spine": spine, "page": page}


def _safe_display_trace(value: object) -> list[dict[str, object]]:
    if not isinstance(value, list): return []
    output: list[dict[str, object]] = []
    samples = value if len(value) <= MAX_FRAME_SAMPLES else value[: MAX_FRAME_SAMPLES // 2] + value[-MAX_FRAME_SAMPLES // 2 :]
    for sample in samples:
        if not isinstance(sample, dict): continue
        item: dict[str, object] = {"available": sample.get("available") is True}
        elapsed = _bounded_int(sample.get("elapsedMs"), 120_000)
        if elapsed is not None: item["elapsedMs"] = elapsed
        difference = _screen_difference(sample)
        if "differentFraction" in difference: item["differentFraction"] = difference["differentFraction"]
        for field in ("differentPixels", "totalPixels"):
            number = _bounded_int(sample.get(field), 5_000_000)
            if number is not None: item[field] = number
        output.append(item)
    return output


def _safe_guest_markers(serial: object) -> list[str]:
    if not isinstance(serial, str): return []
    patterns = (
        re.compile(r"\bapp=1\.6\.6\+[0-9a-f]{8}\b", re.IGNORECASE),
        re.compile(r"\brender_ready\b"), re.compile(r"\[SHELF\] stage=on_enter_view"),
        re.compile(r"\bactivity=(?:FolioLibrary|EpubReader|RecentBooks)\b"),
        re.compile(r"\bEntering activity:\s*(?:FolioLibrary|EpubReader|RecentBooks)\b"),
        re.compile(r"\bboot_load=valid count=\d+\b"),
        re.compile(r"\breader_quiescent reason=[A-Za-z0-9_-]+ spine=-?\d+ page=-?\d+\b"),
        re.compile(r"\bstage=reader_return_deferred reason=survival_backoff remaining_ms=\d+\b"),
        re.compile(r"\bstage=reader_survival_backoff_complete elapsed_ms=\d+\b"),
    )
    markers: list[str] = []
    for line in serial.splitlines()[-400:]:
        for pattern in patterns:
            match = pattern.search(line)
            if match: markers.append(match.group(0))
    return list(dict.fromkeys(markers[-32:]))


def sanitize_failure_context(path: Path, device: str) -> dict[str, object]:
    """Select bounded telemetry and allowlisted guest markers; never copy raw serial."""
    base: dict[str, object] = {"schema": 2, "device": device, "contextAvailable": False}
    if not path.is_file():
        base["reason"] = "missing"
        return base
    try:
        if path.stat().st_size > MAX_CONTEXT_BYTES:
            base["reason"] = "oversize"
            return base
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        base["reason"] = "unreadable"
        return base
    if not isinstance(raw, dict) or not isinstance(raw.get("state"), dict):
        base["reason"] = "invalid-shape"
        return base
    state = raw["state"]
    if state.get("device") != device:
        base["reason"] = "device-mismatch"
        return base
    firmware_hash = state.get("firmwareSha256")
    base["firmwareIdentity"] = {"officialV1_6_6ChecksumMatch": firmware_hash == OFFICIAL_V166_SHA256, "sha256": OFFICIAL_V166_SHA256 if firmware_hash == OFFICIAL_V166_SHA256 else None}
    acceptance = state.get("acceptance")
    if isinstance(acceptance, dict):
        base["acceptance"] = {key: acceptance[key] is True for key in sorted(SAFE_ACCEPTANCE) if key in acceptance}
    actions = state.get("actions")
    if isinstance(actions, list) and actions and isinstance(actions[-1], dict):
        action = actions[-1]
        button = action.get("key")
        last: dict[str, object] = {"button": button if button in SAFE_BUTTONS else "unknown"}
        for source, target in (("heldMs", "configuredHoldMs"), ("actualHeldMs", "browserPointerHeldMs")):
            value = _bounded_int(action.get(source), 1000)
            if value is not None: last[target] = value
        for source in ("readerPositionBefore", "readerPositionAfter"):
            position = _safe_reader_position(action.get(source))
            if position is not None: last[source] = position
        display_trace = _safe_display_trace(action.get("displayFrameTrace"))
        if display_trace: last["displayFrameTrace"] = display_trace
        qom = {}
        for source, target in (("qomStateBefore", "before"), ("qomStateWhilePressed", "whilePressed"), ("qomStateAfter", "after")):
            value = _safe_qom_value(action.get(source))
            if value is not None:
                qom[target] = value
        if qom:
            last["qomKeyState"] = qom
        events = action.get("finalInputEvents") or action.get("inputEvents") or []
        safe_events = []
        if isinstance(events, list):
            for event in events[-24:]:
                if not isinstance(event, dict) or event.get("event") not in SAFE_EVENTS:
                    continue
                safe: dict[str, object] = {"event": event["event"]}
                key = event.get("key")
                code = event.get("code")
                if isinstance(key, str) and key in SAFE_BUTTONS:
                    safe["key"] = key
                if isinstance(code, str) and code in SAFE_CODES:
                    safe["code"] = code
                for field in ("down", "held", "trusted"):
                    if isinstance(event.get(field), bool): safe[field] = event[field]
                time_ms = _bounded_int(event.get("time"), 86_400_000)
                if time_ms is not None: safe["timeMs"] = time_ms
                safe_events.append(safe)
        last["inputEvents"] = safe_events
        last["screenDifference"] = _screen_difference(action.get("finalScreenDifference") or action.get("screenDifference"))
        base["lastAction"] = last
    page_state = raw.get("pageState")
    wrapper = page_state.get("wrapper") if isinstance(page_state, dict) else None
    if isinstance(wrapper, dict):
        markers = _safe_guest_markers(page_state.get("serial"))
        base["guest"] = {"serialMarkers": markers, "serialAvailable": isinstance(page_state.get("serial"), str)}
        runtime: dict[str, object] = {}
        for field in ("engineLoaded", "crossOriginIsolated"):
            if isinstance(wrapper.get(field), bool):
                runtime[field] = wrapper[field]
        length = _bounded_int(wrapper.get("consoleLength"), 10_000_000)
        if length is not None:
            runtime["consoleLength"] = length
        if runtime:
            base["runtime"] = runtime
    base["contextAvailable"] = True
    return base

def record(device: str, exit_status: int, log_path: Path, output_dir: Path, context_path: Path | None = None) -> None:
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
        if context_path is not None:
            context = sanitize_failure_context(context_path, device)
            (output_dir / f"{device}-context.json").write_text(json.dumps(context, sort_keys=True, indent=2) + "\n", encoding="utf-8")
            context_status = "available" if context["contextAvailable"] else str(context.get("reason", "unavailable"))
            line += f"; action_context={context_status}"
    with report.open("a", encoding="utf-8") as stream:
        stream.write(line + "\n")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", choices=("x3", "x4"), required=True)
    parser.add_argument("--exit-status", type=int, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--context", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    record(args.device, args.exit_status, args.log, args.output_dir, args.context)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
