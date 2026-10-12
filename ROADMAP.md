# CrossPoint Reader Roadmap

## Combined X4 stabilization pass — 2026-10-09

- Stats return now releases the retained RecentBooks local book/string copy at
  the explicit child boundary and restores it once from the in-memory
  persistent store before the parent shelf render. This targets the topology
  pin without rebuilding RecentBooks during ordinary browsing.
- Settings BLE uses an explicit lifecycle state/epoch. A render queue is no
  longer treated as continuous Settings pressure, so a connected notification
  cannot cause the idle connect/stop loop. Re-admission consumes the pending
  transition; new stops require new activity/resource events.
- Web/Transfer owns the heap exclusively for its session, suppresses BLE, and
  releases rebuildable runtime caches once on entry. Web settings streaming is
  fixed-buffer/two-pass and guarded by a Web-only memory floor.

No timers, delays, polling retries, per-frame cleanup, cached-page cleanup, or
normal RecentBooks rebuild loop was added. The unchanged BLE floors,
BLE-OFF behavior, image behavior, Section semantics, XTC, and sleep boundary
remain protected. Physical X4 validation is still required.

## X4 COM audit repair gate — 2026-10-09

The current implementation has three bounded, event-driven repairs:

- Reader may release the already-proven global SD-font Reader caches once,
  only after failed normal BLE-return cleanup and only when cold/incomplete
  section or heavy image/scratch work armed the tier. Cached page turns do not
  trigger it.
- Stable Settings may release only the built-in compressed-font page/hot
  buffers once after a BluetoothSettings return. The observed `99.6/34.8 KB`
  case must still be checked physically against the unchanged floors.
- ReadingStats releases its snapshot before child destruction, then the safe
  shared built-in font scratch, and logs the child-destroyed and parent-resume
  boundaries. The retained RecentBooks local copy is now restored once after
  Stats child destruction; no normal shelf rebuild is introduced.

The X4 log does not show monotonic heap erosion caused by repeated NimBLE
stop/start on image misses, so that behavior remains unchanged. Wi-Fi's
low-memory Settings case remains deferred because no stale child owner was
proven. Validation requires one offline `gh_release` build, artifact checks,
and physical tests for next-chapter image reading, stable Settings, Stats to
RecentBooks, cached turns, Reader exit, Wi-Fi, and BLE-OFF.

The final offline `gh_release` build completed successfully at 61,396 bytes
static RAM and 5,804,659 bytes linked flash. Physical X4 validation remains
the release gate.

## Focused BLE stabilization pass — 2026-10-09

The next physical validation gate is now narrowly defined:

- Settings returns from BluetoothSettings through a render-complete, event-driven
  BLE admission path; no timer, polling, or duplicate start is allowed.
- An empty `EpubReaderActivity::cachedBookmarks` vector may release its reserved
  16-entry capacity once at BLE-return quiescence, with a single admission recheck.
- RecentBooks transition diagnostics must show whether the outgoing rebuildable
  book vector release improves heap topology before Reader construction. No normal
  shelf cache purge is permitted.

The fragmented Reader case stays pending when the largest block remains below
36,852. The 98,304 free floor, 36,852 largest-block floor, 40-row scratch,
image/PXC behavior, Section semantics, Web settings, Reader-exit cleanup, and XTC
activity-specific policy are protected. Release validation still requires the
focused source checks, one `gh_release` build, artifact hashes/sizes, and physical
acceptance on clean EPUB, fragmented RecentBooks-to-Reader, ten-plus cached turns,
images, Reader exit, and BLE OFF.

## Uncached section post-persist release — 2026-10-08

After uncached section persistence, release builder-only state before BLE
return admission; cached page behavior must remain unchanged. Section-local
parser/Expat state, build scratch, page LUT, build strings, and temporary HTML
ownership end with the committed section build. The newly explicit release
boundary also drops empty shared CSS hash-bucket capacity only after the
section build context has been destroyed. Current page/Section state, live CSS
rules, images, font caches, reusable scratch, BLE floors, and one-shot return
behavior remain protected. Physical acceptance still requires a measurable
post-persist gain and a clean cached-page regression check.

## BLE return polish — 2026-10-08

