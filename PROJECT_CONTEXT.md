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

The durable EPUB/CSS research record is
[`docs/EPUB_CSS_RESEARCH.md`](docs/EPUB_CSS_RESEARCH.md). It captures the
current Nooir parser/layout/cache baseline, the constrained-reader findings
from Microreader, CrossPoint, and PapyriX, known selector/inheritance and
malformed-EPUB gaps, memory/performance gates, torture fixtures, candidate
dispositions, and the phased research-to-implementation order. It is
documentation only and does not authorize firmware/source changes.

The Folio Nooir **1.6.3 development line is officially open** at checkpoint
`12e66d191fe87a0b0006305e2ea661528b4efb65`. Released 1.6.2 remains the
compatibility and measurement baseline; the first 1.6.3 task is the clean
flash/headroom audit. Opening the line does not itself change firmware
behavior, cache formats, dependencies, partitions, or generated resources.

Folio Nooir **1.6.2 is released** and is now the compatibility baseline. Tag `1.6.2` resolves to `25df494020874150a047e2a4b3be62c3e40151e8`; the known-good 1.6.2 firmware/source milestone is `85dda52a102163b40fd4a2ddfde65e6cdc23af36`. Any new source work belongs to the **1.6.3 development line**. The known-good
1.6.2 firmware/source milestone is `85dda52a`, which includes the current
dictionary, reader lifecycle, KOSync/Font Manager, and Spine work. The 1.6.1
EPUB work covers Arabic/RTL support, text shaping, fonts, layout,
malformed-EPUB recovery, bounded typography, EOF finalization, and warm
page-turn responsiveness. CBZ/Manga preparation and cache work remain future
planned phases; the complete deferred plan is preserved in
`docs/CBZ_MANGA_PLAN.md`.

## Current phase status and gate — 2026-09-24

- **Phase A — headroom/foundations:** FROZEN / COMPLETE.
- **Phase B — EPUB foundation/performance:** FROZEN / COMPLETE. Its B1/B2
  physical findings remain durable engineering context; the temporary
  profiling changes were removed and no Phase B optimization remains to be
  implemented.
- **Phase C — EPUB CSS correctness:** COMPLETE / FROZEN.
  - **C1:** deterministic source-order/per-property cascade — committed and
    physically validated on the old-model XTEINK X4.
  - **C2:** transactional CSS-cache publication — committed and physically
    validated through normal cache creation/reuse/reboot on X4; destructive
    interruption or power-loss testing is not claimed.
  - **C3:** bounded compound selectors — committed and physically validated
    on X4.
  - **C4:** per-property `!important` cascade — committed as
    `601b7005a0d8eb0f975ed0a77ee02f0fb9d4b12c` and physically validated on X4.
  - **C5:** targeted inheritance — not implemented; deferred because no
    meaningful reading failure was demonstrated.
  - **C6:** additional resilience hardening — not implemented as a separate
    Phase C slice; deferred into evidence-driven Phase D work.
- **Phase D — EPUB resilience + richer rendering:** NEXT / ACTIVE PLANNING.

Phase C is frozen at `CSS_CACHE_VERSION = 13` and
`SECTION_FILE_VERSION = 41`. It leaves a deliberately bounded CSS subset:
deterministic per-property source order, bounded specificity, repeated
selector blocks, ordinary inline styles, supported tag/class/ID compounds,
up to three required classes, class-token-order-independent matching,
per-property `!important`, importance > specificity > source order, supported
inline/important interaction, `display:none` cascade interaction, and
transactional CSS-cache publication. It is not browser-complete CSS and does
not claim descendant, child, sibling, attribute, pseudo, universal,
generic-AST, complete-inheritance, font-family, arbitrary-specificity, or
user-origin support.

Phase D must preserve bounded memory, incremental parsing, warm-cache behavior,
the section-cache lifecycle, low hot-path allocation pressure, minimal
unnecessary SD work, Arabic/Quran behavior, pagination, and the principle that
Nooir can get smarter, but not sluggish.

