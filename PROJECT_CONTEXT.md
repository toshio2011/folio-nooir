# Folio Nooir Project Context

## Project goal

Folio Nooir is a bookshelf-focused firmware fork of CrossPoint Reader for
Xteink X3/X4 devices. The primary goals are:

- responsive Library, Recent, and Finished navigation for large libraries;
- reliable EPUB reading, image rendering, clipping, and highlighting;
- direct CBZ reading with safe image/cache handling;
- conservative memory, SD-I/O, display, and power behavior on physical X3;
- feature parity between the native X3/X4 simulators and shared reader logic;
- a responsive independent Carousel theme and Folio shelf layouts with
  prepared HQ-cover support;
- on-device Reading Statistics and Statistics Sleep without adding work to
  reader/page-turn paths;
- no regressions in XTC/XTCH, TXT, sleep, web, dictionary, or existing reader
  workflows.

Folio Nooir **1.6.1** is the current release line. The completed **1.6.0**
work is the known-good baseline for this cycle. The 1.6.1 EPUB work covers
Arabic/RTL support, text shaping, fonts, layout, malformed-EPUB recovery,
bounded typography, EOF finalization, and warm page-turn responsiveness.
CBZ/Manga preparation and cache work remain a future planned phase; its detailed
architecture/design will still be planned and audited separately before
implementation. The complete deferred plan is preserved in
`docs/CBZ_MANGA_PLAN.md`.

FreeInk is a real Nooir dependency through the `freeink-sdk` submodule. The
1.5.10 baseline uses the Nooir-specific FreeInk commit
`958720659ea289ae325e83db20049d0ea844800d` (`9587206`). Its only SDK diff is
`libs/book/FreeInkBook/third_party/tjpgd/tjpgdcnf.h`, changing
`JD_FASTDECODE` from `1` to `0` to use the portable Huffman path and avoid the
fast-path grayscale artifacts seen during Nooir validation.

## Current repository state

Authoritative parent repository:

- Branch: `codex/folio-nooir`
- 1.6.1 source checkpoint before release preparation:
  `c13eda8c490b53c0d787d641e144b4e1d332478b`
- The completed 1.6.0 source, translations, README inventory, and related
  integration work are committed and pushed on this branch.
- The 1.6.0 source was integrated by merge `0f1bd556`; `b12f2732` added the
  dedicated 1.6.0 README summary and `ccddded2` is the latest README-only
  remote update.
- The former safety checkpoint `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751` is an ancestor of this branch.
- The normal/default PlatformIO environment has compiled successfully, and
  Carousel/HQ cover plus Statistics/Sleep behavior has been physically
  exercised on X4. Simulator validation remains a separate WSL-mirror task.
- The 1.6.1 release state is being prepared from this branch. Firmware assets
  and hardware uploads remain separate release operations.

### 1.6.1 release candidate state

The release candidate retains the completed 1.6.0 baseline and includes the
reviewed Arabic/EPUB foundation and typography work. This includes Arabic
fallback, shaping/RTL integration, Quranic-mark bounds compensation,
malformed-XML recovery, generic block flow, bounded CSS typography,
EOF/page finalization, and the warm page-turn fast path. The section cache
version is `41`, with shaping and fallback contract discriminators retained.

Temporary physical missing-glyph and EPUB memory-pressure diagnostics were
used during investigation and have been removed from the release source. The
functional fallback, font grouping, and rendering fixes remain. Physical X4
validation covered the EPUB fixes and Arabic/Quran reading paths; physical X3
validation is not claimed here. Existing ignored build outputs are not release
assets unless their source and configuration are positively verified.

The detailed CBZ/Manga preparation and cache plan remains deferred in
`docs/CBZ_MANGA_PLAN.md` and is not part of this EPUB release scope.

Nested `freeink-sdk`:

- Local branch: `nooir-1.5.10-tjpgd`
- Local commit: `958720659ea289ae325e83db20049d0ea844800d`
- `origin`: `https://github.com/toshio2011/freeink-sdk.git` (user fork)
- `upstream`: `https://github.com/Free-Ink/freeink-sdk.git` (official)
- Branch `nooir-1.5.10-tjpgd` is pushed to the fork and remote verification
  resolves it to `958720659ea289ae325e83db20049d0ea844800d`.
- The parent tree points `.gitmodules` at the user fork and commits the exact
  tested `9587206` submodule pointer. Do not update it to an untested SDK SHA.

The authoritative source tree is kept separate from the simulator mirror.
Scratch paths such as `.codex-*`, `_epub-inspect*`, `codex-work-monitor/`, logs,
probes, caches, binaries, and build output must remain untracked and untouched.

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

### Retained readers, annotations, and device workflows

- EPUB remains the primary reflowable reader, with bounded image handling,
  typography/reflow caches, progress, bookmarks, clipping/highlighting, and
  KOReader-compatible progress synchronization.
- XTC/XTCH and TXT/Markdown retain their format-specific reader paths,
  progress, cover, sleep, and resume behavior.
- StarDict dictionary lookup remains available from reader settings and text
  selection, with persistent lookup history and bounded index preparation.
- Bookmark, clipping, highlight, Reading Summary, and per-book statistics
  workflows remain available from the reader and book-action surfaces.
- Wi-Fi setup, browser-based file transfer, Calibre Wireless, WebDAV, OPDS,
  OTA update support, and the web To-Do and Clock & Weather flows remain part
  of the supported device workflow.
- Themes and settings retain independent layout, typography, orientation,
  refresh, sleep, dictionary, network, and device-configuration persistence.

### Synchronization and compatibility

- KOReader Sync accepts all successful HTTP 2xx responses, including bodyless
  successful updates, while still validating required progress payloads.
