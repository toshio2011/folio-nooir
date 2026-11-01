# Folio Nooir 1.6.7 — Snappiness and Heap Audit Handover

**Date:** 2026-11-01 (MYT)  
**Repository:** https://github.com/toshio2011/folio-nooir  
**Working branch audited:** codex/nooir-1.6.7-refinement  
**Audited remote HEAD:** d8e75cbba88a98452f957d2182f28e140725e9c9  
**Default branch at audit:** codex/folio-nooir (1e10e5deb7b8570e6c8383b43abf83aafcc06275; diverged)  
**Scope:** Read-only source analysis and planning. No firmware code, build, tests, device flash or release performed as part of this audit. Historical hardware traces are not new results.

## Objective and external reference

Improve actual reading snappiness and heap safety on **XTEINK X4 ESP32-C3**, especially EPUB page turns, Reader entry/exit, BLE coexistence, Home/Recent, Settings and wake. Reading comes before optional BLE. Preserve **Nooir and Lite**, their fast paths, and the original X3 compatibility.

Licorice evidence: https://github.com/kycube/x4pro-emu/blob/main/docs/stock-licorice.md and https://github.com/kycube/x4pro-emu/blob/main/docs/reading-fonts.md . This is reverse engineering of stock **X4 Pro ESP32-S3** behavior in an emulator, NOT the original Licorice source or verified X4/C3 memory figures. Do not infer a portable page-cache policy from its XIC image references or add a second full framebuffer.

## ALREADY IMPLEMENTED — DO NOT REDO

1. **SD font reuse and retry guards.** lib/EpdFont/SdCardFont.cpp has keep-if-fits glyph, interval, bitmap and mini-kern buffer reuse; reusable 2 KiB codepoint workspace for page prewarm, explicit disposable release, and failed-allocation retry suppression.
2. **Compact per-page kerning.** Class lookups are loaded separately; a mini matrix for used classes avoids retaining a full ~36 KiB kerning matrix per page.
3. **Bounded font advance lookup.** buildAdvanceTableRange() replaced the old fixed 4,098-slot workspace with an input-byte-sized allocation (4,096 unique codepoint cap + reserved slots). Largest-block preflight, retry memoization and a persistent per-style 768-entry advance cache already exist.
4. **Adaptive built-in font caches.** src/util/ReaderRenderBudget.h and FontCacheManager have Normal/Constrained/Heavy budgets of 512/384/256 glyphs and 4096/3072/2048 scan bytes per font.
5. **BLE reader survival controls.** src/activities/reader/EpubReaderActivity.cpp and src/util/BleMemoryPolicy.h have render preflight, largest-request tracking, one-shot staged reclaim, BLE yielding during heavy work, 15–120s survival backoff and a **three-stop per-book readmission latch until Reader exit**. BLE-OFF must remain the ordinary non-BLE path.
6. **EPUB.** Serialized Section layout/page caches; skip loading phase for ready pages; idle next-page glyph prewarm when BLE preference is off; image warmup before font prewarm for uncached images; render-lifetime image-pixel-cache cleanup; queued-turn grayscale/AA replay deferral.
7. **Exit and durability.** EPUB exit frees Section/EPUB and multiple Reader-owned/global font/image/gray scratch owners in an ordered sequence; EPUB/XTC/CBZ progress saves skip identical positions and use atomic/recovery logic; per-store exit save profiling exists. Do not trade away data integrity.
8. **Home.** Persistent validated snapshot, targeted dynamic patches and fast waveforms, delayed (~500ms idle) snapshot writes, Lite and Nooir 3-row layout behavior, and planner stack-safety fix. RecentBooks exits release their local book vector.
9. **CBZ/XTC.** CBZ persistent per-page cache, prefetch cancellation and memory/battery gates, X4 lookahead 3 / X3 lookahead 1; XTC/XTCH streaming and metadata/cache behavior already implemented.
10. **Host coverage already exists:** test/ble_memory_policy, test/home_snapshot_policy, test/settings_persistence, test/epub_image_recovery, test/epub_page_turn_queue, test/section_cache and test/x4_shelf_perf.

## SOURCE-VERIFIED GAPS / MEASURE-FIRST CANDIDATES

### P0-A — SD font advance temporary allocation peak (strongest candidate)

Path: lib/EpdFont/SdCardFont.cpp, buildAdvanceTableRange() -> fetchAdvancesForCodepoints() -> mergeIntoAdvanceTable().

