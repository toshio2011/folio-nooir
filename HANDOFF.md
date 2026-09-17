# Folio Nooir Handoff

Read [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md) for the complete project
history, decisions, and feature inventory.

## Current position

- Active development line: Folio Nooir **1.6.2**, preparing a manual release
- Latest released baseline: Folio Nooir **1.6.1**
- Authoritative branch: `codex/folio-nooir`
- 1.6.1 source checkpoint before release preparation: `c13eda8c490b53c0d787d641e144b4e1d332478b`
- Published 1.6.1 tag/release commit: `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Known-good firmware/source milestone: `85dda52a feat: stabilize Nooir
  readers, sync and Spine Shelf`.
- Remote Quran fixture commit: `e0478714 Quran Epub`; synchronization merge:
  `5efe0695`. The merge added only three repository Quran EPUB fixtures and is
  not a new firmware/source baseline. Documentation commits after the merge
  must remain distinct from the firmware milestone.
- After documentation work, the branch is expected to be ahead of
  `origin/codex/folio-nooir` only by the synchronization/documentation commits;
  no tag or GitHub release is created by this task.
- FreeInk is a real Nooir dependency through the `freeink-sdk` submodule.
- The 1.5.10 baseline uses the Nooir-specific FreeInk commit
  `958720659ea289ae325e83db20049d0ea844800d` (`9587206`).
- The completed 1.6.0 source, translations, and feature inventory are
  integrated into `codex/folio-nooir` at merge `0f1bd556`; the branch also
  contains the dedicated 1.6.0 README update at `b12f2732` and the latest
  README-only remote update at `ccddded2`.
- The previous safety checkpoint was `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`, which is an ancestor of the current development branch.
- The normal `gh_release` build, combined diagnostic build, host suite, Spine
  tests, WSL simulators, and physical X4 validation have all passed for the
  known-good milestone. No new firmware implementation is authorized by this
  documentation task.
- The existing CBZ reader and cache behavior listed below are 1.6.0 baseline
  functionality.
- The completed 1.6.1 focus was EPUB reading quality, especially Arabic/RTL
  support, text shaping, fonts, and layout. The Arabic/EPUB foundation,
  typography work, EOF finalization, glyph-bound compensation, and warm-turn
  path are included in the release candidate being frozen below.
- The detailed future CBZ/Manga plan is preserved in
  `docs/CBZ_MANGA_PLAN.md` and remains deferred. Quick Actions are not
  implemented and may be explored only in a later release. Full UI/System
  Dark Mode is not implemented and remains deferred.

## 1.6.1 released baseline

- The release retains the completed 1.6.0 baseline and the planned
  CBZ/Manga work remains future work in `docs/CBZ_MANGA_PLAN.md`.
- The 1.6.1 EPUB work includes Arabic fallback, shaping/RTL integration,
  Quranic-mark bounds compensation, malformed-XML recovery, generic block
  flow, bounded CSS typography, EOF/page finalization, and the warm page-turn
  fast path. Section cache version is `41`, with shaping/fallback contract
  discriminators retained.
- Temporary physical missing-glyph and EPUB memory-pressure diagnostics were
  used during investigation and have been removed from the release source.
  The functional fallback, font grouping, and rendering fixes remain.
- Physical X4 validation covered the EPUB fixes and Arabic/Quran reading paths.
  Physical X3 validation is not claimed here; the shared X3/X4 code paths and
  simulator validation remain useful but do not replace that hardware check.
- The current normal `gh_release` build is linked at `6,492,279 / 6,553,600`
  bytes, with `61,321` linked bytes remaining; padded `firmware.bin` is
  `6,506,128` bytes, leaving `47,472` bytes in the app slot. Static RAM is
  `53,492 / 327,680` bytes. This leaves `7,472` bytes above the preferred
  approximately 40 KB production cushion.
- The combined `gh_release_diag` profile enables
  `NOOIR_EPUB_DIAGNOSTICS=1` and `NOOIR_KOSYNC_FONT_DIAGNOSTICS=1`; its linked
  size is `6,506,717` bytes and padded image is `6,520,560` bytes, leaving
  `33,040` bytes. Normal `gh_release` does not enable these diagnostics.
- The host regression suite is `211/211` passing and the focused Spine suite is
  `13/13` passing. WSL `simulator_x4` and `simulator_x3` pass and reach
  RecentBooks. Physical X4 validation is recorded; physical X3 hardware
  validation is not claimed. Windows simulator builds are blocked before
  compilation when `sdl2-config` is unavailable.
- Existing ignored build outputs must not be treated as release assets unless
  their source/configuration is positively verified.

## Current 1.6.2 implementation state

- Dictionary lookup keeps the remembered/configured dictionary as the fast
  path and continues to the first valid prepared fallback when it misses or
  cannot be opened. A stale, renamed, deleted, truncated, or unsupported
  preferred folder therefore does not block another working dictionary.
- The first detailed open diagnostic for a failed folder is retained while
  repeated validation of the same folder during the reader/dictionary session
  is suppressed. Settings and dictionary history are not rewritten, and the
  existing 32-byte settings field / 31-byte history-name limit is unchanged.
- The definition screen discovers alternate matches only when its Dictionary
  action is opened. It keeps at most six lightweight source records and loads
  only the selected definition body; the former combined multi-definition
  buffer is no longer used. Source switching replaces the body and resets
  definition pagination.
- The result header identifies the matched headword, source, preferred/fallback
  status, page position, and source position where multiple matches exist.
- The Sources picker contains only successful matching sources, up to six, and
  discovers alternates lazily. Invalid/no-match folders are skipped. The first
  successful result remains immediate.
- The current Spine layout is available independently for Recent and Finished:
  bounded left-to-right pagination, deterministic dimensions and grayscale
  tones, restrained binding styles, UTF-8-safe title/author fallback, shared
  render/hit rectangles, shelf/support styling, and an optional decoration-only
  plant. Library and Carousel are untouched.
- Font Manager installation is physically confirmed. KOReader Sync is
  physically interoperable with actual KOReader; Filename mode requires equal
  actual filenames, Binary mode retains the KOReader partial-MD5 identity, and
  the public server default is `https://sync.koreader.rocks:443`.