Protect the restored canonical 1.6.3 ImageBlock/PXC pipeline while completing
the narrow BLE lifecycle fix. A connected, enabled BLE session may be stopped
for heavy Reader work and must return automatically once the Reader is
quiescent and the unchanged 98,304 / 36,852 admission floors pass.

The new return cleanup is one-shot and event-driven per BLE-return event. It
follows the existing normal idle disposable-font cleanup and runs only after
failed admission. Its safe, audited candidates are UI-fallback font glyph
arenas and empty CSS parser bucket capacity. Current page/section ownership,
EPUB/source state, image replay/cache semantics, and 40-row renderer scratch
are preserved. There is no per-page reclaim, timer, polling, delay hack, or
repeated retry; a failed return remains pending quietly until BLE lifecycle
state creates a fresh return event. Logs report free-heap and largest-block
gains separately. The scratch is intentionally retained because lazy
recreation would make connected cached turns allocate again.

BLE-OFF remains the normal Nooir path with no return state or BLE thresholds.
Reader exit recovery, duplicate-connect joining, one-shot BluetoothSettings
deferral, Sleep, and Web remain separate validations.

## Regression restoration pass — 2026-10-08

Restore the known-good EPUB image behavior before any further BLE optimization:
Roosevelt images worked before BLE coexistence changes, so ImageBlock/PXC
strictness is not an accepted compatibility fix. The canonical historical PXC
flow is restored, while committed figure/full-page/CSS work remains responsible
only for destination geometry, page ownership, and caption layout. BLE connect
ownership and BluetoothSettings admission are kept at the lifecycle boundary;
the repeated render-deferral log is now one-shot/event-driven. The latest
on-demand glyph path is reverted to the prior normal prewarm behavior. Reader
exit recovery, 40-row scratch, BLE-OFF inertness, unchanged floors, and Web
settings streaming remain protected. Physical Roosevelt and BLE validation are
still release gates.

## Stabilization correction — 2026-10-08

The latest physical candidate proved the BLE-connected cached-render reclaim
experiment was a regression: repeated `reader_cached_reclaim` records freed
zero bytes and still caused deferred BLE stops/reconnect churn. That experiment
is reverted while the accepted cached-page behavior, 40-row scratch,
Reader-exit recovery, sleep cleanup, event-driven admission, and 98,304 / 36,852
floors remain frozen.

Current release blockers are the on-device BLE toggle needing Home navigation,
Roosevelt placeholders with no PXC diagnostics reaching the physical failure,
and the Web Transfer/browser Settings hard reset. The correction pass adds
quiescent BluetoothSettings admission, in-flight connect deduplication,
top-level EPUB image placeholder reasons, bounded Web thumbnail negative
caching, and route-level Web heap/client diagnostics. No release claim is made
until the exact new binary is physically tested.

This roadmap describes how CrossPoint is moving through the tighter scope defined in [SCOPE.md](SCOPE.md). It is
intentionally phased: Phase 0 closed out the commitments already in flight before locking down to the stricter
"fill gaps the stock firmware leaves" delineator.

Phases are sequential. We do not start the next phase until the prior one is wrapped or explicitly carried over.

---

## Phase 0 - Close Out Legacy Scope Items — **COMPLETE**

**Goal:** Land the work that was already in motion under the prior, broader scope so contributors are not left
hanging, and so we enter the stricter phases with a clean slate.

**Landed in Phase 0:**

* **RTL support PRs.** The in-flight right-to-left work was reviewed, iterated, and merged.
* **Dictionary PR.** The offline dictionary lookup work was reviewed and merged.
* **Bookmarks** feature. First-class navigation markers in EPUBs.
* ~~**Transparent sleep screens.**~~ Shelved; not picked back up under the stricter phases.

Phase 0 is closed. The tighter scope in [SCOPE.md](SCOPE.md) is now fully enforced. "But it was on the old roadmap"
is not a valid argument for accepting a PR.

---

## Phase 1 - Consolidation, Footprint, and Multi-Device Support — **IN PROGRESS**

**Goal:** Reduce memory and flash usage, clean up the codebase, and land the SDK / HAL generalization work so
CrossPoint runs cleanly on ESP32-based e-reader hardware beyond Xteink (X3 / X4), including ESP32-S3 class devices.

**Focus areas:**

