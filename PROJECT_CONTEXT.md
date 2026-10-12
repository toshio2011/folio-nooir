# Folio Nooir Project Context

## Combined X4 stabilization pass — 2026-10-09

The current pass keeps reclamation transition-scoped. ReadingStats entry
releases only RecentBooks-owned rebuildable cover-source state, popup state,
and the local `recentBooks` copy (including its per-book strings); the
persistent `RecentBooksStore` is retained. The local copy is restored once
after the stats child is destroyed, before the resumed shelf render. No normal
scroll, browse, cached page turn, or steady-state font path performs cleanup.

Settings BLE now has an explicit `TransitionSensitive -> ReturnPending ->
StableIdle` state and epoch. The old `hasPendingRenderWork()` predicate made a
BLE connected notification's own Settings redraw look permanently
resource-sensitive, causing connect/stop oscillation. Successful admission
consumes the transition; only a new explicit child/heavy transition can arm a
new stop. BLE-OFF remains inert.

Web/Transfer remains an exclusive-memory session: BLE admission is suppressed
for the complete session, rebuildable runtime caches are released once on
entry, and HTTP/Wi-Fi/server teardown completes before UI reconstruction and
normal BLE admission on exit. `/api/settings` uses a two-pass fixed-buffer
stream with a measured Web-only guard; schema and image/section semantics are
unchanged. The pass adds no timers, polling retries, per-frame cleanup, or
steady-state cache churn.

## X4 COM audit and bounded memory repair — 2026-10-09

The newest physical log confirms three independent return failures. The Reader
shared-cache owner is the global `SdCardFontManager` loaded-font set: its
`SdCardFont::releaseReaderCaches()` frees rebuildable advance, kern/ligature,
mini-glyph, and overflow state while retaining font registration/coverage. The
existing Reader-exit order also clears the built-in decompressor before this
shared release, proving the topology
repair: the physical case moves from about `98.7/22.5 KB` after renderer
shared release to `108.8/49.1 KB` after Reader shared-cache release. A new
one-shot deep tier may call that same audited release only after normal Reader
return cleanup fails, the Reader is quiescent, return-pending is armed, and a
cold/incomplete section or image/grayscale-scratch miss marked heavy work.
Fully cached normal page turns never arm it.

The repeated image-miss stop/start sequence is retained. The log shows the
connected heap shape returning to the same roughly `30.2/27.6 KB` state after
each NimBLE stop/start, without monotonic NimBLE-only erosion. There is no
safe event-driven coalescing point that avoids image semantics, so no change
is made to that behavior.

Stable Settings return now has one narrow, one-shot candidate: the global
built-in `FontDecompressor` page/hot buffers. It runs only after a
BluetoothSettings return has been armed and the Settings frame is stable; it
does not clear SD-font ownership or UI lists. The observed `99.6/34.8 KB`
case remains under the unchanged `98,304/36,852` admission floors until this
reclaim is measured on hardware. Wi-Fi Settings remains a separate low-memory
case and stays deferred; no stale Wi-Fi child owner was proven.

`ReadingStatsActivity::StatisticsSnapshot` owns the day/book/achievement
vectors and the per-book strings. The explicit Stats exit now releases that
snapshot before child destruction, then releases only the rebuildable built-in
font decompressor buffers, with diagnostics before/after each stage and at
child destruction/RecentBooks resume. The retained RecentBooks local
book/string copy is released only for the Stats child boundary and restored
once before the parent shelf render; persistent store data is not reloaded from
book metadata files.

Bookmark capacity reclaim, Reader-exit shared-cache release, the renderer
scratch decision, ImageBlock/PXC, Section semantics, Web settings, XTC,
BLE-OFF behavior, and the `98,304` free / `36,852` largest-block floors remain
protected. This pass is not flashed, committed, or pushed.

The final offline `gh_release` build completed successfully after the source
review: PlatformIO reports 61,396 bytes static RAM and 5,804,659 bytes linked
flash. The physical X4 validation checklist remains outstanding.

## Focused BLE stabilization pass — 2026-10-09

The Settings BLE stop was caused by `SettingsActivity::bluetoothResourceSensitive()`
being unconditionally true. Returning from `BluetoothSettingsActivity` therefore
made the parent Settings screen a resource-sensitive activity, so the centralized
lifecycle stopped NimBLE and had no stable parent-render event that could admit it
again. The pass now arms a one-shot return on the child result, keeps BLE blocked
through the Settings render, and admits it on the next event-driven lifecycle pass
only after the render work is complete and the existing 98,304-free / 36,852-largest
floors pass. BLE-off remains inert and `ensureStarted()` remains idempotent.

The repeated `reader_caps_a ... bm=0/16 bytes=960` item is
`EpubReaderActivity::cachedBookmarks`, a `std::vector<BookmarkEntry>` loaded by
`loadCachedBookmarks()`. `BookmarkEntry` contains the two bookmark strings,
percentage, and three cached position fields; the load path clears the vector and
reserves 16 entries even when the bookmark file has no rows. Its uses are the
quiescent diagnostics, menu visibility, load, add/remove/save, and current-page
flag paths. The BLE-return cleanup now swaps only an empty vector at the existing
one-shot quiescent boundary, after font/UI/CSS cleanup, and rechecks admission once.
Live bookmarks are never released; the vector rebuilds lazily on the next load or
bookmark action. The physical expectation is up to 960 bytes reclaimed.