- StarDict and `.qidx` formats, persistent settings, EPUB caches, fonts,
  Arabic/Quran behavior, `SECTION_FILE_VERSION = 41`, the FreeInk SDK pin,
  partitions/SPIFFS, and user SD data remain unchanged.

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
- Native X3/X4 simulator support and documentation are available through the
  shared source and `simulator_x3`/`simulator_x4` environments.
- The independent Carousel theme supports persisted 3-Cover and 5-Cover
  layouts, circular navigation, small-collection handling, screen-derived
  trapezoidal cover geometry, opaque far-to-near rendering, and the graphical
  Library path.
- Folio Nooir Recent and Finished each support independent `3 Covers`,
  `4x2 Grid`, `3-Cover Carousel`, and `5-Cover Carousel` layouts. Folio
  Carousel rendering remains inside the shelf region; the graphical Folio
  Library remains unchanged.
- Carousel centers and Statistics Sleep screens prefer an explicitly prepared
  valid `thumb_360.bmp` and immediately fall back to `thumb_220.bmp`. Side
  covers, Folio shelf cards, and Statistics Books use the existing 220px
  source. Navigation and sleep never synchronously generate HQ covers.
- Carousel source reuse keeps a six-handle cache, protects only the current
  frame's source paths from LRU eviction, caches unavailable-HQ probes during
  the activity session, and uses shared perspective-rendering fast paths.
- Reading Statistics provides Overview, Calendar, Books, and Achievements
  tabs, persistent time/session/page data, bounded 730-day history, streaks,
  averages, circular navigation, and twenty derived achievements. Reading
  Stats Sleep and Minimal Stats Sleep use bounded cached-cover layouts, while
  legacy Cover, Overlay, and Clipping sleep modes retain their full-screen
  rendering behavior.
- Settings persist the independent Carousel layout and Folio Recent/Finished
  layouts separately, with migration/default handling and translations added
  through the normal i18n source workflow.
- PDF and FB2 readers are not implemented; the repository contains feasibility
  documentation only.

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
`958720659ea289ae325e83db20049d0ea844800d`. The parent tree points
`.gitmodules` at the fork and pins the exact tested `9587206` submodule
commit. The pin is committed in the authoritative history.

## Current release-preparation notes