The integrated standalone Phase C validation EPUB passed on a real old-model
XTEINK X4. It covered 62 visual tests across C1/C2/C3/C4, hidden/display
behavior, cache reopen/reuse, and resilience. ZIP/container/XML structure was
checked; EPUBCheck was unavailable and is not claimed. No destructive power-
loss test was performed. The shared `gh_release` configuration compiles X3/X4
paths, but physical Phase C validation is X4 only.

Final C4 engineering state: 5,470,841 linked bytes, 5,484,688-byte
`firmware.bin`, 1,068,912 padded app-slot bytes remaining, and 53,448 bytes
static RAM. C4 versus C3: +3,176 linked flash, +3,168 padded bytes,
-3,168 app-slot margin, and 0 static RAM.

FreeInk is a real Nooir dependency through the `freeink-sdk` submodule. The
1.5.10 baseline uses the Nooir-specific FreeInk commit
`958720659ea289ae325e83db20049d0ea844800d` (`9587206`). Its only SDK diff is
`libs/book/FreeInkBook/third_party/tjpgd/tjpgdcnf.h`, changing
`JD_FASTDECODE` from `1` to `0` to use the portable Huffman path and avoid the
fast-path grayscale artifacts seen during Nooir validation.

## Authoritative development workspace

The workspace migration completed on 2026-09-18. The authoritative Windows checkout is now **`D:\fatiha\side`**. All normal Windows development and production `gh_release` builds must use that checkout.

The active Linux validation checkout is **`/home/fatiha/side-wsl`**, a clean native-WSL mirror of the D: source exposed to WSL at **`/mnt/d/fatiha/side`**. Simulator/build synchronization therefore flows:

`D:\fatiha\side -> /mnt/d/fatiha/side -> /home/fatiha/side-wsl -> simulator/build validation`.

The previous Windows location **`C:\Users\fatiha\Documents\side`** is intentionally retained as a pre-migration safety/reference checkout only. The previous dirty native-WSL checkout is likewise retained as **`/home/fatiha/side-wsl-pre-d-migration-20260918`**, preserving original HEAD `90c42ce35ad3cc594a8f70ebac6d7e05bc283ada`, its branch, modified state, and representative untracked font/test fixtures. Neither preserved location is an active workspace: do not develop or build there, sync over it, reset/clean it, or delete it. It may be inspected read-only for historical reconciliation.

Migration validation used branch `codex/folio-nooir` at `124581ff14db309ffca52c4d334fd08409b43d96`, with clean D: and fresh WSL states matching the remote at that checkpoint. Firmware version source was still 1.6.2, `SECTION_FILE_VERSION = 41`, and FreeInk remained exactly `958720659ea289ae325e83db20049d0ea844800d`. The fresh Windows toolchain is D-local Python 3.12.10 with PlatformIO 6.1.19 and `D:\fatiha\side\.pio-home`; the fresh WSL mirror validated with Python 3.12.3, PlatformIO 6.1.19, and SDL2 2.30.0.

Fresh migration validation produced a successful normal `gh_release` at **6,497,905 linked bytes**, **6,511,760-byte firmware.bin**, **41,840-byte final app-slot margin**, and **53,576-byte static RAM**. `simulator_x4` and `simulator_x3` both built and ran through Boot to RecentBooks without crashes/allocation failures; missing virtual-SD fonts/books/home-state messages were expected. The historical released 1.6.2 measurement remains **6,492,279 linked / 6,506,128 bin / 47,472 margin / 53,492 RAM**. Keep both records until the approximately 5.6 KB fresh-build discrepancy is explained; do not silently rewrite the historical measurement.

## Current repository state

Authoritative parent repository:

- Branch: `codex/folio-nooir`
- Published 1.6.1 release/tag commit:
  `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Known-good firmware/source milestone:
  `85dda52a feat: stabilize Nooir readers, sync and Spine Shelf`.
- Fetched remote Quran fixture commit: `e0478714 Quran Epub`; merge
  synchronization commit: `5efe0695`. The remote commit adds only three Quran
  EPUB fixtures and is not a new firmware baseline.
- Released 1.6.2 tag: `1.6.2` -> `25df494020874150a047e2a4b3be62c3e40151e8`
- 1.6.2 firmware/source milestone: `85dda52a102163b40fd4a2ddfde65e6cdc23af36`
- The completed 1.6.0 source, translations, README inventory, and related
  integration work are committed and pushed on this branch.
- The 1.6.0 source was integrated by merge `0f1bd556`; `b12f2732` added the
  dedicated 1.6.0 README summary and `ccddded2` is the latest README-only
  remote update.
- The former safety checkpoint `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751` is an ancestor of this branch.
- The normal/default PlatformIO environment has compiled successfully, and
  Carousel/HQ cover plus Statistics/Sleep behavior has been physically
  exercised on X4. The separate WSL mirror has also passed the current
  simulator smoke checks; it remains a separate validation checkout.
- The 1.6.1 release is complete. The current 1.6.2 milestone is known-good;
  documentation/release preparation and firmware publication remain separate
  operations.
- Current normal `gh_release`: linked flash `6,492,279 / 6,553,600` bytes,
  `6,506,128` padded `firmware.bin`, `47,472` app-slot bytes remaining, and
  `53,492` static RAM. This is `7,472` bytes above the preferred approximately
  40 KB release cushion.
- `gh_release_diag` enables `NOOIR_EPUB_DIAGNOSTICS=1` and
  `NOOIR_KOSYNC_FONT_DIAGNOSTICS=1`; it is diagnostic-only at `6,506,717`
  linked bytes / `6,520,560` padded bytes with `33,040` remaining. Normal
  `gh_release` does not enable those macros.
- Host suite: `211/211`; focused Spine suite: `13/13`; WSL
  `simulator_x4` and `simulator_x3`: pass and reach RecentBooks; physical X4:
  pass. Physical X3 hardware is not claimed. Windows simulator builds are
  blocked before compilation when `sdl2-config` is unavailable.

### 1.6.1 released baseline

The release retains the completed 1.6.0 baseline and includes the
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
assets unless their source and configuration are positively verified. The
separate WSL simulator mirror has unique branch/worktree state and must be
inspected before future synchronization; it must not be reset blindly.

The detailed CBZ/Manga preparation and cache plan remains deferred in
`docs/CBZ_MANGA_PLAN.md` and is not part of this EPUB release scope.

The remaining focused 1.6.2 investigation backlog is in
`docs/FOLIO_1.6.3_BACKLOG.md`. The current dictionary enhancement batch and
its successful-source behavior are documented there; no further feature
implementation is authorized from this context alone.

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

## CURRENT IMPLEMENTED FEATURES

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
  OTA update support, and the web To-Do and Clock & Weather flows remain part  of the supported device workflow.
- Themes and settings retain independent layout, typography, orientation,
  refresh, sleep, dictionary, network, and device-configuration persistence.

### Synchronization and compatibility

- KOReader Sync accepts all successful HTTP 2xx responses, including bodyless
  successful updates, while still validating required progress payloads.
- The public default server is `https://sync.koreader.rocks:443`.
- Filename matching uses the existing filename identity and requires the
  actual filenames to match across devices. Binary matching retains KOReader's
  partial-MD5 content identity and requires identical files. The earlier
  apparent Filename incompatibility was caused by different filenames, not a
  hashing defect. Nooir ↔ actual KOReader interoperability is physically
  confirmed; do not change the Filename algorithm.
- Font Manager installation/download is physically confirmed; diagnostic
  identity markers and sync payload traces remain diagnostic-only.
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
- The optional Spine layout is available independently for Recent and Finished.
  It uses fixed bounded planning, deterministic widths/heights and
  White/LightGray/DarkGray tones, restrained spine styles, UTF-8-safe
  title/author rendering, title-only fallback, filename-stem fallback only
  when metadata is absent/malformed, shelf/plank/support styling, an optional
  decoration-only plant, pagination, and shared render/hit rectangles. Valid
  Arabic metadata remains Arabic and uses the existing font/Bidi/shaping path;
  it is not converted into a filename.

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


## Post-1.6.2 upstream plan — 1.6.3 development line