The RecentBooks-to-Reader trace is now logged at request, before and after outgoing
RecentBooks destruction, before and after Reader shell entry, before and after
EpubReader entry, and after the first render boundary. The outgoing activity already
dies before EPUB/Section construction; the precise safe ownership fix releases only
the rebuildable `recentBooks` vector during `RecentBooks::onExit()` before thumbnail
source invalidation. No global shelf-cache purge was added. The fragmented case
remains safely deferred unless physical logs show a largest-block improvement.

ImageBlock/PXC behavior, Section build/suspend/cache semantics, Web settings, the
40-row scratch, Reader exit cleanup, XTC activity-specific safety, and BLE floors
remain unchanged.

## Uncached section post-persist release — 2026-10-08

After uncached section persistence, release builder-only state before BLE
return admission; cached page behavior must remain unchanged. The audited
section-local `BuildContext` owns the live HTML parser/Expat state, parser
scratch, page LUT, build path strings, and temporary HTML ownership. These are
destroyed after the committed `section.bin` swap; serialized pages retain the
resolved text/image layout needed by the current Section. The shared CSS
parser's rules are cleared at the same boundary, and its empty hash-bucket
capacity is now released only after the build context is gone. No live CSS
rules, current page, current Section, image state, font cache, or 40-row
scratch is released here. `section_post_persist_before_release` and
`section_post_persist_after_release` report the build-context and CSS-capacity
heap deltas. BLE floors remain 98,304 free / 36,852 largest; return-pending is
still one-shot and event-driven.

## BLE return polish — 2026-10-08

The restored 1.6.3 ImageBlock/PXC source-extraction, decode, draw, and cache
pipeline is protected and remains outside this BLE-only pass. BLE may stop
temporarily for heavy Reader work, but an enabled session that was running and
connected is now explicitly marked for automatic return after Reader becomes
quiescent.

The return policy is one-shot and event-driven per BLE-return event: normal
Reader idle disposable-font cleanup must complete first, admission must still
fail, and only then may the bounded return cleanup release UI-fallback font
glyph arenas and empty CSS parser bucket capacity. There is no per-page
reclaim, timer, polling loop, repeated retry, or current-page/section/image
purge. A fragmented failure remains pending quietly until BLE lifecycle state
creates a fresh return event. The 98,304 free-heap and 36,852 largest-block
floors are unchanged; diagnostics distinguish free-heap shortage from
largest-block fragmentation. The reusable 40-row grayscale scratch is retained
because releasing it would force a new allocation on a connected cached turn;
it is not counted as expected recovery.

BLE-OFF remains inert: no return-pending state, BLE thresholds, return cleanup,
or NimBLE work affects ordinary Reader behavior. Reader exit, sleep, Wi-Fi,
duplicate-connect joining, and one-shot BluetoothSettings deferral remain
separate lifecycle paths.

## Regression restoration — 2026-10-08

Roosevelt EPUB images physically worked before the BLE coexistence work. This
pass restores `ImageBlock.cpp/.h` to the canonical 1.6.3-compatible pipeline:
source resolution/extraction, tolerant historical PXC replay, original decode,
draw, and cache generation. The later exact-geometry/strict-length policy,
temporary-cache promotion, cache-invalidating replay changes, and image-path
telemetry were removed from the normal path. The P1 figure/full-page parser
work remains layout-only: it chooses page geometry and caption/page breaks;
the canonical ImageBlock still owns JPEG/PNG/PXC loading and drawing.

BLE remains a lifecycle concern. The FreeInk host's synchronous in-flight
connection claim is retained, application callers now report join/no-op rather
than duplicate starts, and BluetoothSettings render deferral is coalesced to a
single pending event until render completion. The latest on-demand Reader
glyph experiment was reverted to the proven normal font scan/prewarm behavior.
Reader-exit shared font/cache/scratch release, the reusable 40-row scratch,
BLE-OFF inertness, and the 98,304 / 36,852 admission floors remain protected.

Web `/api/settings` bounded diagnostics/streaming remain separate and were not
changed by this image restoration. Physical Roosevelt validation and release
sign-off remain pending for the rebuilt binary.

## Stabilization correction — 2026-10-08

The physical candidate `9A23C1D70A161F8492E43F349AD036A88B6EB1D266F776112DCD1A40C04DF1D5`
regressed BLE-connected cached Reader turns. Its `reader_cached_reclaim` records
showed zero bytes reclaimed and then deferred BLE stops, so that latest
pre-render reclaim experiment is being removed. The previous cached-page path,
40-row renderer scratch, Reader-exit shared-cache release, event-driven
re-admission, sleep cleanup, and unchanged 98,304 / 36,852 floors remain in
scope.

The on-device BluetoothSettings bug was traced to the parent Settings activity
remaining on the activity stack and keeping global BLE admission sensitive.
BluetoothSettings now ignores only that lightweight Settings parent after its
own render work is quiescent; a Reader parent remains protective. Manual paired
or scan connect requests join an existing in-flight request instead of issuing
duplicate `connect_start` attempts.

Roosevelt images still show placeholders, but the latest physical log contains
no `EPUBIMG`, `.pxc`, or source-recovery records. Diagnostics now cover the
parser's pre-PXC decisions and every ImageBlock/layout placeholder, so the
actual failing source/format/dimension/cache decision can be identified. The
self-healing PXC path is not claimed physically executed until those records
appear.