- Normal production profile: `gh_release`; current measurements are
  `6,492,279` linked flash, `6,506,128` padded `firmware.bin`, `47,472` app
  bytes remaining, and `53,492` static RAM. The preferred app-slot cushion is
  about 40 KB, so new firmware work must measure its flash delta.
- Diagnostic profile: `gh_release_diag` with
  `NOOIR_EPUB_DIAGNOSTICS=1` and `NOOIR_KOSYNC_FONT_DIAGNOSTICS=1`. It is for
  measurement only and is not the release image. Diagnostic memory result
  semantics remain `result=0` = policy/dependency skip, `result=-1` = actual
  allocation failure, and `result=1` = success.
- EPUB ownership/lifecycle, image/font cleanup, cumulative spine sizing,
  bounded temporary allocations, and ditherer allocation work are production
  adaptations from the known-good milestone. Do not alter pagination, cache
  format, Arabic/Quran behavior, or user SD data without separate approval.
- JPEGDEC progressive-component patching is kept in the committed patch stack;
  patches `0001` through `0004` are applied by `scripts/patch_jpegdec.py`.
- Full UI/System Dark Mode is **not implemented**. Reader Dark Mode is
  implemented. Quick Actions are **not implemented**; they remain future
  investigation only. No X4 Pro simulator exists.

## Immediate next steps

1. Review the documentation commit and create the manual GitHub release only
   after independently verifying the intended production artifact.
2. Continue physical X3 regression validation of the released shared paths
   when hardware access is available; X4 evidence does not substitute for it.
3. If simulator validation is needed, use the separate WSL mirror at
   `/home/fatiha/side-wsl` only after inspecting its branch and dirty state.
4. To sync FreeInk later, fetch `upstream`, merge or rebase deliberately,
   test Nooir, push the tested branch to `origin`, and only then update the
   parent pointer to a new exact commit. Avoid a blind `git pull` inside
   `freeink-sdk`.
5. Review `docs/FOLIO_1.6.2_BACKLOG.md` and approve one investigation at a
   time. Keep cache format, pagination, rendering quality, and Arabic/Quran
   behavior unchanged unless a later task explicitly authorizes otherwise.
6. After the current release, return to the preserved CBZ/Manga preparation and
   cache plan and complete its separate architecture/safety audit before
   implementation.
7. Review any future post-1.6.2 source change with the X3 safety, cache, storage,
   and format-isolation constraints below before implementation.
8. Create or upload release artifacts only from a positively identified,
   appropriately built image; never use an arbitrary ignored `firmware.bin`.

## Resource map

- Repository: <https://github.com/toshio2011/folio-nooir>
- Authoritative development branch: `codex/folio-nooir`
- 1.6.1 release tag commit: `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Known-good firmware/source milestone: `85dda52a`
- Remote Quran fixture commit: `e0478714`; synchronization merge:
  `5efe0695`
- 1.6.1 source checkpoint before release preparation: `c13eda8c490b53c0d787d641e144b4e1d332478b`
- Previous 1.6.0 safety checkpoint: `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`
- WSL simulator mirror: `/home/fatiha/side-wsl`
- WSL simulator branch: `safety/wsl-cbz-before-carousel-merge-20260824`
- WSL synchronization backups: `/home/fatiha/nooir-sim-sync-backup-20260917`
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

- Preserve the committed `freeink-sdk` pin at the verified fork commit.
- Keep `origin` pointed at the user fork and `upstream` pointed at official
  FreeInk. Never push Nooir work to `Free-Ink/freeink-sdk`.
- Keep the parent submodule at one exact tested SDK commit; do not use
  `git pull` blindly inside the submodule.
- Leave `.codex-*`, `_epub-inspect*`, `codex-work-monitor/`, logs, probes,
  caches, binaries, and build output untouched and unstaged.
- Preserve the released 1.6.1 Arabic/EPUB fixes and keep future diagnostics,
  probes, logs, caches, and arbitrary build outputs out of release commits.
- WSL-only Carousel work is outside the authoritative Windows milestone and
  must not be overwritten or described as merged unless explicitly integrated.
- Treat the existing 1.6.0 CBZ reader as baseline functionality. CBZ/Manga
  preparation and cache work is deferred until after the EPUB phase and its
  separate planning and architecture audit.
- Use explicit Git staging; never use `git add -A`, reset, checkout, or
  force-push for this handoff. Keep documentation updates separate from
  source changes when preparing the next checkpoint.