Folio Nooir **1.6.2 is released**. Any new source change belongs to the **1.6.3 development line** unless explicitly scoped otherwise. The first 1.6.3 objective is to recover flash and preserve/expand heap safety before adding another large subsystem.

### Upstream sources and policy

Track CrossPoint Reader, CrossInk, InkPointX, CrossPDF (PDF architecture), and CrossLink (Bluetooth/X4 reference). Repeat an upstream-delta audit during each Nooir release cycle and classify work as **TAKE NOW / INVESTIGATE / LATER / SKIP / ALREADY COVERED**. Upstream is a source of fixes and ideas, not Nooir's target state; never wholesale-merge simply to catch up.

### 1. Flash recovery — first priority

The 1.6.2 production baseline is 6,492,279 linked flash, 6,506,128 padded firmware.bin, 47,472 app-slot bytes remaining, and 53,492 static RAM. With a preferred ~40 KB production cushion, this is too little headroom for casually adding OPDS, PDF, FB2, or Bluetooth. Before large features, generate a linker/map-level breakdown and audit compiled-in themes, bundled/fallback fonts, icons/assets, inherited unused activities, translations, web assets, duplicate theme/rendering code, dead linked functionality, LTO/garbage collection, and resources that can safely move to SD. Compare CrossPoint's SD-theme direction and CrossInk's font/build-size reductions. Keep Folio Nooir built in unless separately proven safe. Never enlarge/change the partition. Investigation target: recover meaningful headroom, ideally 100–200 KB or more, but claim only measured normal gh_release savings.

### 2. EPUB / fonts / images / memory

Read [`docs/EPUB_CSS_RESEARCH.md`](docs/EPUB_CSS_RESEARCH.md) before changing
EPUB/CSS behavior. It distinguishes existing Nooir equivalents from upstream
ideas and marks claims that still need source, fixture, or hardware
re-verification.

Re-diff Nooir against current CrossPoint and CrossInk before adding formats. Re-evaluate CrossPoint #3521 font-cache fragmentation, #3501 SD/SPI batching, #3398 / 9d2f234 packed Font Manager catalog, 3555ff5 image-fragmentation work, 06b5d5b SD-font ligature-view cleanup, and f4b4ff0 partial font-cache space-width recovery. Some older upstream ideas are already adapted in 1.6.2; never port them twice. Also compare CrossInk deferred SD-font discovery, debounced progress writes, streaming EPUB tables, framebuffer lending during indexing, cancellable pre-indexing, low-heap dictionary guards, overlay-image fixes, XTCH memory fixes, and font-cache release around overlay PNG work. Preserve cache version 41, pagination, Arabic/Quran rendering, image quality, and working KOSync unless separately tested evidence requires a change.

### 3. SD/SPI performance

Benchmark CrossPoint #3501-style SD/SPI batching because EPUB, CBZ, XTC/XTCH, covers, metadata, dictionaries, fonts, sleep images, and caches are SD-heavy. Measure before/after on X4 and treat it as an optimization candidate, not an automatic port.

### 4. XTC / XTCH

Keep Nooir's streaming/retained B/W-plane architecture. Diff current CrossPoint/CrossInk for concrete chapter-listing, token-scan, settings, memory, cache, or SD-I/O fixes. Prefer small isolated fixes and re-test XTC/XTCH after shared SD/image/font changes.

### 5. FB2 — strong candidate after flash recovery

Audit InkPointX FB2 in detail. Prefer normalizing FB2 into Nooir's existing reflow/chapter/layout machinery instead of shipping a second full reader, reusing typography, images, progress, bookmarks, statistics, dictionary/clipping, Arabic/Bidi where valid, and cache/lifecycle infrastructure. Prototype separately and measure flash/RAM before integration.

### 6. OPDS — after headroom exists

Audit CrossPoint and InkPointX OPDS for saved servers, search, pagination, authentication if present, direct download, cancellation, XML parsing, persistence, and library refresh. Prefer the smallest useful implementation. Measure linked flash and peak TLS/parser heap before deciding whether it belongs in normal firmware.