Web Transfer/browser Settings remains a P0 hard-reset path. Repeated missing
`thumb_226.bmp` probes are now covered by a fixed-size, session-scoped negative
cache on both library and thumbnail routes, with no synchronous thumbnail
generation. `/api/library`, `/api/library/cover`, `/api/stats`, and both
`/api/settings` methods emit bounded begin/complete heap and client diagnostics.
The current ELF maps MEPC/RA to `esp_cpu_wait_for_intr`, saved PC to
`esp_system_reset_modules_on_exit`, and MTVAL into FreeRTOS `prvIdleTask`; this
does not prove WebServer ownership and remains compatible with a panic/reset
after another task corrupted memory or stack.

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

## 1.6.4 release-candidate checkpoint — 2026-10-07

The current Folio Nooir 1.6.4 checkpoint is being prepared on branch
`reconstruct/folio-nooir-1.6.4`. This checkpoint captures the integrated source,
tests, and documentation state; it is not a claim that the final physical BLE
validation has passed.

### P0 Web UI / Transfer

- The `HalFile::close()` startup assert in Transfer was traced to an ownership
  error and fixed with ownership-safe `closeFileIfOpen()` handling.
- Web UI / File Transfer was physically confirmed to open and work after the
  fix.
- The retained P0 memory work includes streamed `/api/stats`, no full stats
  duplication, reduced Settings descriptor copying, temporary upload buffers,
  library temporary-buffer reuse, thumbnail/client cleanup, WebSocket upload
  cleanup, complete Transfer teardown, and BLE re-evaluation after cleanup.

### Reader BLE lifecycle

The actual normal-page churn root cause was the unconditional post-input handoff
in `src/main.cpp`: `pollBle()` led to `bleHadActivityThisFrame()` and then
`bleinput::stop()` before Reader had classified the action. That unconditional
handoff and its pending flag were removed. Stable cached page turns should now
keep the running BLE host alive.

BLE is still deliberately stopped for resource-sensitive Reader paths:
initial unstable Reader open; section/cache build or miss; new-section
transitions; partial rebuilds; first-use image decode or cache misses; loading
UI; quality-recovery or full-refresh work; queued work while rendering is
occupied; exit/settings-triggered reflow; and Wi-Fi, Web UI, sleep, OTA, or
other resource-sensitive activities.

The `BLE wanted` and pending re-admission semantics remain intact: failed
admission does not clear pending intent, lifecycle events trigger
re-evaluation, and there is no polling loop. The memory floors remain 96 KiB
free heap and 36 KiB largest free block. Physical evidence showed repeated
NimBLE teardown/reinitialization to be the proven fragmentation amplifier; no
specific Reader cache was proven to be a permanent leak.

### P1 EPUB

The bounded P1 work remains present: small caps; `hgroup`; bounded `>` and `+`
selectors; verse hanging indents; letter roles; table captions; bounded
`colspan`; figure, `figcaption`, and full-page image handling; and CSS cache
version 14. The implementation keeps the performance rules: no DOM, no general
browser CSS engine, no deep or unbounded selectors, and the ordinary fast path
is preserved.

### P2 carryovers included in 1.6.4

- Empty CSS span/inline spacing now queues transient `padding-left` geometry
  onto the next real token. Line breaking, hyphenation, RTL reordering, and
  positioning account for it; no synthetic DOM token or cache-format change
  was added.
- KOReader oversized `getProgress()` handling uses a bounded 8 KiB streaming
  response buffer, rejects oversized, incomplete, or aborted bodies, and parses
  the bounded buffer directly without an extra retained `getString()` copy.
- No other P2 items were included in this checkpoint.
- The remaining P2 backlog is limited to items not already equivalent or
  superseded; broader parser, cache, global-per-book, and memory-policy changes
  remain deferred.

### Build and release-candidate checkpoint

- Version: **1.6.4**.
- Latest physical-test/release-candidate `firmware.bin`: **5,803,184 bytes**.
- SHA256: `7F9586900C9D4E4A5DA065E9E7E25AA155B9886795F1F8BB57B9E46D0D2EC2CE`.
- Build succeeded; flash/app slot is **5,789,035 / 6,553,600 bytes (88.3%)**.
- Static RAM is **60,996 / 327,680 bytes (18.6%)**.
- Linked components include **NimBLE-Arduino 2.5.1** and
  **BleKeyboardHost 1.0.0**.
- `git diff --check` was clean at the build checkpoint.
- Web UI physical validation passed; final BLE physical validation for this
  exact RC is still pending.

### Near-term roadmap

Next is the short physical BLE validation: cached page retention across pages
1–4; temporary drop and recovery when entering a heavy/new section; BLE return
after Transfer; and largest-block behavior. If that passes without a new
blocker, release 1.6.4 quickly rather than expanding scope.

Later 1.6.5 candidates are the remaining safe CrossInk/parser carryovers, a
regression/torture suite, more BLE controller UX/support, deterministic builds,
CBZ improvements, formal X3 validation, and X4 Pro/Classic work later.

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

## 1.6.4 stabilization continuation — 2026-10-08

The reconstructed local checkout is on `reconstruct/folio-nooir-1.6.4` at
`d9af683b6f9758c479fefe43aaecc0460c0b5803` (`Prepare Folio Nooir 1.6.4
release candidate`). This commit is the pre-final-fix safety checkpoint. The
working tree must not be reset or discarded; `sdkconfig.gh_release` is the
intentional untracked build configuration.

