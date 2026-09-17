# Folio Nooir 1.6.3 Investigation Backlog

This is the actionable post-1.6.2 investigation queue. It is **not approval to implement every item in 1.6.3**. Folio Nooir 1.6.2 is released; tag `1.6.2` resolves to `25df494020874150a047e2a4b3be62c3e40151e8`. The known-good firmware/source milestone is `85dda52a102163b40fd4a2ddfde65e6cdc23af36`.

## Immutable comparison baseline

Normal `gh_release` for the released 1.6.2 source baseline:
- linked flash: **6,492,279 / 6,553,600 B**
- padded `firmware.bin`: **6,506,128 B**
- app-slot margin: **47,472 B**
- static RAM: **53,492 / 327,680 B**
- preferred production app-slot cushion: about **40 KB**
- `SECTION_FILE_VERSION = 41`
- host tests: **211/211**
- Spine tests: **13/13**
- WSL `simulator_x4` and `simulator_x3`: pass to RecentBooks
- physical X4: passed the 1.6.2 validation/torture cycle
- physical X3: **not claimed**
- Font Manager install: physically confirmed
- Nooir ↔ real KOReader KOSync: physically confirmed

Do not change the partition layout to create apparent headroom. Every production source change must be compared with this baseline using a normal `gh_release` build.

## Upstream audit policy

Primary references:
- CrossPoint Reader: https://github.com/crosspoint-reader/crosspoint-reader
- CrossInk: https://github.com/uxjulia/CrossInk
- InkPointX: https://github.com/yokki-vans/InkPointX
- CrossPDF, PDF architecture only: https://github.com/davemessew/CrossPDF
- CrossLink, Bluetooth/X4 reference: https://github.com/DaisonChun/crosslink

For each upstream candidate record **TAKE NOW / INVESTIGATE / LATER / SKIP / ALREADY COVERED**, source commit/PR, Nooir equivalent if any, firmware delta, static-RAM delta, peak free-heap/largest-block effect where relevant, X3/X4 risk, and required tests. Never wholesale-merge upstream.

The September 2026 audit is the starting point for 1.6.3. Re-run a delta audit during every Nooir release cycle so future work resumes from the last inspected upstream state instead of starting from memory.

## P0 — flash recovery first

Generate a linker/map-level breakdown before adding another large subsystem. Audit:
- compiled-in themes and whether non-default themes can move to SD;
- bundled/fallback fonts and CrossInk-style downloadable/font trimming opportunities;
- icons, bitmaps and other compiled assets;
- inherited unused activities/features;
- translations and generated web assets;
- duplicate theme/rendering paths;
- dead linked functionality and build flags/LTO/garbage collection;
- generated hyphenation data and safe language/representation reductions;
- resources that can safely move to SD without harming boot/recovery.

Keep Folio Nooir built in unless an SD-loaded default is separately proven safe. Investigation target: recover meaningful headroom, preferably **100–200 KB or more**, but do not claim savings until measured.

## P1 — EPUB/font/image/memory delta

Re-diff current Nooir against current CrossPoint/CrossInk. Revisit:
- CrossPoint #3521 font-cache fragmentation;
- CrossPoint #3501 SD/SPI batching;
- CrossPoint #3398 / `9d2f234` packed Font Manager catalog, only if diagnostics justify it;
- `3555ff5` image-related fragmentation;
- `06b5d5b` SD-font ligature-view cleanup;
- `f4b4ff0` partial font-cache space-width recovery;
- CrossInk deferred SD-font discovery;
- debounced progress writes;
- streaming EPUB table rendering;
- framebuffer lending during chapter indexing;
- cancellable section pre-indexing;
- dictionary low-heap guards;
- overlay image/font-cache cleanup;
- XTCH memory fixes.

Do not port work already covered by 1.6.2. Preserve pagination, Arabic/Quran behavior, cache compatibility, image quality and working KOSync unless a separately measured fix requires otherwise.

## P1 — SD/SPI benchmark