### 7. PDF — experimental branch, reflow first

PDF remains unimplemented. Compare InkPointX fixed-layout/raster/zoom with CrossPDF-style text/reflow and SD-prepared caches. For Nooir's novel use case investigate reflow/one-time SD preparation first so the existing reading experience can be reused. Keep fixed-layout raster PDF separate. Do not promise scanned/image-heavy or arbitrary PDF compatibility. Measure parser/raster flash cost, preparation peak heap/largest block, cache size, latency, and failure behavior.

### 8. Bluetooth remote input / BLE — research first, experimental implementation only

Do not treat this merely as "Bluetooth page turner support" and do not merge a full BLE stack into normal Nooir before flash/memory headroom exists. First perform a comparative BLE/HID audit across relevant XTEINK firmware implementations (including CrossLink, the Chinese X3/CrossLink lineage where source can be obtained, CrossInk, CrossPoint/current BLE work, CrumBLE/community implementations, and other working X3/X4 BLE firmware). Record the exact stack/library and build configuration used, supported HID report types, pairing/bonding, reconnect behavior, sleep/wake lifecycle, failure recovery, and how each implementation hands input to its UI.

The key research question is the **remote's actual input behavior**. Some remotes may emit simple keyboard/consumer-control clicks, while swipe-style/touch-oriented remotes may emit mouse/pointer movement, wheel data, press/release sequences, or another multi-report HID pattern. Nooir should eventually capture and decode the complete relevant HID report/sequence before deciding what it means. Do not prematurely collapse the first report into Next/Previous Page. Investigate a bounded pipeline such as: **BLE/HID transport -> raw report capture -> HID decoder -> bounded sequence/gesture recognizer -> normalized trigger -> configurable Nooir mapping -> existing Nooir action/input dispatch**. This should allow supported remote inputs to be mapped to useful actions without making the BLE layer EPUB-specific. A development/diagnostic BLE Input Inspector may log report type, usage/key/button, modifiers, movement/wheel values, press/release/repeat, and recognized sequence; it need not ship in production if its flash cost is undesirable.

Study **memory and battery architecture as seriously as compatibility**. For every firmware/stack, measure or determine linked-flash cost, static RAM, BLE initialization delta, idle-connected heap and largest free block, scan/pair/reconnect peaks, per-event allocation behavior, fragmentation over repeated connect/disconnect cycles, task/stack sizes, whether buffers/pools remain resident, coexistence with Wi-Fi, and whether BLE can be fully or partially released when disabled. Compare BLE-only host-stack choices and compile-time feature trimming rather than assuming the largest/default stack is appropriate. Investigate bounded/on-demand allocation and reusable pools only where they improve Nooir's actual measurements.

Battery/power study must include radio duty cycle, scanning policy and timeout, connection interval/latency/supervision choices where exposed, always-on vs reader-only/on-demand BLE, reconnect/backoff policy, CPU frequency/wake-lock behavior, light/deep-sleep interaction, whether a bonded remote can reconnect after device sleep/wake and remote power cycling, and the battery cost of leaving BLE enabled during a normal reading session. Avoid continuous aggressive scanning or needless CPU wakeups. Measure physical X4 battery/current behavior where practical rather than inferring efficiency from code alone.

Prototype BLE only in an isolated experimental profile/branch after Phase A establishes comfortable headroom. Compare at minimum: no-BLE baseline, BLE compiled but disabled, enabled/disconnected, scanning, connected-idle, active remote input, repeated reconnect, reader page turns, dictionary/menu use, EPUB/CBZ/XTC activity, Wi-Fi interaction, and sleep/wake. Validate button responsiveness and lost/duplicate/repeated events. X3 remains the shared resource budget; physical X3 support is not claimed without hardware evidence. The eventual goal is a small, robust **Bluetooth Remote Input** subsystem that understands the remote faithfully, maps inputs flexibly to existing Nooir actions, reconnects reliably, and has measured/acceptable flash, memory, and battery cost.

### 9. Quick Actions / UI Dark Mode