* DRAM and heap fragmentation reduction across the reader core.
* Flash footprint reduction (dead code, redundant strings, oversized tables).
* Refactors that tighten the HAL / SDK boundary.
* Pluggable per-device SDK layers (display, input, storage, battery) and per-device build configuration without
  forking the reader core.
* Documentation for adding a new ESP32 e-reader target.
* E-ink driver refinement (ghosting, partial update behavior).

**Closed during this phase:** new themes built into firmware, new external network connectors (sync engines, cloud
storage, remote file access).

---

## Phase 2 - Languages, Fonts, and Themes

**Goal:** With the codebase smaller and portable, make reading great in every language: multi-language support,
better font support with custom fonts, UI translations, and themes loaded from the SD card instead of consuming
flash.

**Focus areas:**

* Multi-language reading support (underserved languages, complex script support where realistic on ESP32 hardware).
* Better font support and custom fonts.
* UI languages and localization.
* Moving themes off-firmware to SD-loaded assets (see SCOPE.md Section 6).
* **Moving hyphenation files off-firmware.** Hyphenation rules vary per language and the files are large (German
  alone is ~200KB). Today these eat flash budget that should be available for the reader core. The plan is to build
  a downloader analogous to the existing font downloader and store the dictionaries on SD / SPIFFS, loading on
  demand. This unlocks better hyphenation for long-word languages (German, Finnish, Norwegian, etc.) without paying
  the flash cost up front.

This phase depends on Phase 1 cleanup landing first; otherwise we generalize a moving target.

---

## Out of Roadmap

The following are explicitly *not* on the roadmap. They may live in other CrossPoint forks; they will not be picked
up here:

* Interactive apps (games, calculators, notepads).
* Writing / authoring tools.
* Active connectivity features (RSS, news, browsers).
* PDF rendering as a first-class format.

See [SCOPE.md](SCOPE.md) for the full rationale.

---

## How This Roadmap Changes

* Phase boundaries are decided by maintainers, not by individual PRs.
* If a phase needs to be extended or an item carried over, that is documented here with a short note.
* Proposals for new phases or reordering should go through a Discussion first.

---

## Folio Nooir 1.6.4 release-candidate checkpoint — 2026-10-07