The unconditional post-input BLE teardown was removed before this checkpoint.
Physical evidence confirms the important cached-page behavior: BLE reconnects,
normal cached EPUB pages render, and BLE remains connected. Do not restore a
BLE stop after every page turn. The remaining BLE issue was the 8,000-byte
grayscale strip scratch allocated per tiled grayscale render while NimBLE was
resident, followed by poor largest-block recovery. The final-fix direction is
to reserve one reusable renderer-owned strip scratch during reader heavy setup,
reuse it for cached pages, release it before section/EPUB teardown, and only
gate a render when the scratch cannot be guaranteed. A local fallback remains
bounded and narrow.

Measured BLE admission floors remain unchanged: 98,304 bytes total free heap
and 36,852 bytes largest free block. Physical logs show that total free heap
can recover to roughly 98–102 KiB while the largest block remains around
34,804, 25,588, or 21 KiB; this is why cold admission can correctly fail. The
source proves existing rebuildable font/page/CSS cleanup boundaries, but the
exact allocator owner of every surviving hole is not proven by source alone.
BLE intent must remain wanted/pending after a failed floor check and be
reevaluated at real lifecycle transitions, never by polling.

BLE OFF is a required BLE-less mode: no NimBLE init, connection/reconnect,
admission-floor decision, pending work, BLE-specific render teardown, or
BLE-only memory reservation may affect the device. Turning BLE off at runtime
must stop NimBLE, release its resources, and clear pending intent; turning it
back on may re-enter normal admission. Home, Recent/Library, Reader,
Reader Settings, main Settings, sleep, Transfer, Web Settings, and Reading
Stats must remain ordinary BLE-less behavior.

The Web Settings and Reading Stats symptom is a full device reset, not a
normal Wi-Fi disconnect. Two patterns remain recorded: a degraded post-Reader
heap with repeated missing `thumb_226.bmp` activity, and a fresh-boot
immediate-Transfer reset before the normal server memory log. The pre-final-
checkpoint `gh_release` ELF matched the reported SHA256 prefix `b27d8dfff`;
`0x40389af6` and `0x40389ae6` resolve to `esp_cpu_wait_for_intr`, while
`0x4038e3b0` resolves to FreeRTOS `prvIdleTask`. The rebuilt candidate is a
different ELF and must not be used to decode that older panic. These addresses
do not identify an application WebServer root cause without the panic
reason/backtrace. Existing route streaming and ownership fixes remain
preserved. WebSocket shutdown now clears
the static callback trampoline before synchronous close callbacks, and missing
thumbnail 404s receive a short deterministic negative cache while valid cover
retrieval remains intact.

Do not call this release-ready until the new candidate passes physical BLE-on
cached-page, heavy-section/pending re-admission, BLE-OFF, and Web UI/Transfer
reset tests. Do not flash, commit, or push the final fixes automatically.

## 1.6.4 second stabilization pass — 2026-10-08

The first reusable-scratch candidate removed the physical `OOM: grayscale strip
scratch (8000 bytes)` failure and improved largest-block recovery to about
57,332 bytes after CSS cleanup. It still left Reader idle at roughly 97,756
bytes free / 57,332 largest, just 548 bytes below the unchanged BLE admission
floor of 98,304 bytes. Reader destruction returned about 9.3 KiB, consistent
with the session-long 8,000-byte scratch plus activity overhead; the source
confirms that the scratch was `displayWidthBytes * 80` rows.

The second candidate changes only the Reader strip geometry: the existing
band loop now uses 40 rows, so the reusable allocation is
`displayWidthBytes * 40` (about 4 KiB on the 800-pixel panel). The renderer
already handles arbitrary final bands and streams each band to the controller,
so AA/polarity behavior is unchanged while idle Reader should recover about
4 KiB of useful margin. The session scratch is still released before Reader
section/EPUB teardown. BLE floors and lifecycle semantics are unchanged.

The post-`connected_idle` `connect_start` was audited. The automatic host poll
queues a reconnect only when the live host is not connected, not already
connecting, and not scanning, after the permission callback allows it. The
application cold-start path is also gated by `!BleHid.isRunning()`. With no
complete log proving a redundant request, no BLE lifecycle rewrite or
speculative reconnect change was made; a later `connect_start` remains either
a real link-drop recovery or a deliberate direct settings request until the
physical log distinguishes those cases.

Sleep overlay PNG allocation remains behaviorally unchanged. Source inspection
shows PNGdec embeds a 32 KiB zlib window, inflate state, a 16,416-byte row
buffer, palette, and file buffer in each `PNG` object. That explains why a
contiguous decoder allocation can fail near a 57 KiB largest block despite
102–108 KiB total free, but the exact capability pool is not yet physically
proven. Decoder-failure telemetry now reports `sizeof(PNG)`, configured row
buffer, and free/largest values for default, internal 8-bit, DMA-capable, and
generic pools. No custom sleep image support was removed or blindly shrunk.

This candidate remains not release-ready until physical validation confirms
+Reader idle margin/BLE re-admission, repeated section transitions, BLE OFF
+inertness, the duplicate-connect interpretation, and sleep PNG diagnostics.

### Third stabilization pass — 2026-10-08