Quick Actions remain a good isolated candidate after flash/memory work: preserve the audited CrossInk model of one configurable trigger, five persisted actions, shared popup, context filtering, and existing Nooir dispatch. Full UI/System Dark Mode remains a larger separate project; Reader Dark Mode already exists.

### 10. 1.6.3 sequencing and evidence rules

Recommended order: **flash map/recovery -> EPUB/font/image/memory upstream delta -> SD/SPI benchmark -> small safe fixes -> Quick Actions if headroom allows -> FB2 prototype -> OPDS -> PDF experiment -> Bluetooth experiment**. This is planning, not authorization to implement all of it in 1.6.3. Every upstream adaptation must record source commit/PR, why Nooir needs it, whether Nooir already has an equivalent, measured flash impact, RAM/largest-block effect where relevant, regression risk, and affected X3/X4/shared tests. Never trade recovery safety, cache compatibility, Arabic/Quran behavior, or physical X4 stability merely to match upstream.

## AUDITED / PLANNED / FUTURE FEATURES

- Reader Dark Mode is implemented. Full UI/System Dark Mode is **not
  implemented** and remains deferred.
- Quick Actions are **not implemented**; they were audited/planned only and
  may be considered for a later release.
- There is no X4 Pro simulator target, and X4 Pro/S3 compatibility is not
  claimed without hardware evidence.
- PDF and FB2 readers are not implemented; only feasibility documentation is
  present.
- CBZ/Manga preparation and cache architecture remain deferred to the separate
  `docs/CBZ_MANGA_PLAN.md` audit. Existing direct CBZ reading is implemented.
- Ubuntu built-in font flash optimization, hyphenation flash reduction, further
  EPUB responsiveness/memory work, and remaining typography improvements are
  backlog investigations, not current release claims.

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
8. **1.6.3 sequencing.** The released 1.6.2 behavior is the immutable comparison baseline. Start with flash-map/recovery and measured upstream memory work; do not change Arabic/Quran behavior, rendering quality, pagination, cache format, KOSync interoperability, partition layout, or user SD data without a separately approved task.

## Simulator workflow

The native simulator checkout is separate from the authoritative Windows
repository:

- WSL working copy: `/home/fatiha/side-wsl`
- WSL branch: `safety/wsl-cbz-before-carousel-merge-20260824`
- Supported environments: `simulator_x4` and `simulator_x3`
- Windows is the source of truth; synchronize Windows to WSL for simulator
  validation only.
- Do not make unique feature changes in WSL or assume parent changes are
  present there automatically.
- Before syncing future simulator work, inspect its branch, HEAD, tracked
  changes, untracked files, and simulator-specific configuration. Preserve any
  genuinely unique local simulator work; never reset it blindly.
- Current WSL validation reaches RecentBooks on both targets. Synchronization
  backups at `/home/fatiha/nooir-sim-sync-backup-20260917` are outside the
  repository and must never be committed. Windows simulator validation is
  blocked before compilation when `sdl2-config` is unavailable.
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
- Native simulator checkout: `/home/fatiha/side-wsl` (separate from this
  Windows copy)
- Simulator guide: `docs/simulator.md`
- File/cache format notes: `docs/file-formats.md`
- Nested SDK: `freeink-sdk/`
- SDK fork target: <https://github.com/toshio2011/freeink-sdk>
- SDK official upstream: <https://github.com/Free-Ink/freeink-sdk>
- Upstream reader / CrossPoint reference:
  <https://github.com/crosspoint-reader/crosspoint-reader>
- CrossInk reference (reader, display, sleep, Bluetooth, and OPDS ideas):
  <https://github.com/uxjulia/CrossInk>
- InkPointX reference (FB2/PDF/OPDS and reader architecture):
  <https://github.com/yokki-vans/InkPointX>
- CrossPDF reference (PDF reflow/SD-preparation architecture):
  <https://github.com/davemessew/CrossPDF>
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

