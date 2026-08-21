# Folio Nooir Project Context

## Project goal

Folio Nooir is a bookshelf-focused firmware fork of CrossPoint Reader for
Xteink X3/X4 devices. The primary goals are:

- responsive Library, Recent, and Finished navigation for large libraries;
- reliable EPUB reading, image rendering, clipping, and highlighting;
- direct CBZ reading with safe image/cache handling;
- conservative memory, SD-I/O, display, and power behavior on physical X3;
- feature parity between the native X3/X4 simulators and shared reader logic;
- no regressions in XTC/XTCH, TXT, sleep, web, dictionary, or existing reader
  workflows.

The current development line is the **Folio Nooir 1.5.11 release candidate**.

FreeInk is a real Nooir dependency through the `freeink-sdk` submodule. The
1.5.10 baseline uses the Nooir-specific FreeInk commit
`958720659ea289ae325e83db20049d0ea844800d` (`9587206`). Its only SDK diff is
`libs/book/FreeInkBook/third_party/tjpgd/tjpgdcnf.h`, changing
`JD_FASTDECODE` from `1` to `0` to use the portable Huffman path and avoid the
fast-path grayscale artifacts seen during Nooir validation.

## Current repository state

Parent repository:

- Branch: `feat/cbz-persistent-read-ahead`
- Base commit: `114a0f7d5a891ee037cbe15d75308622d1cfcc31`
- The branch contains uncommitted 1.5.11 source, translation, web, README,
  and handoff changes; `.gitmodules` and the `freeink-sdk` pointer are staged
  separately for review.
- The default PlatformIO environment compiles successfully. Simulator runs,
  physical flashing, release tagging, and GitHub Release publication remain
  pending.
- The latest parent commits include the 1.5.10 version bump, Recent/synopsis
  correction, dark-mode/sleep ghosting work, KOReader HTTP-success handling,
  CBZ/EPUB reader work, and README updates.

The repository's GitHub default branch is currently `codex/folio-nooir`, not
`main`. Its README was updated separately with the current feature inventory
in remote commit `a6046870`; source changes remain on the development branch.

Nested `freeink-sdk`:

- Local branch: `nooir-1.5.10-tjpgd`
- Local commit: `958720659ea289ae325e83db20049d0ea844800d`
- `origin`: `https://github.com/toshio2011/freeink-sdk.git` (user fork)
- `upstream`: `https://github.com/Free-Ink/freeink-sdk.git` (official)
- Branch `nooir-1.5.10-tjpgd` is pushed to the fork and remote verification
  resolves it to `958720659ea289ae325e83db20049d0ea844800d`.
- The parent working tree now points `.gitmodules` at the fork and stages the
  exact `9587206` submodule pointer. No parent commit or push has been made.

Current parent working-tree state is intentionally not fully clean:

- `.gitmodules` and the `freeink-sdk` pointer are staged for review at the
  verified fork URL and commit `9587206`.
- Codex inspection/build scratch paths remain untracked and untouched, such as
  `.codex-*`, `_epub-inspect*`, `codex-work-monitor/`, logs, probes, and caches.
- These files must not be staged or deleted as part of normal release work.

## Completed functionality

### CBZ

- Native CBZ reader with ComicInfo.xml metadata, cover/thumbnail caching,
  metadata-first retrieval, and Library/Recent/Finished integration.
- Recent is updated when a CBZ is opened for reading, not when details or
  metadata are merely viewed.
- Fit Width, Fit Page, Landscape, Zoom, Reset View, page picker, RTL/LTR page
  order, per-book page bookmarks, and direct bookmark/page jumps.
- Bounded archive/page limits, path-budget checks, extraction limits,
  malformed-image handling, fail-soft placeholders, and transient-file cleanup.
- PixelCache replay, persistent atomic page staging, queued navigation,
  stale-candidate protection, and bounded read-ahead. X4 uses up to three
  pages; X3 uses a conservative one-page lookahead with memory/ownership
  checks.
- Fit Width, Landscape, and Zoom share the same page-cache identity. Landscape
  and Zoom use half-screen pan steps, while foreground navigation cancels or
  queues competing read-ahead work.
- Read-ahead status is shown in a reserved lower area with page/cache progress
  and a Next-ready marker. Cache misses show a lightweight progress state so
  the reader does not appear frozen.
- Reset Progress removes CBZ `progress.bin`; per-book Clear Reading Cache
  removes generated CBZ reader pages while preserving progress; global Clear
  Reading Data remains the broader shelf/progress/statistics cleanup action.
- Web transfer optionally normalizes progressive CBZ JPEGs to baseline JPEGs;
  baseline JPEGs and PNGs remain unchanged.

### EPUB and clipping

- Bounded JPEG/PNG/TJpgDec image paths with dimension/overflow guards,
  progressive/long-Huffman fallback handling, safe cache writes, and cleanup
  of incomplete PixelCache files.
- No full-resolution source framebuffer is allocated for large images.
- PixelCache replay and image failure handling are bounded and fail-soft.
- Clipping/highlight restoration preserves regular, bold, and mixed-style runs
  independently of the highlight overlay across save/load, reflow, and font
  changes.
- A shared long-operation indicator supports user-facing CBZ and EPUB loading
  without continuous animation or repeated e-ink refreshes.

### Library, Recent, and metadata

- Viewing synopsis/details, refreshing metadata, and cache retrieval do not
  create false Recent entries.