The latest physical run confirms that the 40-row reusable grayscale strip
removed the old 8 KiB scratch OOM and allows BLE to reconnect inside Reader
after the initial CSS cleanup (about 101,852 free / 65,524 largest). It also
shows a separate heap-shape failure: repeated BLE-connected cached/heavy page
turns progressively reduce the largest block, with the connected session
reaching about 5,612 bytes minimum free. After BLE stop and idle cleanup,
total free recovers but the largest block remains fragmented.

The strongest physical signature is an exact approximately 30,708-byte
largest-block ceiling across Reader exit: section release, EPUB release,
disposable-font release, and activity destruction all leave that ceiling in
place. RecentBooks then settles around 90–94 KiB free / 30,708 largest,
versus a clean baseline near 103 KiB / 65.5 KiB. Reader exit therefore was
not a hard recovery boundary. The 59,456-byte sleep-PNG decoder allocation
failure is now treated as a consequence of this fragmentation, not an
independent PNG design problem.

Source ownership audit identifies the most credible pinning owner: the global
SdCardFontSystem/font-manager retains Reader-built advance tables and lazy
kern/ligature data after disposable font cleanup. These allocations are
created lazily by page layout, can occur while NimBLE is resident, and outlive
both bleinput::stop() and EpubReaderActivity destruction. The static
ImageBlock pixel cache and global renderer state remain monitored suspects,
but their normal render guards release their temporary buffers. Physical
attribution to the exact 30,708-byte block is not yet proven until the new
before/after diagnostics are captured.

The lifecycle fix adds an explicit Reader-cache release after section/EPUB,
disposable-font, renderer scratch, and ImageBlock teardown. It clears
Reader-built font advance/kern/ligature/mini caches while preserving font
objects, coverage, registrations, and rebuildability. Reader exit now logs
heap shape at BLE-connected cached render, BLE stop, idle cleanup, every exit
stage, shared-cache release, activity destruction, and RecentBooks activation.
The NimBLE source audit already shows end() disconnects, deletes the client,
and calls NimBLEDevice::deinit(true); no BLE rewrite is justified without
physical before/after evidence.

### 1.6.4 fourth stabilization pass — 2026-10-08

The next physical evidence to address is BLE coexistence inside Reader, not
Reader exit: connected idle is commonly about 29–32 KiB free and cached/light
turns can consume more than 10 KiB, fragmenting the largest block and causing
BLE stop/reconnect cycles. The source audit found no retained previous Page
object to discard. This pass therefore adds a BLE-ON/runtime-only, headroom-
gated reclaim before cached/light renders. It releases a residual ImageBlock
pixel replay slot, previous-page footnote/highlight vector capacity, and
rebuildable SD-font glyph/bitmap/overflow arenas. Section layout, current page
state, EPUB ownership, CSS/parser state, stable-page data, font registration,
coverage, and essential metadata remain resident. If the local runtime policy
still fails after that bounded reclaim, the existing event-driven BLE stop and
render retry is used as the last resort. The cold admission floors remain
unchanged at 98,304 free / 36,852 largest.

BLE-OFF remains inert: the new policy is called only for an enabled, running
BLE host and a cached/light page, with no BLE-specific reclaim, render gate, or
fallback stop in the disabled path. It does not add polling or delays.

The Roosevelt `img_9_13.pxc` / `img_9_14.pxc` evidence is an invalid-cache
failure path, not proof that raw image size is too large. PXC v1 is the
historical four-byte width/height plus exact 2-bpp payload format and has no
separate version byte. Invalid geometry or exact-length mismatch now closes,
removes, and reports the cache; the original EPUB source is resolved or
re-extracted, decoded to the current page, written to a sibling temporary PXC,
atomically promoted, and validated before reuse. Bounded per-session failure
memoization remains in place. Diagnostics identify cache invalidation/removal,
source extraction, decode format/dimensions/heap, cache promotion, and source
render success. Hardware validation is still required to prove Roosevelt
images render on the target.

### Targeted stabilization continuation — 2026-10-08

Physical evidence from candidate firmware SHA256
`DB5CC1B2ED39022757CA5A524B703E90F4D9A167CA65353B00E3D826BD148F4E` and ELF
SHA256 `291E92734A8C2E286E73242397397425C6A6DB5D53EED6E3D1D7F7EB32117AC7`
shows cached BLE-connected Reader rendering falling to 1,128 bytes and later
704 bytes minimum free heap, then recovering at idle to roughly 29 KiB free /
26 KiB largest. Treat this as transient render pressure. BLE direct connection
inside BluetoothSettings passes; duplicate `connect_start` remains. Roosevelt
reaches `decision=image_block` for `OEBPS/image_rsrc587.jpg` with an existing
464x287 PXC cache, but the downstream replay/draw result is still unknown.
The `/api/settings` route begins near 34.7 KiB free / 22.5 KiB largest and
resets before completion. Reader-exit recovery still passes.

The continuation skips only the expensive font scan/prewarm on BLE-connected
cached pages and preserves the original BLE-OFF path. It claims BLE connection
ownership synchronously, logs one `connect_join_inflight`, traces PXC replay to
actual framebuffer writes, requires exact cache geometry/length, and replaces
the settings GET dynamic JSON document with a reusable 256-byte direct stream.
The Reader-exit boundary, shared cache release, 40-row scratch, BLE floors
98,304 / 36,852, BLE-OFF inertness, thumbnail negative cache, and existing
P0/P1/P2 fixes are not to be broadened or removed. Hardware validation remains
required before any release decision.