1. Treat tag `1.6.2` at `25df4940` as the released compatibility baseline and `85dda52a` as the known-good firmware/source milestone used for the recorded production measurements.
2. Begin Phase D with a short source re-check focused on malformed/problematic EPUBs, huge or hostile CSS/value cases, huge paragraphs, image-heavy behavior, tables/layout edges, low-memory failure behavior, and graceful degradation.
3. Use the failure ladder: full supported styling -> reduced safe styling -> default styling -> readable text. Revisit C5 inheritance only if a real fixture or book exposes a meaningful reading problem; C6-style hardening is evidence-driven.
4. Preserve the Phase A/B/C freeze boundaries, the partition, cache formats, Arabic/Quran behavior, KOSync interoperability, user SD data and the exact FreeInk pin.
5. Keep future changes scoped and measured. Every approved normal firmware change should be compared with the relevant clean baseline and report linked flash, padded `firmware.bin`, app-slot margin, static RAM, and relevant X3/X4 evidence.
6. Continue physical X3 validation when hardware is available. Preserve the separate WSL simulator workflow and do not treat simulator success as physical X3 evidence.

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

## Deferred Nooir UI refresh — design decisions captured 2026-09-19

This is a **post-Phase-A / later implementation phase**, not authorization to start the large UI rewrite now. Finish the original 1.6.3 flash/headroom, memory, regression, and physical-X4 work first. Recovered headroom is a safety margin first and a feature budget second. X3 remains the resource budget for shared UI.

### Non-negotiable preservation contract

The UI refresh is a **presentation project, not a functionality-reduction project**. Preserve all existing information, actions, button mappings, short/long presses, touch gestures, tabs, popups, paging/scrolling, conditional states, status indicators, Back behavior, selection wrapping, orientation behavior, and dynamic button hints unless a separate change is explicitly approved. Before changing or mocking any screen, inspect the actual current Nooir activity/source and make a behavior/information inventory. Existing source behavior wins over generated mockups.

Generated mockups from the 2026-09-19 exploration are visual references only. They repeatedly invented controls, labels, rows, summaries, and footer actions. **Do not implement invented mock content.** In particular, utility screens must use their real current rows/states and real mapped footer labels.

### Visual direction

Keep Nooir's existing identity: monochrome/e-ink geometry, thin rules and dividers, strong typography hierarchy, restrained spacing, simple line/rectangle icons, soft selected rows, compact status/value presentation, and existing cover cache. Prefer reusable primitives over bitmap assets. Avoid texture packs, custom bitmap icon packs, animations, extra full-screen framebuffers, or redraw-heavy effects. Use chevrons only for genuine submenu/detail/selector behavior; booleans may use compact toggle/check visuals; direct/cycled values remain right-aligned without implying a submenu.

Create/reuse a small shared Nooir presentation layer where it actually reduces duplication (header, section title, row, selected row, divider, popup, tab, progress, metadata, button hints). Every visual addition must be measured for linked flash, RAM/largest free block where relevant, redraw cost, button responsiveness, and X3/X4 parity.

### Screens already Nooir — protect their composition

- Library, Recent, and Finished remain recognizably as they are. Keep Featured Book, the middle book-display section, compact statistics, tabs/navigation/actions/info, and existing behavior.
- Existing middle layouts remain. The requested Spine addition means a **2-row Spine View** in the middle book section, not a replacement shelf/theme. The current Spine implementation must be traced/extended because the audited planner/rendering appears to provide one shelf row; do not claim two rows until implemented and tested.
- Existing Statistics information architecture remains: Overview, Calendar, Books, Achievements, with the current selection/navigation/touch/button behavior. Polish it; do not replace it with a new dashboard.
- Existing To-Do information architecture and actions remain. No dates, categories, notes, subtasks, Today/Upcoming tabs, or due dates were approved.

### Special UI treatments approved for later implementation