- X3/X4 native simulator support, X3 geometry/profile handling, simulated X3
  tilt controls, compatibility scripts, and documentation are already merged
  in the parent history.

### Carousel, Folio, and cover caching

- The independent Carousel theme has persisted 3-Cover and 5-Cover layouts,
  circular navigation, safe 0/1/2/3/4-book handling, screen-derived geometry,
  mirrored trapezoidal side covers, opaque far-to-near draw order, and the
  existing graphical Library behavior.
- Folio Nooir Recent and Finished each have independent `3 Covers`, `4x2
  Grid`, `3-Cover Carousel`, and `5-Cover Carousel` choices. Folio Carousel
  rendering is restricted to the shelf region and does not replace the
  graphical Folio Library.
- Carousel centers and Statistics Sleep can use a valid explicitly prepared
  360px cover and immediately fall back to the existing 220px cover. Side
  covers, Folio shelves, and Statistics Books remain on 220px sources.
  Navigation and sleep never synchronously generate HQ covers.
- `Prepare Carousel Covers` is an explicit progress flow. Individual cache
  refresh can refresh an existing HQ file, while normal navigation and Library
  retrieval retain their existing behavior.
- Featured covers use the active Folio 3-Cover rendered geometry at runtime,
  preserve aspect ratio, and use the shelf-compatible 220px rendering path so
  Featured, 3 Covers, and the 4x2 grid remain visually consistent.
- Carousel performance work retains a six-entry source-handle cache, frame-
  local source protection against LRU eviction, unavailable-HQ probe caching,
  and shared perspective-rendering precomputation/fast paths without caching
  decoded pixel buffers or changing image quality.

### Reading Statistics and Sleep

- Reading Statistics is a unified activity with Overview, Calendar, Books, and
  Achievements tabs. It persists daily reading time, sessions, and pages,
  keeps up to 730 days of daily history, calculates current/longest streaks,
  and derives twenty achievements without double-counting book totals.
- Overview includes the seven-day chart, totals, averages, and book/streak
  KPIs. Calendar supports bounded month navigation and daily intensity/details.
  Books uses circular navigation and cached 220px covers only. Achievements
  preserves the two-column X3/X4-safe layout where the display allows it.
- Reading Stats Sleep and Minimal Stats Sleep use bounded cached-cover layouts
  with valid 360px-to-220px selection, no generation/preparation during sleep,
  and aspect-preserving rendering. Legacy Cover Sleep, Cover Overlay Sleep,
  and Cover Clipping Sleep retain their separate full-screen crop/stretch/fit
  behavior.

### Settings, integration, and translation

- Carousel layout, Folio Recent layout, and Folio Finished layout are separate
  persisted settings with independent defaults, filtering, migration, and
  translations.
- The unified Statistics entry is integrated into Folio while per-book
  statistics, Reading Summary, bookmarks, and clippings remain available.
- New Carousel, Folio layout, HQ preparation, Statistics, Sleep, status, and
  achievement labels use the normal translation-source/generation workflow.
- PDF and FB2 reader support are not implemented; only feasibility notes are
  present.

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
7. **Explicit release actions.** Tags, GitHub Releases, firmware assets,
   flashing, and physical-device validation require an explicit request and a
   positively identified build/configuration.
8. **1.6.1 focus and CBZ/Manga sequencing.** The existing CBZ reader is 1.6.0
   baseline functionality. The 1.6.1 EPUB quality phase covers Arabic/RTL,
   shaping, fonts, layout, malformed-EPUB recovery, typography, and
   pagination resilience. CBZ/Manga preparation and cache work remain planned
   for after the EPUB phase, followed by their separate requirements,
   architecture, and safety audit.

## Simulator workflow

The native simulator checkout is separate from the authoritative Windows
repository:

- WSL working copy: `~/side-wsl`
- Supported environments: `simulator_x4` and `simulator_x3`
- Windows is the source of truth; synchronize Windows to WSL for simulator
  validation only.
- Do not make unique feature changes in WSL or assume parent changes are
  present there automatically.
- Before syncing future simulator work, inspect its branch, HEAD, tracked
  changes, untracked files, and simulator-specific configuration. Preserve any
  genuinely unique local simulator work; never reset it blindly.
- Detailed setup, virtual SD-card usage, keyboard controls, tilt testing, and
  limitations are documented in `docs/simulator.md`.

## Key resources

- Main repository: <https://github.com/toshio2011/folio-nooir>
- Authoritative development branch: `codex/folio-nooir`
- 1.6.1 source checkpoint before release preparation:
  `c13eda8c490b53c0d787d641e144b4e1d332478b`
- Previous 1.6.0 safety checkpoint: `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`
- GitHub source merge: `0f1bd556`; README 1.6.0 summary: `b12f2732`
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

1. Continue physical X3/X4 regression testing for completed 1.6.0 paths,
   especially CBZ page turns/cache replay, EPUB images, clipping restoration,
   Statistics/Sleep, Folio shelves, Featured, and Carousel.
2. If simulator validation is needed, synchronize the authoritative Windows
   source to WSL and smoke-test both `simulator_x4` and `simulator_x3`; do not
   infer simulator readiness from the default firmware build alone.
3. To consume later FreeInk updates, `fetch upstream`, merge or rebase, test,
   push the tested result to `origin`, and deliberately update Nooir's pinned
   submodule SHA. Never blindly pull inside `freeink-sdk`.
4. After the EPUB phase, return to the preserved CBZ/Manga preparation and
   cache plan and complete its separate architecture/safety audit before
   implementation.
5. Keep future changes scoped and classify them as CBZ-only, EPUB-only,
   shared, simulator-only, or documentation-only before editing source.
6. Review and publish release artifacts only when explicitly authorized and
   only from a positively identified, appropriately built image; do not use an
   arbitrary ignored `firmware.bin`.

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