### Reader advance-workspace / Web teardown follow-up — 2026-10-09

The latest X4 report identifies a late-Reader `SdCardFont` topology failure.
`buildAdvanceTableRange()` previously requested a fixed 4,098-entry
`uint32_t` array (16,392 bytes) once per paragraph during uncached layout,
regardless of paragraph length. It now sizes that transient workspace to the
UTF-8 byte upper bound (capped at the unchanged 4,096 unique-codepoint limit),
checks the largest block before allocation, and memoizes a failed size per font
until topology improves or an explicit font-cache release occurs. No advance
table or mini-font cache is purged on ordinary cached page turns. Mini interval
allocation failure leaves the font on its stub view; its failed interval arena
may be gone while unrelated retained glyph/bitmap arenas remain, and later
glyph/read-order failures perform the existing full mini-arena cleanup.

Web teardown is now instrumented by owner (HTTP/request handlers/WebDAV,
WebSocket clients, UDP, upload/global state, DNS, mDNS, Wi-Fi scan state, and
radio deinitialization). Arduino's `WIFI_MODE_NULL` implementation already
stops and deinitializes the ESP Wi-Fi driver; no second raw deinit is added.
Home/RecentBooks transitions prepare this teardown before constructing the
replacement activity, and ActivityManager logs the post-stack
`exclusive_exit_clean` boundary. BLE remains Web-exclusive through teardown and
replacement UI entry.

`GET /api/settings` now walks static descriptors by reference and serializes
the dynamic sleep/font options directly, avoiding per-request copied
`SettingInfo` vectors and captured family-name copies. It retains the fixed
256-byte writer and exact Content-Length pass. Its Web-only admission floor is
20 KiB free / 8 KiB largest; the physical 10-refresh and full Web-exit checks
remain outstanding. This source pass does not alter EPUB image/PXC, section
build/suspend/cache semantics, XTC, BLE floors, or BLE-OFF behavior. The
separate BLE HID canonical-navigation changes in `MappedInputManager.cpp` and
`OptionPopup.h` are preserved in the same working tree and included in the
release build.

While Web-exclusive mode is active, ActivityManager now skips its recurring SD
UI-font load service; released fallback arenas are not recreated by the main
loop. The normal loader resumes after Web teardown and replacement UI entry.
Settings POST uses the same allocation-stable approach as GET by visiting base
settings by reference and applying dynamic keys directly.

### Physical stabilization follow-up — 2026-10-09

The Stats/Calendar parent-return path had two distinct defects: ActivityManager
held `RenderLock` while RecentBooks rebuilt state through a helper that
re-entered that lock, and the saved shelf framebuffer could still be treated as
valid after Stats had overwritten it. Restore now copies the in-memory
RecentBooks store once, restores the selected tab/index, invalidates only
rebuildable carousel sources, forces a fresh shelf frame, and keeps BLE gated
until `displayBuffer()` completes. No persistent book metadata scan is added.

`/api/settings` still uses a 256-byte application writer and exact Content-
Length pass, but chunks now go through synchronous `NetworkClient::write()`;
short writes stop the route instead of allowing descriptor generation to
continue after transport backpressure. Arduino-ESP32's lwIP headers mark
`SO_SNDBUF` unimplemented and do not expose queued-byte depth, so diagnostics
report the unavailable transport metric rather than pretending the queue is
bounded by a socket option. The route retains its 20 KiB / 8 KiB Web-only
admission guard.

Web exit explicitly closes the active HTTP socket before destroying handlers,
and records whether the default STA/AP `esp_netif` handles actually disappear
after Wi-Fi deinit. The ESP-NETIF/TCP-IP core is process-lifetime; do not call
the unsupported global deinitializer to chase free-heap readings. Whether the
remaining physical Web-exit deficit is TIME_WAIT/TCP state or another lower
layer owner still requires X4 evidence; the source now exposes the active
client and netif boundaries without delaying or polling.

SD-font render scopes now retain mini interval/glyph/bitmap/kern capacities;
the old low-free-heap scope reset caused repeated release/rebuild on cached
renders. Kern-row scratch is stack-bounded, failed kern/mini/advance
allocations are memoized to an explicit retry generation, and capacity growth
preserves the old arena if replacement allocation fails. `shrink_to_fit()` was
removed from the page prewarm scope. At Reader exit, the selected Reader-size
font's global coverage owner is detached unless it is also an 8/10/12-point UI
fallback; RecentBooks' separate UI fallback fonts remain registered. The next
Reader entry reloads the selected size once. Physical heap attribution and
snappiness still require target validation.

The reported eight-page pause is consistent with the existing five-page
Reader lookahead window: the builder intentionally waits for reading to advance
before extending farther. `SCT build_wait` now identifies `reader_window` vs
heap gates; no section persistence/build semantics changed. The prior
input-sized advance workspace fix remains intact. No image/PXC, XTC, BLE floor,
or BLE-OFF policy changes are part of this follow-up.

Final offline `gh_release` build succeeded on 2026-10-09. Reported static RAM
usage is 61,396 / 327,680 bytes (18.7%); flash usage is 5,814,185 / 6,553,600
bytes (88.7%). The build output and copied `artifacts/firmware.bin` match.
Physical validation remains required, especially Stats/Calendar usability,
Web transport minimum heap and post-exit networking topology, and Reader SDCF
stability/exit topology.