- **Reader Menu:** floating Nooir panel over the visible book page, retaining every current action and its conditional visibility. Same brain, new clothes. No action may disappear merely because a mock moved it.
- **Reader Settings/Text Settings:** may share the reader-overlay family; retain the real tabs (Font, Size, Layout, Style, Dict., Controls), live book preview where technically safe, and current behavior.
- **Dictionary result:** floating panel over the book where feasible. Show real dictionary/source information above the headword, real plain-text StarDict definition, source/page status, and the activity's real dynamic four-button hints. Do not invent pronunciation/audio/synonym/source tabs.
- **Book Info:** evolve Synopsis into Book Info while preserving full synopsis paging/navigation. Approved additions are cover/title/author, useful real metadata, real progress/statistics where available, and editable star rating. Prefer Nooir-owned rating metadata rather than modifying the EPUB.
- **Statistics:** retain the exact existing four-tab structure and data; apply Nooir polish only.
- **To-Do:** retain the exact current task model and eight-option action popup; apply Nooir polish only.
- **Main Settings:** retain exactly the four persistent categories Display / Reader / Controls / System and their real conditional rows/actions. Preserve tab-level/list-level Back and Confirm semantics. Footer labels are dynamic and must come from the existing mapped-input behavior, not a hard-coded mock.
- **Reading Stats Sleep / Minimal Stats Sleep and To-Do sleep presentation:** include existing sleep-screen variants in the visual refresh, but inspect the exact current renderer first. Preserve existing content/functionality. Custom sleep images remain custom.

### Shared utility treatment

Wi-Fi/network, KOReader Sync, OPDS, OTA/update, Font Manager, keyboard, shared popups/dialogs, file browser, and other utilities do **not** need bespoke redesigns. Give them the shared Nooir visual language while retaining each activity's existing state machine, content, actions, and mapped button hints.

Source-audited examples that must be preserved:

- **Wi-Fi:** scanning/auto-connect, saved-network ordering, network list, hidden network entry, password keyboard, connecting, save-password prompt, forget-network prompt, failure handling, retry/rescan, signal/encryption/saved indicators. Network-list mapped hints are Back / Connect / conditional Forget / Retry; other states use their own existing mappings.
- **KOReader Sync settings:** exactly Username, Password, Sync Server URL, Sync Device Name, Document Matching, Send Metadata, Sync Behavior, Sign Up, Authenticate. Preserve the real right-side values/status and Back / Select / Up / Down mapping. Do not add Enable Sync, Sync Now, Last Sync, or Sync Help from mockups.
- **OPDS server settings/list:** preserve saved server rows plus Add Server, Download Folder, Filename Format; editor fields are Name, URL, Username, Password, with Delete for an existing server. Preserve browser/search/download/error/loading flows. Do not add invented catalog-management menu rows from mockups.
- **OTA:** preserve the existing check / confirmation / Cancel-or-Update / progress / no-update / failure / completion / restart flow.
- **Font Manager:** preserve the existing online family manifest/list, Download All/Update All when applicable, per-family download/update, progress/error/completion, Wi-Fi handoff, and memory safeguards. Do not add an invented top-level font menu.

### Resource and implementation gates

Do not start the large UI implementation until Phase A proves comfortable production headroom. First recover/measure flash, then prototype one representative shared-style screen (Reader Menu is a good high-value candidate), compile A/B, and benchmark physical X4 plus X3/X4 simulators. Overlay designs must not assume an extra full-screen framebuffer; reuse the existing rendered page or another bounded strategy only after measurement. Preserve fast paths such as DictionaryWordSelect's lightweight highlight snapshot/FAST_REFRESH and OTA/network render throttling.

The goal is: **make Nooir look much more like Nooir while keeping it lightweight enough for X3 and at least as responsive as it is now.**

## BLE research dossier

The detailed Bluetooth Remote Input research, HID-capture architecture, firmware comparison notes, memory/power test matrices, evidence gaps, and future implementation sequence are preserved in [`docs/BLE_REMOTE_RESEARCH.md`](docs/BLE_REMOTE_RESEARCH.md). Treat that dossier as research input only; re-audit upstream refs before implementation because BLE stacks and firmware branches may change.

The BLE research is intentionally ecosystem-wide rather than limited to obvious page-turner forks. See [`docs/BLE_ECOSYSTEM_SURVEY.md`](docs/BLE_ECOSYSTEM_SURVEY.md) for the 47-entry catalog screen, additional BLE lineages, transferable memory/power/input patterns, and the expanded future audit queue.
