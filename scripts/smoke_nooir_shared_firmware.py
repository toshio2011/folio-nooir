#!/usr/bin/env python3
"""Boot the unchanged official v1.6.6 image in the C3 X3 and X4 profiles."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import tempfile
import time

FIRMWARE_SHA256 = "badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108"
MILESTONES = {
    "x3": re.compile(r"Wait complete:\s+(?:8279|X3)_DRF"),
    "x4": re.compile(r"Wait complete:\s+(?:8179|8279x4)_DRF"),
}
GEOMETRY = {"x3": (528, 792), "x4": (480, 800)}


def validate_firmware(path: Path) -> None:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != FIRMWARE_SHA256:
        raise SystemExit(
            "smoke firmware is not the unchanged official v1.6.6 shared image: "
            f"expected {FIRMWARE_SHA256}, got {digest}"
        )


def parse_ppm(path: Path, expected: tuple[int, int]) -> None:
    data = path.read_bytes()
    header = data.split(b"\n", 3)
    if len(header) != 4 or header[0] != b"P6":
        raise RuntimeError("QEMU screen capture is not a binary PPM")
    width, height = map(int, header[1].split())
    pixels = header[3]
    if (width, height) != expected or len(pixels) != width * height * 3:
        raise RuntimeError(f"unexpected panel capture {width}x{height}")
    if len(set(pixels)) < 2:
        raise RuntimeError("panel capture is blank")


def smoke_failure(machine: str, reason: str, log_path: Path) -> RuntimeError:
    try:
        data = log_path.read_bytes()
    except OSError:
        data = b""
    excerpt = data[-16_384:].decode("utf-8", errors="replace").strip()
    if excerpt:
        return RuntimeError(
            f"{machine}: {reason}\n"
            "Last emulator output (the complete temporary log is removed after the run):\n"
            f"{excerpt}"
        )
    return RuntimeError(f"{machine}: {reason}; no emulator output was captured")


def boot_one(engine: Path, firmware: Path, machine: str, timeout: float) -> None:
    with tempfile.TemporaryDirectory(prefix=f"nooir-{machine}-smoke-") as temp_name:
        work = Path(temp_name).resolve()
        flash = work / "flash.img"
        sd = work / "sd.img"
        shot = work / f"{machine}.ppm"
        log_path = work / f"{machine}.log"
        subprocess.run(
            [sys.executable, str(engine / "eink-qemu" / "mkflash.py"),
             "--machine", machine, str(firmware), str(flash)],
            check=True,
        )
        subprocess.run(
            [sys.executable, str(engine / "eink-qemu" / "mksd.py"),
             str(sd), "64", "--cluster-size", "512"],
            check=True,
        )
        env = {
            **os.environ,
            "X4MACHINE": machine,
            "WASM64_MODE": "32",
            "RUN_NODE_SLEEP": "off",
            "TURBO": "1",
            "WASM_JIT_STATS": "1",
        }
        process = subprocess.Popen(
            ["bash", str(engine / "eink-qemu" / "wasm" / "run-node.sh"), str(flash), str(sd)],
            cwd=engine,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
        )
        selector = selectors.DefaultSelector()
        assert process.stdout is not None and process.stdin is not None
        selector.register(process.stdout, selectors.EVENT_READ)
        output = bytearray()
        milestones = 0
        screenshot_requested = False
        start = time.monotonic()
        try:
            with log_path.open("wb") as log:
                while time.monotonic() - start < timeout:
                    for key, _ in selector.select(0.1):
                        chunk = os.read(key.fd, 65536)
                        if not chunk:
                            continue
                        log.write(chunk)
                        log.flush()
                        output.extend(chunk)
                        if len(output) > 2 * 1024 * 1024:
                            del output[:1024 * 1024]
                    text = output.decode(errors="replace")
                    if any(marker in text for marker in ("Guru Meditation", "RuntimeError:", "Aborted(")):
                        raise smoke_failure(machine, "guest/runtime error", log_path)
                    current = len(MILESTONES[machine].findall(text))
                    milestones = max(milestones, current)
                    if milestones >= 3 and "[MEM]" in text and not screenshot_requested:
                        monitor_path = "/host" + shot.as_posix()
                        process.stdin.write(f'\x01cscreendump "{monitor_path}" panel\n'.encode())
                        process.stdin.flush()
                        screenshot_requested = True
                    if shot.exists() and shot.stat().st_size > 1000:
                        try:
                            parse_ppm(shot, GEOMETRY[machine])
                        except RuntimeError as exc:
                            raise smoke_failure(machine, str(exc), log_path) from exc
                        if process.poll() is not None:
                            raise smoke_failure(machine, "emulator exited before the capture completed", log_path)
                        print(f"{machine}: PASS; Home refreshes={milestones}; display={GEOMETRY[machine][0]}x{GEOMETRY[machine][1]}")
                        return
                    if process.poll() is not None:
                        raise smoke_failure(machine, f"emulator exited {process.returncode}", log_path)
            raise smoke_failure(machine, "timed out before a nonblank Home capture", log_path)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            selector.close()
            process.stdin.close()
            process.stdout.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine-source", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=240)
    args = parser.parse_args()
    engine = args.engine_source.resolve(strict=True)
    firmware = args.firmware.resolve(strict=True)
    validate_firmware(firmware)
    for machine in ("x3", "x4"):
        boot_one(engine, firmware, machine, args.timeout)
    print(f"PASS: unchanged v1.6.6 shared firmware SHA-256 {FIRMWARE_SHA256} on X3 and X4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