## Paused stabilization continuation — 2026-10-12

The working tree is still based directly on `d9af683b6f9758c479fefe43aaecc0460c0b5803`;
preserve all existing edits and the intentionally untracked `sdkconfig.gh_release`,
`src/util/ReaderRenderBudget.h`, and `work/` contents. Do not flash, stage,
commit, push, or reset. The user chose one-shot NTP on every station Wi-Fi
session regardless of the old opt-out. The clock now uses one UTC system epoch;
valid Weather local timestamps plus their returned UTC offset can initialize
that same epoch, and NTP is latched once per Wi-Fi session and stopped after
its bounded attempt. Weather remains optional/daily. The obsolete visible
clock opt-out was removed while the persisted field and `/api/clock-weather`
compatibility key remain; old POST values are ignored. No clock-tick flash
writes or Wi-Fi-only-for-time path was introduced.

Clock reads/writes now share a small HAL mutex because the render and main
tasks can observe/update the shared epoch concurrently. Wi-Fi event callbacks
only enqueue state; the normal event path and Wi-Fi-selection handoff run the
sync outside the callback. Web/Transfer's main-task HTTP handling remains
synchronous, so a new station session can still occupy up to the existing
five-second NTP bound; physical Web-session validation must check this.

Sleep is now an explicit heap-recovery boundary before artwork/PNG decode. It
logs and releases only existing reconstructible owners: disposable renderer
font caches, Reader/shared SD-font layout caches and unreferenced Reader-size
font ownership, ImageBlock's existing render cache, and the reusable grayscale
strip scratch. It does not change PNG/PXC/ImageBlock decode semantics or the
framebuffer. The Reader scratch is consistently 40 rows at reservation and
use (about 4 KiB on the 800-pixel panel); Reader exit also tears down section,
EPUB, SDCF Reader caches/coverage owner, ImageBlock cache, and scratch. These
are source-level lifecycle fixes, not proof of restored physical heap shape.

BLE cold-admission floors remain exactly 98,304 free / 36,852 largest; no
admission check runs for BLE-OFF Reader work. Defensive stop is inert when the
host is already stopped, and an OFF/no-host Web session does not create a BLE
exclusive gate or emit sleep stop diagnostics. Host-library source confirms
poll returns immediately when `begun_` is false and `end()` clears the queued
key ring during real shutdown. The 40-row scratch matches the tiled render's
strip size; its reusable allocation is tried before any render-time fallback.

The Web Settings/Reading Stats symptom remains a full-reset report, not a
confirmed Wi-Fi disconnect. The only local `.pio` ELF hashes to
`43e31f8d5634661b4b435ba8b7de9e9d894c3ab1c10a7dbd151d0f568963689d`, which
does not match the supplied panic prefix `b27d8dfff`; no other ELF is present
in the checkout. Do not symbolicate that panic against this ELF. Panic class
and root cause remain unproven. Source traces show synchronous WebServer
handlers, parent-owned activity lifetime, server-owned route closures, and
ordered server/client/WebDAV/WebSocket/Wi-Fi teardown; Settings/Stats routes
do not call Wi-Fi disconnect. Preserve the streamed Stats rows, existing
cover display, and bounded 24-entry/five-minute missing-thumbnail cache.

The current source has not yet had the requested final build. Run
`git diff --check`, offline-only available static/tests, then exactly one
`pio run -e gh_release` when the tree is coherent. Record BIN/ELF/MAP paths,
sizes and SHA256, app-slot/flash and static DRAM usage, NimBLE link proof,
warnings and final Git status. Never flash automatically. Physical X4 BLE,
BLE-OFF, sleep-PNG, Stats/Calendar, Web Settings/Stats/Transfer, thumbnail,
upload, and Web-exit checks remain mandatory; do not mark release-ready before
those tests pass.

### Final source/build pass — 2026-10-12

This section supersedes the preceding build-pending paragraph.

The final `gh_release` build succeeded. RAM is 61,428 / 327,680 bytes
(18.7%); app slot is 5,820,533 / 6,553,600 bytes (88.8%). BIN:
`artifacts/firmware.bin`, 5,834,688 bytes, SHA256
`2A4C06489D1F8CC3782DC2F2FA3A706DB6EC0F56426D8DCD13271FF436370E78`.
ELF: `.pio/build/gh_release/firmware.elf`, 81,625,368 bytes, SHA256
`259744C6D47D412E95BB208EEDC5DADFA7E599984944A739010BED1D63728F2C`.
MAP: `.pio/build/gh_release/firmware.map`, 22,074,775 bytes, SHA256
`A7D8FB12E81FB516213EC1DCB1C4067A760337333873BFDD8173A0B724C95F1A`.
The linked ELF contains `BleKeyboardHost::begin/poll/end`,
`NimBLEDevice::init`, and `nimble_port_init`. No build errors; the existing
`CarouselTheme.cpp` explicit-`Rect` constructor warning remains. The first
PlatformIO invocation stalled in its Windows cp1252 output thread; the same
incremental build completed with UTF-8 output in 4m53s. No flash, stage,
commit, or push was performed. Physical X4 tests remain the release gate.
Automatic NTP reuses HalClock's existing bounded wait (up to five seconds) on
the main task; a Wi-Fi reconnect during an active Web session may pause request
handling for that bound. The old approximately 2.7 KiB Web-exit heap decrease
still has no source-proven owner; use the new per-owner physical checkpoints.