Benchmark #3501-style SD/SPI batching on physical X4. Exercise EPUB, CBZ, XTC/XTCH, covers, metadata, dictionaries, SD fonts, sleep images and caches. Record latency plus heap/largest-block behavior. Treat it as a candidate, not an automatic port.

## P2 — XTC / XTCH

Keep Nooir's streaming/retained B/W-plane architecture. Audit CrossPoint/CrossInk for concrete chapter-listing, token scanning, settings, cache, memory and SD-I/O fixes. Prefer small isolated adaptations.

## P2 — Quick Actions

After flash/memory stabilization, consider the already-audited CrossInk model: one configurable trigger, five persisted action slots, shared popup, context-aware filtering and existing Nooir dispatchers. Measure flash cost before merging. Full UI/System Dark Mode is a separate larger project.

## P2 — FB2 prototype

Audit InkPointX's current FB2 implementation. Prefer normalization into Nooir's existing reflow/chapter/layout machinery rather than a second full reader. Reuse typography, images, progress, bookmarks, statistics, dictionary/clipping and existing lifecycle/cache infrastructure where safe. Prototype separately and measure flash/RAM before production integration.

## P3 — OPDS

First source-audit Nooir's current inherited OPDS status because documentation historically references CrossPoint OPDS while the 1.6.3 audit treats richer OPDS as a future candidate. Do not advertise or redesign it until current source behavior is reconciled.

Then compare CrossPoint and InkPointX for saved servers, search, pagination, authentication, direct download, cancellation, XML parsing, persistence and library refresh. Prefer the smallest useful delta and measure linked flash plus peak TLS/parser heap.

## P3 — PDF experiment

PDF is not a released Nooir reader. Compare separately:
1. InkPointX fixed-layout/raster/zoom.
2. CrossPDF-style text/reflow with prepared data cached on SD.

For Nooir's novel use case, investigate reflow/one-time SD preparation first. Keep fixed-layout raster PDF separate. Do not promise scanned/image-heavy, complex-layout, forms or arbitrary PDF compatibility. Measure parser/raster flash, preparation peak heap/largest block, SD cache size, page latency and failure cleanup.

## P3 — Bluetooth page turner experiment

Do not merge a full BLE stack into normal firmware while 1.6.2 has only 47,472 B app-slot margin. Audit current CrossPoint Bluetooth/page-turner work, CrossInk where relevant and CrossLink because CrossLink physically worked on X4 but showed disconnect behavior.

Start with a separate experimental build/profile. Measure:
- exact firmware increase;
- static and idle RAM;
- connection-time free heap/largest block;
- pairing/reconnect/disconnect behavior;
- chapter-indexing interaction;
- reader input ownership;
- sleep/wake and battery impact;
- physical X4 reliability.

Only then decide whether Bluetooth can fit normal Nooir.

## Later / evidence-triggered

- CrossPoint packed Font Manager arena if Font Manager allocation pressure returns.
- Additional image/dither fragmentation work only if physical traces show a remaining problem.
- KOSync redesign only for a concrete interoperability/protocol fix; current Nooir ↔ KOReader behavior is working.
- Full UI/System Dark Mode as a dedicated palette/architecture project; Reader Dark Mode already exists.
- X4 Pro support/simulator only with an explicit hardware/architecture plan; no X4 Pro simulator currently exists.
- Broader Arabic/Quran shaping changes only with a specific bug and dedicated regression evidence.

## Recommended 1.6.3 sequence

**Flash map/recovery → EPUB/font/image/memory upstream delta → SD/SPI benchmark → small safe fixes → Quick Actions if headroom allows → FB2 prototype → OPDS reconciliation/extension → PDF experiment → Bluetooth experiment.**

This sequence is a priority guide, not a promise that all items ship in 1.6.3.

## Non-negotiable regression boundaries

Preserve unless a separately approved task explicitly changes them:
- partition layout and recovery path;
- `SECTION_FILE_VERSION = 41` and compatible persisted data;
- Arabic/Quran rendering behavior;
- KOSync interoperability;
- XTC/XTCH current working architecture;
- user SD data;
- FreeInk exact tested pin;
- physical X4 stability.

Physical X3 evidence remains outstanding and must not be inferred from simulator success.