- The input-sized uint32 workspace can overlap CpIdx[cpCount] and staged AdvanceEntry[needCount], followed by a merged-table replacement while old cache still lives.
- Even though persistent advance cache has only **768 slots per style**, transient mapping and staging can be based on a much larger candidate count (up to 4,096). Worst-case illustrative *capacity* of first three allocations: ~16 KiB + ~32 KiB + ~32 KiB before other heap owners. This is **not measured device usage**; actual needCount may be smaller.
- Audit actual per-stage allocation requests, temporary overlap, total vs largest heap block, SD reads/seeks, and time. If a bottleneck is confirmed, consider remaining-cache-capacity-aware bounded work, chunked processing and/or modest reusable scratch. DO NOT add a permanent ~80 KiB scratch arena.
- collectUniqueCodepoints() deduplicates by linear scan (quadratic worst case). Benchmark before replacing; ordinary Latin pages may not benefit.
- Preserve *all* text fallback, Arabic/RTL shaping, CJK, ligatures, style and page-layout correctness. The 768-entry advance cache limit is not a limit on readable characters.

### P0-B — SD-card font prewarm budget boundary

- SdCardFont::prewarm() uses MAX_PAGE_GLYPHS=512 and has existing buffer reuse/failure handling, but its internal mapping, bitmap growth, read-order and kern/lig allocations do not uniformly participate in the same pre-allocation callback as the built-in compressed-font path.
- Investigate an actual-allocation preflight using free **and largest contiguous** heap blocks, preserving render/ble teardown ownership. Do not blindly shrink SD glyph capacity to 256; that may cause slower overflow SD reads or missing/precarious glyph coverage on CJK/Arabic.
- Scope BLE-off and BLE-connected behavior separately; an optional feature must not disrupt normal reading.

### P1-A — Confirm BLE readmission versus known heavy-book peak

- Current policies **already** have adaptive render budgets, backoff, safe one-shot return and a three-stop book latch. Do not redesign them or add a polling reconnect loop.
- Historical X4 traces: BLE can return near ~97 KiB free at idle; later font work falls to ~3.6–5.6 KiB free and triggers survival stop/replay. This may indicate quiescent admission not sufficiently accounting for next-page SD-font demand, but is only a hypothesis.
- Correlate BLEMEM / HEAPSHAPE / SDCF / ERS for light vs heavy pages; use history/hysteresis only if device evidence supports it. Do not relax global cold BLE floors or reopen repeated stop/start churn.

### P1-B — Home snapshot I/O on overlay close

- RecentBooksActivity::restoreSnapshotAfterOverlayDismissal() re-reads the validated saved framebuffer from SD (approximately 48KB for 480x800x1bpp). This may beat a redraw; **measure it** with SHELFPERF and I/O diagnostics, including Nooir and Lite.
- writeSnapshot() currently writes directly to /.crosspoint/folio_home.bin, not a temp+promotion scheme. Investigate interrupted-write robustness separately from latency. Snapshot validation and fallback already exist. No second full-screen framebuffer.

### P2 — Reader entry/exit persistence, only after profiling

- Entry updates app/recent state and loads bookmarks and clippings; exit already profiles Recent, BookState, Stats, progress and ordered release. Determine if save validation, SD flush or display transition dominates the latency before considering changes.
- Preserve backup and crash-recovery behavior. No deferring or removing essential progress durability merely to improve a benchmark.

## NEXT DEVELOPMENT CHAT — RECOMMENDED WORK ORDER

1. Confirm the 1.6.7 refinement branch/commit; default branch diverged. Read this note before suggesting changes.
2. Add narrow **diagnostics** for P0-A allocation sizes, high-water overlap, SD time and largest block; capture baseline on the actual X4 for representative content. No giant persistent buffers.
3. If measured, implement **one bounded P0-A improvement** with host tests, then compare baseline vs candidate. Otherwise do not change that path.
4. Examine P0-B SD-font prewarm admission in the same instrumented workload; preserve BLE-OFF and font correctness. Add a test for failure/retry and degraded contiguous heap.
5. Evaluate BLE readmission only after font memory is understood; preserve cooldown, book latch, stage boundaries, existing cold floors and no steady-state cache purge.
6. Profile Home overlay close, Nooir/Lite shelf selection, Settings exit, reader exit, wake and repeated books. Optimize only proven hotspots.
7. Run full C++ host tests and Python contracts, gh_release compile, then **physical X4** tests. Record pass/fail and raw numbers; do not equate source review with firmware validation.

**Required matrix:** EPUB warm and cold pages, CSS-heavy chapters, PNG/JPEG mixed images, Arabic/RTL and CJK, bold/italic/ligatures, highlights/clippings, XTC/XTCH, CBZ, BLE off/on/connected/yielded, Nooir and Lite Home, repeated Reader exits, Settings return, sleep/wake and X3 regression where feasible.

**Measure:** button-to-visible-page timing, CPU and display-refresh phases, smallest free heap, largest allocatable block, allocation failures, SD seeks/reads/flush times, page/cache hits, BLE survival stops/reconnects/replays and post-Reader-exit heap. A bigger free-heap number with slower pages is NOT a win.

## Guardrails / audit status

No proprietary Licorice firmware copied. No extra full-screen raster cache, global purge, broad renderer rewrite, Android/Windows MihonNooir edits or unrelated code changes. This document is a **documentation-only** Nov 1 audit/handover; no source compilation or hardware verification was performed.
