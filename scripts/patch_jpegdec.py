"""
PlatformIO pre-build script: apply CrossPoint's JPEGDEC patches via `git apply`.

The upstream JPEGDEC pin still has progressive-decoder safety and scan
component-selection bugs that surface when EIGHT_BIT_GRAYSCALE decodes a
3-component progressive JPEG. The patches in `scripts/jpegdec_patches/` carry
the fixes; this script applies each one against the libdep working tree.

Each patch's idempotency is decided by git itself:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build

Patches live in `scripts/jpegdec_patches/` as one-commit-per-fix files
(see the file headers for context). Applied in lexical order.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys


PATCH_DIR = os.path.join(env["PROJECT_DIR"], "scripts", "jpegdec_patches")  # noqa: F821


def patch_jpegdec(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return
    patches = _patch_files()
    component_patch = next(
        (patch for patch in patches if os.path.basename(patch) == "0003-honor-progressive-scan-components.patch"),
        None,
    )
    table_patch = next(
        (patch for patch in patches if os.path.basename(patch) == "0004-select-progressive-chroma-tables.patch"),
        None,
    )
    for env_dir in os.listdir(libdeps_dir):
        jpeg_dir = os.path.join(libdeps_dir, env_dir, "JPEGDEC")
        if not os.path.isdir(os.path.join(jpeg_dir, ".git")):
            continue

        # 0004 edits the same small block as 0003.  A previous build can
        # therefore leave an intermediate cache with 0003 applied but 0004
        # absent.  Finish that stack before the normal lexical pass; otherwise
        # the later patch's context would be removed by reversing 0003 first.
        if (component_patch and table_patch and _has_legacy_component_guard(jpeg_dir) and
                not _has_full_component_guard(jpeg_dir)):
            _apply_one(jpeg_dir, table_patch)

        for patch in patches:
            if component_patch and patch == component_patch and _has_full_component_guard(jpeg_dir):
                print("JPEGDEC progressive component fixes already applied: 0003 + 0004")
                continue
            _apply_one(jpeg_dir, patch)


def _patch_files():
    if not os.path.isdir(PATCH_DIR):
        raise RuntimeError(
            "JPEGDEC patches missing -- aborting build (expected directory %s)"
            % PATCH_DIR
        )
    patches = sorted(
        os.path.join(PATCH_DIR, name)
        for name in os.listdir(PATCH_DIR)
        if name.endswith(".patch")
    )
    if not patches:
        raise RuntimeError(
            "JPEGDEC patches missing -- aborting build (no .patch files in %s)"
            % PATCH_DIR
        )
    return patches


def _read_jpeg_source(jpeg_dir):
    source_path = os.path.join(jpeg_dir, "src", "jpeg.inl")
    try:
        with open(source_path, "r", encoding="utf-8") as source:
            return source.read()
    except OSError:
        return ""


def _has_legacy_component_guard(jpeg_dir):
    source = _read_jpeg_source(jpeg_dir)
    return (
        "if (pJPEG->JPCI[1].component_needed)" in source
        and "if (pJPEG->JPCI[2].component_needed)" in source
        and "iErr |= JPEGDecodeMCU_P(pJPEG, MCU_SKIP, &iDCPred1);" in source
        and "iErr |= JPEGDecodeMCU_P(pJPEG, MCU_SKIP, &iDCPred2);" in source
    )


def _has_full_component_guard(jpeg_dir):
    source = _read_jpeg_source(jpeg_dir)
    return (
        "if (pJPEG->JPCI[1].component_needed)" in source
        and "if (pJPEG->JPCI[2].component_needed) {" in source
        and "pJPEG->ucACTable = cACTable2;" in source
        and "pJPEG->ucDCTable = cDCTable2;" in source
    )


def _apply_one(jpeg_dir, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply_succeeds(jpeg_dir, patch_path, reverse=True):
        return
    if not _git_apply_succeeds(jpeg_dir, patch_path, reverse=False):
        # Not applied, not appliable -- the libdep source has diverged from
        # what the patch expects. Don't write a half-patched file.
        result = subprocess.run(
            ["git", "apply", "--check", patch_path],
            cwd=jpeg_dir,
            capture_output=True,
            text=True,
        )
        sys.stderr.write(
            "ERROR: JPEGDEC patch %s does not apply cleanly:\n%s%s\n"
            % (name, result.stdout, result.stderr)
        )
        raise SystemExit(1)
    subprocess.run(["git", "apply", patch_path], cwd=jpeg_dir, check=True)
    print("Applied JPEGDEC patch: %s" % name)


def _git_apply_succeeds(jpeg_dir, patch_path, *, reverse):
    cmd = ["git", "apply", "--check"]
    if reverse:
        cmd.append("--reverse")
    cmd.append(patch_path)
    return subprocess.run(
        cmd, cwd=jpeg_dir, capture_output=True, text=True
    ).returncode == 0


patch_jpegdec(env)  # noqa: F821