- CBZ/EPUB metadata and cover retrieval are cache-aware and metadata-first.
- Existing shelf snapshot, Recent, Finished, statistics, bookmarks, and
  clipping behavior is preserved.

### Synchronization and compatibility

- KOReader Sync accepts all successful HTTP 2xx responses, including bodyless
  successful updates, while still validating required progress payloads.
- X3/X4 native simulator support, X3 geometry/profile handling, simulated X3
  tilt controls, compatibility scripts, and documentation are already merged
  in the parent history.

### Web and power reliability

- Web To-Do saves reject overlapping requests, avoid duplicate submissions, and
  pause background refresh while the user is editing.
- Clock & Weather web actions prevent duplicate sync requests and show a
  progress state. Slow network operations feed the task watchdog; temporary
  station-Wi-Fi loss does not automatically end web mode.
- Power-lock transitions avoid redundant active requests and unnecessary
  frequency bouncing while retaining normal sleep/deep-sleep behavior.

## Important decisions and constraints

1. **X3 safety is the baseline.** Avoid full-resolution buffers, concurrent
   decoders, background threads, aggressive SD activity, persistent CPU wake
   locks, or unbounded cache growth.
2. **Serialize foreground and prefetch work.** Foreground page rendering always
   wins. A stale or cancelled prefetch must release ownership before foreground
   cache work begins and must never replace the active page cache.
3. **Keep format-specific changes isolated.** CBZ changes must not alter EPUB,
   XTC/XTCH, TXT, or clipping behavior unless a shared fix is proven safe.
4. **Do not weaken safety guards for appearance.** Preserve image bounds,
   memory limits, cache validation, and failure cleanup even when an image is
   difficult to decode.
5. **Use explicit Git staging.** Never use `git add -A` in this repository;
   exclude scratch, logs, probes, generated caches, binaries, and build output.
6. **Submodules are separate release units.** Nooir pins one exact, tested
   FreeInk commit. Do not update the parent pointer to an SDK commit that is
   not reachable from the user's fork. Keep `origin` as the fork and
   `upstream` as official; do not push to official Free-Ink. Avoid a blind
   `git pull` inside the submodule.
7. **No automatic release actions.** Tags, GitHub Releases, firmware assets,
   flashing, and physical-device validation require an explicit request.

## Simulator workflow

The native simulator checkout is separate from the Windows repository:

- WSL working copy: `~/side-wsl`
- Supported environments: `simulator_x4` and `simulator_x3`
- Do not assume parent changes are present there automatically.
- Before syncing future simulator work, inspect its branch, HEAD, tracked
  changes, untracked files, and simulator-specific configuration. Preserve any
  genuinely unique local simulator work; never reset it blindly.
- Detailed setup, virtual SD-card usage, keyboard controls, tilt testing, and
  limitations are documented in `docs/simulator.md`.

## Key resources

- Main repository: <https://github.com/toshio2011/folio-nooir>
- Main development branch: `main`
- GitHub default/documentation branch: `codex/folio-nooir`
- Native simulator checkout: `~/side-wsl` (separate from this Windows copy)
- Simulator guide: `docs/simulator.md`
- File/cache format notes: `docs/file-formats.md`
- Nested SDK: `freeink-sdk/`
- SDK fork target: <https://github.com/toshio2011/freeink-sdk>
- SDK official upstream: <https://github.com/Free-Ink/freeink-sdk>
- Upstream reader / CrossPoint reference:
  <https://github.com/crosspoint-reader/crosspoint-reader>
- CrossInk reference (reader, display, sleep, Bluetooth, and OPDS ideas):
  <https://github.com/uxjulia/CrossInk>
- CrossLink reference (Bluetooth and device workflows):
  <https://github.com/DaisonChun/crosslink>
- vCodex/Codex reference (display and firmware ideas):
  <https://github.com/marcoand75/cpr-vcodex-steroids>
- Flowe OS reference (clock/weather and utility UI ideas):
  <https://github.com/andrewjiang/flowe-OS>
- Xteink X3 upstream source/release reference:
  <https://gitee.com/daixinchun/xteink-x3/releases#release-v20260617>

Local test fixtures used during CBZ/EPUB investigation are outside the
repository and must not be copied into Git. Recheck their paths on the local
machine before using them.

## Recommended next steps

1. Review and commit the staged parent changes (`.gitmodules` and
   `freeink-sdk`), then push that parent commit when ready.
2. To consume later FreeInk updates, `fetch upstream`, merge or rebase, test,
   push the tested result to `origin`, and deliberately update Nooir's pinned
   submodule SHA. Never blindly pull inside `freeink-sdk`.
3. From the exact resulting source, build and smoke-test `simulator_x4` and
   `simulator_x3`. The default PlatformIO environment has already compiled
   successfully; do not infer simulator or hardware readiness from that alone.
4. Physically test X4 and X3, prioritizing CBZ image quality, Fit Width Next,
   prefetch reuse/cancellation, cache replay, page errors, EPUB images, and
   bold clipping restoration.
5. Review the final staged diff and scratch exclusions, then create the 1.5.11
   tag/GitHub Release and upload firmware only when explicitly authorized.

## Useful handoff checks

```text
git status --short --branch
git diff --check
git -C freeink-sdk status --short --branch
git -C freeink-sdk diff --check
git log --oneline --decorate -12
```

For any future change, first state whether it is CBZ-only, EPUB-only, shared,
simulator-only, or documentation-only. Keep the smallest safe change, verify
the affected path, and stop at the next unrelated blocker.
