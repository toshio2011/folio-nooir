# Folio Nooir Handoff

Read [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md) for the complete project
history, decisions, and feature inventory.

## Current position

- Development line: Folio Nooir **1.5.11 release candidate**
- Parent branch: `feat/cbz-persistent-read-ahead`
- Parent base commit: `114a0f7d5a891ee037cbe15d75308622d1cfcc31`
- FreeInk is a real Nooir dependency through the `freeink-sdk` submodule.
- The 1.5.10 baseline uses the Nooir-specific FreeInk commit
  `958720659ea289ae325e83db20049d0ea844800d` (`9587206`).
- The local README now inventories the current 1.5.11 release-candidate
  features. No remote README branch was changed by this documentation pass.
- The default PlatformIO environment now builds successfully after the shared
  watchdog header fix. No simulator run, flashing, tag, or GitHub Release has
  been performed for the current development source.

## Completed work

- Direct CBZ reader with ComicInfo metadata, covers, thumbnails, bounded page
  indexing/extraction, Fit Width/Fit Page/Landscape/Zoom, page picker,
  RTL/LTR navigation, bookmarks, persistent cache replay, queued navigation,
  X4 three-page read-ahead, and conservative X3 read-ahead.
- CBZ read-ahead uses the same cache across Fit Width, Landscape, and Zoom;
  it shows reserved-area progress and a Next-ready state, yields to foreground
  navigation, and cancels/queues actions instead of silently blocking input.
- CBZ Reset Progress removes `progress.bin`; per-book Clear Reading Cache
  removes generated reader pages without removing progress; global Clear
  Reading Data retains its broader metadata/statistics cleanup behavior.
- Web transfer can optionally normalize progressive CBZ JPEGs to baseline
  JPEGs; baseline JPEGs and PNGs are not re-encoded.
- EPUB bounded JPEG/PNG handling, large/progressive-image safeguards, safe
  PixelCache behavior, and shared long-operation loading feedback.
- EPUB clipping/highlight restoration preserving regular, bold, and mixed
  styles through save/load and reflow.
- Library/Recent metadata behavior fixes, XTC exit/cover preservation, and
  KOReader Sync 2xx handling.
- Web To-Do editing avoids refresh collisions and duplicate saves; Clock &
  Weather sync guards duplicate requests, feeds the watchdog during slow
  network work, and temporary station-Wi-Fi loss no longer ends web mode.
- Power-lock transitions avoid redundant active requests while retaining the
  existing sleep and deep-sleep paths.
- Native X3/X4 simulator support and documentation are already in parent
  history.

## FreeInk dependency and publishing state

The nested `freeink-sdk` contains the only Nooir-specific SDK change used by
the 1.5.10 baseline:

```text
JD_FASTDECODE 1 -> 0
local commit: 958720659ea289ae325e83db20049d0ea844800d
local branch: nooir-1.5.10-tjpgd
```

This selects TJpgDec's portable Huffman path, which was needed to avoid the
grayscale artifacts seen with the fast path. FreeInk must therefore be
fetchable for anyone cloning Nooir; the parent must pin this exact tested
commit rather than track a moving branch.

The nested checkout is now configured conventionally:

```text
origin   https://github.com/toshio2011/freeink-sdk.git       (user fork)
upstream https://github.com/Free-Ink/freeink-sdk.git         (official)
branch   nooir-1.5.10-tjpgd
```

The fork was created under `toshio2011`, and branch `nooir-1.5.10-tjpgd` was
pushed successfully. Remote verification resolves it to
`958720659ea289ae325e83db20049d0ea844800d`. The parent working tree now
points `.gitmodules` at the fork and stages the exact `9587206` submodule
pointer; no parent commit or push has been made yet.

## Immediate next steps

1. Review the staged parent changes (`.gitmodules` and `freeink-sdk`) together
   with the unstaged source, translation, README, and handoff changes. Stage
   only the intended release files, then commit and push when ready.
2. To sync FreeInk later, fetch `upstream`, merge or rebase deliberately,
   test Nooir, push the tested branch to `origin`, and only then update the
   parent pointer to the new exact commit. Avoid a blind `git pull` inside
   `freeink-sdk`.
3. Inspect `~/side-wsl` before syncing simulator changes.
4. Build and smoke-test `simulator_x4` and `simulator_x3` from the exact
   source; the default environment compile is already verified.
5. Physically test CBZ page turns/prefetch/cache replay, EPUB images, and bold
   clipping restoration on X4 and X3.
6. After the 1.5.11 enhancement is implemented and validated, create the
   1.5.11 tag/release and upload firmware only when explicitly authorized.

## Resource map

- Repository: <https://github.com/toshio2011/folio-nooir>
- Main branch: `main`
- Default README branch: `codex/folio-nooir`
- WSL simulator copy: `~/side-wsl`
- Simulator instructions: `docs/simulator.md`
- Cache/format reference: `docs/file-formats.md`
- Nested SDK: `freeink-sdk/`
- SDK fork target: <https://github.com/toshio2011/freeink-sdk>
- SDK official upstream: <https://github.com/Free-Ink/freeink-sdk>
- CrossPoint reference: <https://github.com/crosspoint-reader/crosspoint-reader>
- CrossInk reference: <https://github.com/uxjulia/CrossInk>
- CrossLink reference: <https://github.com/DaisonChun/crosslink>
- vCodex/Codex reference: <https://github.com/marcoand75/cpr-vcodex-steroids>
- Flowe OS reference: <https://github.com/andrewjiang/flowe-OS>
- Xteink X3 source/releases:
  <https://gitee.com/daixinchun/xteink-x3/releases#release-v20260617>

Local EPUB/CBZ test files are external fixtures, not project resources; do
not stage or copy them into the repository.

## Working-tree rules

- Preserve the staged `freeink-sdk` pin at the verified fork commit until the
  parent change is reviewed and committed.
- Keep `origin` pointed at the user fork and `upstream` pointed at official
  FreeInk. Never push Nooir work to `Free-Ink/freeink-sdk`.
- Keep the parent submodule at one exact tested SDK commit; do not use
  `git pull` blindly inside the submodule.
- Leave `.codex-*`, `_epub-inspect*`, `codex-work-monitor/`, logs, probes,
  caches, binaries, and build output untouched and unstaged.
- Use explicit Git staging; never use `git add -A`, reset, checkout, or
  force-push for this handoff.
