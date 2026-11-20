# Apply the Nooir-only FreeInk SDK delta pinned to the public base revision.
# This runs as a PlatformIO pre-build script in a clean checkout.
from pathlib import Path
import hashlib
import subprocess

Import("env")

BASE_COMMIT = "958720659ea289ae325e83db20049d0ea844800d"
PATCH_SHA256 = "3d2e37ac22cf8727bd1e6aff6431617a3f60b071e972a2d01e7857ec048b191f"
PATCH_RELATIVE = Path("patches/freeink-sdk/nooir-1.6.7_20261120_04.patch")
EXPECTED_PATHS = {
    "libs/hardware/SDCardManager/include/SDCardManager.h",
    "libs/hardware/SDCardManager/src/SDCardManager.cpp",
    "libs/network/BleKeyboardHost/include/BleKeyboardHost.h",
    "libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp",
}


def git(sdk: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(sdk), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


project = Path(env.subst("$PROJECT_DIR")).resolve()
sdk = project / "freeink-sdk"
patch = project / PATCH_RELATIVE
if not (sdk / ".git").exists() and not (sdk / ".git").is_file():
    raise RuntimeError("Initialize the pinned freeink-sdk submodule before building")
if not patch.is_file():
    raise RuntimeError("Required Nooir FreeInk SDK patch is missing")
patch_hash = hashlib.sha256(patch.read_bytes()).hexdigest()
if patch_hash != PATCH_SHA256:
    raise RuntimeError("Nooir FreeInk SDK patch hash mismatch")
head = git(sdk, "rev-parse", "HEAD")
if head.returncode != 0 or head.stdout.strip() != BASE_COMMIT:
    raise RuntimeError(
        "freeink-sdk revision mismatch; expected " + BASE_COMMIT + ", got " + head.stdout.strip()
    )

# A previously patched checkout is accepted only when the exact delta is present.
reverse = git(sdk, "apply", "--reverse", "--check", str(patch))
if reverse.returncode == 0:
    changed = git(sdk, "diff", "--name-only")
    actual = set(changed.stdout.splitlines()) if changed.returncode == 0 else set()
    if actual != EXPECTED_PATHS:
        raise RuntimeError("Patched FreeInk tree contains unexpected modified files")
    print("Verified existing Nooir FreeInk SDK patch")
else:
    forward = git(sdk, "apply", "--check", str(patch))
    if forward.returncode != 0:
        raise RuntimeError("Pinned FreeInk source does not accept the reviewed Nooir patch")
    applied = git(sdk, "apply", str(patch))
    if applied.returncode != 0:
        raise RuntimeError("Could not apply the reviewed Nooir FreeInk SDK patch")
    changed = git(sdk, "diff", "--name-only")
    actual = set(changed.stdout.splitlines()) if changed.returncode == 0 else set()
    if actual != EXPECTED_PATHS:
        raise RuntimeError("Applying the SDK patch changed an unexpected file set")
    print("Applied pinned Nooir FreeInk SDK patch")