### Resumed source pass — 2026-10-12 (final build pending)

The local checkout remains the source of truth at
`d9af683b6f9758c479fefe43aaecc0460c0b5803`, branch
`reconstruct/folio-nooir-1.6.4`; preserve all existing dirty/untracked work.
The unconditional post-input BLE stop remains removed, and physical cached
Reader pages have already proved they can render while BLE stays connected.

The BLE Settings-row defect was in `MappedInputManager::bleEdge`: the
NavNext/NavPrevious cases handled PageForward/PageBack and side aliases but
omitted the direct canonical BLE edge. Direct canonical actions are now
consumed once through that same logical path; physical side mapping and Reader
page turns are unchanged. Reader return recovery now rechecks Level 1 after
its disposable font/empty CSS/bookmark releases, then may release the
renderer-owned grayscale strip exactly once at an idle boundary if admission
still fails. The strip is 8,000 bytes on the tested path; the existing
render-priority path stops BLE before rebuilding it. Cold BLE floors stay
98,304 free / 36,852 largest. `render_work_pending_or_inflight` is an explicit
queued/in-flight-work diagnostic, not a static activity label.

The 34,804 largest-block topology cause is still unproven. Targeted 8 pt font
coverage allocation/release pointer logs were added; the known 2,064-byte
coverage total remains only a hypothesis until X4 logs correlate those owners
with actual largest-block recovery. Sleep now also releases rebuildable
interface-font glyph/layout caches before custom PNG decode while retaining
font coverage ownership.

The Web Settings symptom remains a full device reset, not an ordinary Wi-Fi
disconnect. The current panic prefix `259744c6d` matches the pre-change local
ELF SHA256 exactly. The bundled Windows addr2line binary cannot launch
(`Illegal System DLL Relocation`), but pyelftools resolved the exact ELF DWARF
rows: MEPC `0x40389af6` -> `esp_cpu_wait_for_intr`, `cpu.c:64`; RA `0x40389ae6`
-> the same function, `cpu.c:57`; MTVAL `0x4038e3b0` -> `prvIdleTask`,
`tasks.c:4576`; saved PC `0x4038c1e0` -> `esp_restart_noos`,
`system_internal.c:115` (`ESP_INFINITE_LOOP`). The idle-task address lies in
deleted-task cleanup, and its idle hook calls the CPU wait function. The panic
reason/full backtrace is absent, so the exception class and application/Web
root cause remain unproven. The saved PC is not
`esp_system_reset_modules_on_exit`, which begins at `0x4038c1e2`.

`NetworkClient::write()` accepts bytes into the lwIP send path after a
socket-writable check; it does not prove remote ACK, and its `flush()` clears
RX data rather than draining TX. The exact TX queue depth is unavailable. The
bounded Settings serializer remains, but the prior 9,489-byte response showed
about 24,548 bytes of transient heap pressure. The Web UI now fetches
sequential descriptor pages, the old full-array API is retained behind a
response-size heap budget, and per-write requested/accepted bytes plus
before/after heap/client diagnostics are logged. This is source-level risk
reduction, not physical proof of stable repeated requests.

The Stats return/streaming behavior and unified UTC NTP/Weather clock path
remain preserved. NTP is one-shot on every existing Wi-Fi session, even when
the former auto-clock opt-out is Off; Wi-Fi is never enabled solely for NTP,
and no periodic polling is added. BLE-OFF continues to bypass host/admission
work in source; hardware BLE-OFF acceptance remains pending. Run source review,
`git diff --check`, available offline checks, then exactly one final
`gh_release` build. Do not flash, stage, commit, push, or call 1.6.4 release
ready before physical X4 validation.

Latest Settings BLE row-navigation report: `selectedSettingIndex == 0`
highlights the category tabs; positive indices highlight list rows, and the
option popup consumes input first. Continuous tab changes are gated to tab
focus because `ButtonNavigator` suppresses the matching release after a
continuous move. PageForward/PageBack BLE aliases remain; this is not an
alias-only fix. X4 must verify row steps, one-shot Confirm/Back, popup movement,
and unchanged physical side-button/Reader behavior.

The Reader cached-render largest-block budget now includes the full grayscale
strip allocation, so after a targeted Level-2 release it will not treat a
smaller contiguous block as sufficient to rebuild the strip.

Final `gh_release` build succeeded on 2026-10-12 (Python 3.12, UTF-8 mode).
`artifacts/firmware.bin`: 5,837,552 bytes, SHA256
`32B4BDC1F41031F77FFAE5793B9A037FCC253A573A8D960289F3E81EA578DA92`;
ELF: 81,633,764 bytes, SHA256
`254771284DC92529245C470ADBEB5111690504691B53A873A60A4AE70DD373B3`;
map: 22,076,112 bytes, SHA256
`CCBA54009D157B4F11611963D9B7C4A25AB0BE0558121C2E30A17D2B7333B561`.
Linked usage is 61,428/327,680 B RAM and 5,823,409/6,553,600 B app slot.
The map/symbol table contains BleKeyboardHost and NimBLE functions. No CMake,
CTest, or host C++ compiler is available for offline gtests; the Settings page
JavaScript syntax check and `git diff --check` pass. No flash, commit, or push
was performed. This remains a physical-validation candidate, not a
release-ready build.