The current `reconstruct/folio-nooir-1.6.4` checkpoint carries the integrated
P0 Web UI / Transfer ownership and memory fixes, the corrected Reader BLE
lifecycle, bounded P1 EPUB work, and only the two approved P2 carryovers. The
full engineering record, preservation boundaries, build measurements, and
physical-evidence status are in
[`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md#164-release-candidate-checkpoint--2026-10-07).

The release candidate is version 1.6.4 with a successful build: `firmware.bin`
is 5,803,184 bytes, the app slot is 5,789,035 / 6,553,600 bytes (88.3%), and
static RAM is 60,996 / 327,680 bytes (18.6%). Web UI physical validation has
passed; final BLE physical validation for this exact binary remains the release
gate.

Near-term order:

1. Validate cached page retention, heavy-section temporary BLE drop/recovery,
   post-Transfer BLE return, and largest-block behavior on the exact RC.
2. If no blocker appears, release 1.6.4 quickly without expanding scope.
3. Keep the remaining safe CrossInk/parser carryovers, regression/torture
   suite, BLE controller UX/support, deterministic builds, CBZ improvements,
   formal X3 validation, and later X4 Pro/Classic work for 1.6.5 or later.

### Stabilization continuation — 2026-10-08

The final 1.6.4 candidate remains gated by physical validation. The safe
checkpoint is `d9af683b6f9758c479fefe43aaecc0460c0b5803`; the local reconstructed
checkout is authoritative for edits and builds. The cached-page BLE retention
fix is physically confirmed and must remain: normal cached page turns do not
tear down a connected BLE host.

Remaining release work is deliberately narrow: replace the per-render 8 KB
grayscale scratch allocation with reader-session renderer scratch reserved
during BLE-off heavy setup; release it before Reader section/EPUB teardown;
preserve wanted/pending BLE re-admission and the unchanged 98,304 / 36,852
admission floors; verify BLE OFF behaves as BLE-less firmware; and finish the
WebServer reset investigation without removing Web UI functionality. The
matching panic ELF currently symbolicates only to idle/wait framework code, so
the WebServer reset cause is not declared proven from the partial register set.

No 1.6.4 release should be tagged or pushed until the exact final binary passes
the physical cached-page, heavy-section, pending-admission, BLE-OFF, Transfer,
Settings, Reading Stats, thumbnail, and upload checks.

### Second stabilization candidate — 2026-10-08

Physical RC evidence showed that the reusable 8 KiB Reader scratch removed the
old grayscale OOM and improved largest-block recovery to about 57 KiB, but its
session-long lifetime left idle Reader at about 97,756 bytes free, below the
98,304-byte BLE floor. The candidate now uses 40-row strips (derived from the
existing `displayWidthBytes * rows` loop) for roughly 4 KiB retained scratch;
the floors remain 98,304 / 36,852 and cached-page BLE retention remains in
scope.

The duplicate post-connected `connect_start` is under audit. Source currently
requires the host auto-reconnect path to observe a live disconnected,
non-connecting, non-scanning state, so no connection lifecycle rewrite is
approved without a log proving a redundant request. Sleep overlay PNG behavior
is unchanged; new diagnostics expose the fixed decoder object size and
+capability-specific heap limits because failures occurred near 108 KiB total
+free / 57 KiB generic largest.

### Third stabilization candidate — 2026-10-08

Physical evidence now separates the successful 40-row scratch change from a
remaining ownership bug. BLE can reconnect in Reader initially, but repeated
BLE-connected page turns damage heap shape. After BLE stop, Reader idle, and
full Reader exit, the largest block can remain exactly about 30,708 bytes
through section, EPUB, disposable-font, and activity teardown; RecentBooks
then remains around 90–94 KiB free / 30,708 largest instead of the clean
approximately 103 KiB / 65.5 KiB baseline. The 59,456-byte sleep-PNG failure
is consequently a fragmentation symptom.

The source-level fix releases Reader-built SdCardFont advance, kern/ligature,
and mini caches at the Reader boundary, after normal activity-owned teardown,
and also releases shared ImageBlock/render scratch. Font registrations and
coverage remain rebuildable. This is the leading pinning owner, but exact
physical attribution to the 30,708-byte block remains pending targeted logs.
NimBLE end/deinit already performs client deletion and controller teardown, so
BLE is not rewritten or its floors changed. The candidate remains gated on
physical Reader-exit recovery and the full release checklist.

### Fourth stabilization candidate — 2026-10-08

Implement and physically validate a bounded BLE-connected Reader render policy.
The trigger is local to enabled/running BLE plus cached/light page rendering;
healthy headroom leaves all caches untouched. Marginal headroom reclaims only
the residual pixel replay slot, old page-local footnote/highlight vectors, and
rebuildable SD-font glyph arenas before rechecking. Persistent Section/layout,
EPUB, CSS, current-page, stable-page, and essential font state are preserved.
If the recheck remains unsafe, defer to the existing event-driven BLE stop and
retry. BLE-OFF behavior and the 98,304 / 36,852 cold floors remain unchanged.

Treat Roosevelt image placeholders as an invalid/stale PXC recovery issue until
hardware proves otherwise. Keep the historical PXC v1 layout compatible, reject
bad geometry or non-exact payload length, remove invalid files, recover the
source EPUB item, decode immediately, promote a validated temporary cache, and
avoid retry storms. The focused PXC policy tests cover valid geometry, header
geometry, one-pixel tolerance, short payload, and trailing payload; the target
still needs physical `img_9_13` / `img_9_14` confirmation.

### Targeted stabilization continuation — 2026-10-08

The latest physical evidence makes transient cached-render pressure the Reader
priority: BLE-connected cached rendering reached minimum free heap 1,128 bytes
and later 704 bytes, while quiescent idle recovered to about 29 KiB free /
26 KiB largest. The candidate also confirms direct BLE connection inside
BluetoothSettings, but duplicate `connect_start` remains. Roosevelt reaches
the generic ImageBlock decision for `OEBPS/image_rsrc587.jpg`, cache present,
464x287 geometry; replay/draw is still unproven. `/api/settings` begins near
34.7 KiB free / 22.5 KiB largest and resets before completion.

This pass narrows Reader pressure by skipping only BLE-connected cached-page
font scan/prewarm; BLE-OFF remains the original path. It fixes connection
ownership before async notification, adds exact PXC replay/draw diagnostics and
exact geometry validation, and changes settings GET to bounded direct streaming
with heap/stack/client stage logs. Preserve Reader-exit recovery, 40-row
scratch, BLE floors 98,304 / 36,852, BLE-OFF inertness, the 24-entry/five-minute
thumbnail cache, and all proven P0/P1/P2 fixes. One offline `gh_release` build
is required after checks; hardware validation is still the release gate.

### Focused memory follow-up — 2026-10-09

Reader paragraph advance-workspace sizing/failure memoization and Web-exclusive
server/Wi-Fi teardown diagnostics are in the 1.6.4 stabilization candidate.
`GET /api/settings` uses direct descriptor serialization and a Web-only
20 KiB / 8 KiB admission floor. No steady-state font purge or BLE-navigation
redesign is included. One offline `gh_release` build is required; physical
Reader topology, Web Settings repetition, and Web-exit heap recovery remain
acceptance gates.

The Web-exclusive activity now also suppresses ActivityManager's recurring SD
UI-font loader until Web teardown and replacement UI entry complete. Settings
POST applies dynamic sleep/font selections directly rather than rebuilding
font descriptor vectors on each save.

Final offline `gh_release` build succeeded on 2026-10-09: RAM 61,396 / 327,680
bytes (18.7%), flash 5,814,185 / 6,553,600 bytes (88.7%). This is compile/link
validation only; the physical acceptance gates above remain open.

### Latest physical follow-up — 2026-10-09

Prioritize a usable Stats/Calendar return: reconstruct RecentBooks' released
in-memory shelf copy once, force a real resumed frame, and admit BLE only after
that frame is on screen. The previous freeze was a nested render-lock restore
plus stale snapshot framebuffer reuse.

Bound Web Settings transport by synchronous 256-byte socket writes and stop on
a short write; retain the Web-specific memory guard. Web teardown now closes
the active HTTP client and verifies STA/AP netif destruction before the shelf
is reconstructed. The ESP-NETIF/TCP-IP core remains process-lifetime and the
physical post-Web heap deficit is still an acceptance risk; do not force an
unsupported global deinit.

Retain SDCF mini arenas through normal cached rendering, use explicit retry
generations for failed font allocations, and remove per-scope shrink/rebuild
churn. Detach the Reader-only coverage owner at Reader exit while preserving
separate shelf UI fallback sizes. Keep the five-page section lookahead; use
`SCT build_wait` to distinguish intentional reader-window pauses from heap
deferrals. Final `gh_release` build and physical checks remain mandatory; no
release-readiness claim follows from compilation alone.

### Clock and hard heap-boundary continuation — 2026-10-12

The user selected one-shot clock sync on every station Wi-Fi session. NTP and
valid Weather timestamps update the same UTC epoch; Weather's location offset
is presentation/date metadata, not a second clock. The old visible clock
opt-out is retired while its stored setting and compatibility API key remain.
NTP is deduplicated per Wi-Fi session, has no periodic service after the
bounded attempt, and never turns Wi-Fi on by itself.

Sleep entry now releases rebuildable renderer/SD-font/ImageBlock cache owners
and the reusable Reader grayscale strip before custom PNG allocation. Reader
exit also releases the selected Reader-size coverage owner and session caches.
The shared Reader strip is 40 rows (about 4 KiB on X4) at both reservation
and use. Source inspection supports these ownership boundaries; physical
largest-block recovery is still unverified.

Cold BLE floors remain 98,304 / 36,852. BLE-OFF is kept inert, including
defensive Web/sleep stop calls when no host exists. The local ELF SHA
`43e31f8d…` does not match panic prefix `b27d8dfff`, and no matching ELF is
available, so exact Web panic symbolication/root cause is unresolved. One
offline `gh_release` build and the full physical acceptance list remain gates;
this is not release-ready based on source/build checks alone.

The successful 2026-10-12 `gh_release` build used RAM 61,428 / 327,680
(18.7%) and app-slot space 5,820,533 / 6,553,600 (88.8%). BIN SHA256
`2A4C06489D1F8CC3782DC2F2FA3A706DB6EC0F56426D8DCD13271FF436370E78`;
ELF SHA256 `259744C6D47D412E95BB208EEDC5DADFA7E599984944A739010BED1D63728F2C`;
MAP SHA256 `A7D8FB12E81FB516213EC1DCB1C4067A760337333873BFDD8173A0B724C95F1A`.
This supersedes the preceding build-pending note only; physical X4 acceptance
is still required, and the Web reset root cause is not proven.

### Resumed P0 source work — 2026-10-12 (one new build pending)

Safe checkpoint is still `d9af683b6f9758c479fefe43aaecc0460c0b5803`; the
existing dirty candidate must be preserved. Cached-page BLE coexistence is a
physical success and the unconditional page/input teardown stays removed.

Source changes in this continuation:

- Settings navigation includes direct BLE NavNext/NavPrevious edges and the
  PageForward/PageBack aliases. `selectedSettingIndex == 0` is the tab-bar
  focus sentinel; positive indices focus rows. Held tab-changing callbacks now
  run only with tab focus, because ButtonNavigator suppresses the matching
  release after a continuous move. Confirm/Back and popup routing remain on
  their existing paths; X4 must verify BLE row movement and popup behavior.
- Reader BLE return performs a single Level-2 release of the reusable 8,000 B
  grayscale strip only after Level 1 misses admission at quiescence, then
  immediately rechecks unchanged 98,304 / 36,852 floors. The strip is rebuilt
  through the existing BLE-stop preflight before grayscale work.
- Admission diagnostics distinguish queued/in-flight render work from a
  static “render” label. Reader exit still releases section, EPUB, scratch,
  image render cache, session font state, and the Reader-only font owner.
- Sleep also releases the separately owned interface-font glyph/layout cache
  before custom PNG decode, retaining font coverage and UI functionality.
- 8 pt font coverage now logs allocation/free pointers only at load/unload
  boundaries. The 34,804 largest-block cause remains unproven pending physical
  pointer/heap correlation; the 2,064 B total is not treated as proof.
- Settings JSON remains bounded and keeps the complete no-query API, but now
  has a per-response transport headroom budget. The browser fetches sequential
  pages and retries smaller pages on preflight 503. Network write diagnostics
  report accepted (not ACKed) bytes and before/after heap; queue depth is not
  exposed.

The `259744c6d` Web panic prefix exactly matches the old local candidate ELF.
The bundled Windows `addr2line` executable cannot launch (`Illegal System DLL
Relocation`), but pyelftools resolved exact DWARF rows: MEPC `0x40389af6` ->
`esp_cpu_wait_for_intr`, `cpu.c:64`; RA `0x40389ae6` -> the same function,
`cpu.c:57`; MTVAL `0x4038e3b0` -> `prvIdleTask`, `tasks.c:4576`; saved PC
`0x4038c1e0` -> `esp_restart_noos`, `system_internal.c:115`. The idle-task
path includes deleted-task cleanup and its idle hook calls the CPU wait
function. The panic reason/full backtrace is absent, so the exception class
and application/Web root cause remain unproven. Web route/Stats/Transfer,
BLE-OFF, largest-block, sleep PNG, and calendar acceptance remain physical
gates. Preserve the unified UTC clock path: one NTP attempt per already-active
Wi-Fi session regardless of the old auto-clock opt-out, never start Wi-Fi just
for time and never poll periodically. Run checks and one final `gh_release`
build only; do not flash, commit, or push.

#### Final `gh_release` build — 2026-10-12

The build succeeded (Python 3.12, UTF-8 mode): BIN 5,837,552 B,
SHA256 `32B4BDC1F41031F77FFAE5793B9A037FCC253A573A8D960289F3E81EA578DA92`;
ELF 81,633,764 B, SHA256
`254771284DC92529245C470ADBEB5111690504691B53A873A60A4AE70DD373B3`;
MAP 22,076,112 B, SHA256
`CCBA54009D157B4F11611963D9B7C4A25AB0BE0558121C2E30A17D2B7333B561`.
Static DRAM: 61,428/327,680 B; app slot: 5,823,409/6,553,600 B (88.9%).
The map/symbol table confirms linked NimBLE and BleKeyboardHost. Offline host
tests could not run (no CMake/CTest/compiler); Settings JS syntax and
`git diff --check` passed. No flash/commit/push. Physical X4 validation remains
required; this is not release-ready.
