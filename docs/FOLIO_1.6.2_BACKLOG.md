# Folio Nooir 1.6.2 Investigation Backlog

This document tracks the remaining investigation backlog; its entries are not
approval to implement future features. The released `1.6.1` tag at
`2c817a73` is the behavioral baseline. The current committed 1.6.2 baseline
is the firmware/source milestone `85dda52a` (`feat: stabilize Nooir readers,
sync and Spine Shelf`). The fetched branch also contains the separate Quran
EPUB fixture commit `e0478714`; merge commit `5efe0695` only synchronized that
remote content and is not a new firmware baseline. Documentation commits after
that point must remain clearly separate from source/build claims. For the
remaining candidates, preserve rendering quality,
Arabic/Quran behavior,
pagination, section-cache format, book data, and SD-card data.

## Current implemented 1.6.2 work

The following work is present in the known-good milestone and does not change
the persistent cache formats:

- preferred/remembered dictionary lookup remains the fast path, with valid
  fallback lookup when the preferred folder is stale, missing, truncated, or
  otherwise unavailable;
- failed-folder diagnostics are detailed on first occurrence and deduplicated
  during the activity session without rewriting settings/history;
- alternate matching sources are discovered on demand, capped at six fixed
  records, and retain metadata only;
- source switching loads one definition body at a time and resets pagination;
- the definition header identifies preferred versus fallback results and shows
  source position when multiple matches exist;
- the former combined `48 KB` all-dictionaries definition accumulation is gone.
- Spine is an optional Recent/Finished layout with fixed bounded planning,
  deterministic book dimensions/tones/details, UTF-8-safe title handling,
  shelf pagination, shared hit rectangles, and a decoration-only plant.
- Font Manager download/install and SD-font lifecycle work are physically
  validated; manual SD-font and web-upload paths remain supported.
- KOReader Sync supports the public server default, filename and binary/
  partial-MD5 matching, and the Nooir ↔ KOReader workflow. Filename mode
  requires matching filenames on both devices; the hashing algorithm is
  unchanged.
- EPUB/reader lifecycle, memory diagnostics, image/font cleanup, and network
  cleanup are compile-gated or production-safe adaptations; normal release
  builds leave diagnostics disabled.

The batch preserves `SECTION_FILE_VERSION = 41`, `.qidx`, dictionary/settings,
EPUB/CSS, and persistent cache formats, as well as fonts, Arabic/Quran
behavior, the FreeInk SDK pin, partitions/SPIFFS, and user SD data.

## Baseline signals

- `SECTION_FILE_VERSION` is `41`.
- Released 1.6.1 reference build: linked firmware is
  `6,482,287 / 6,553,600` bytes, with `71,313` linked bytes remaining. The
  padded `firmware.bin` is `6,496,144` bytes, leaving `57,456` bytes in the
  app slot.
- Current normal `gh_release`: linked firmware is
  `6,492,279 / 6,553,600` bytes, with `61,321` linked bytes remaining. The
  padded `firmware.bin` is `6,506,128` bytes, leaving `47,472` bytes in the
  app slot. The preferred release cushion is approximately 40 KB, leaving
  `7,472` bytes above that cushion.
- PlatformIO static RAM is `53,492 / 327,680` bytes.
- The combined diagnostic profile `gh_release_diag` enables
  `NOOIR_EPUB_DIAGNOSTICS=1` and `NOOIR_KOSYNC_FONT_DIAGNOSTICS=1`; it builds
  at `6,506,717` linked bytes and `6,520,560` padded bytes, leaving `33,040`
  bytes in the app slot. It is not the production release image.
- The host suite is `211/211` passing and the focused Spine suite is `13/13`.
- WSL `simulator_x4` and `simulator_x3` validation passed and reached
  RecentBooks. Direct physical X4 validation is recorded; physical X3
  validation is not claimed. Windows simulator builds remain blocked before
  compilation when `sdl2-config` is unavailable.
- Ubuntu built-in font headers total `1,727,246` source bytes. Generated
  hyphenation trie headers total `2,215,571` source bytes. These source totals
  are signals for investigation, not direct estimates of linked flash use.
- The WSL mirror is `/home/fatiha/side-wsl` on
  `safety/wsl-cbz-before-carousel-merge-20260824`; backups from synchronization
  are outside repository content and must never be committed.

Estimates below are preliminary net firmware effects and must be measured from
the `gh_release` map/bin before implementation. With only about 57.5 KB of
padded app-slot margin, a medium-sized flash increase is already a release
risk. Any change to serialized page geometry, shaping/fallback contracts, or
cache semantics must be treated as a cache review and may require a version
change; this baseline does not change the cache format.

## LOW RISK

| Candidate | Expected user benefit | Flash saving/cost | RAM impact | X3/X4 risk | Cache/version implications | Priority |
|---|---|---:|---:|---|---|---|
| Release-size, heap, and page-readiness measurement guardrails | Makes future investigations repeatable and catches slot/heap regressions early. | 0 to +4 KB firmware; tooling/test-only cost preferred. | 0 to +1 KB if release-disabled. | Low; keep diagnostics gated off. | None. | P0 |
| Extend the focused EPUB regression corpus | Protects EOF content, trailing structures, malformed XHTML, mixed Arabic/Latin blocks, and fallback combining marks already released in 1.6.1. | 0 KB firmware; test-only. | None. | Low; host coverage first. | None. | P0 |
| Ubuntu built-in font flash-size audit and safe trimming | Recovers headroom for bug fixes and future EPUB work without reducing visible quality. | Preliminary target: save 50–200 KB; actual linked contribution must be measured. | 0 KB expected. | Low if glyph coverage, metrics, and fallback order are preserved. | No change if IDs/metrics remain stable; otherwise verify persisted font selection and affected section caches. | P1 |
| X3/X4 validation matrix for released EPUB paths | Converts the remaining X3 uncertainty into device evidence for EOF, Arabic, memory pressure, and page turns. | 0 KB. | None. | Low implementation risk, but direct X3 hardware risk is currently unverified. | None. | P1 |

## MEDIUM RISK

| Candidate | Expected user benefit | Flash saving/cost | RAM impact | X3/X4 risk | Cache/version implications | Priority |
|---|---|---:|---:|---|---|---|
| Hyphenation flash-usage audit and reduction | Preserves app-slot margin while retaining language-aware line breaking. | Preliminary target: save 100–500 KB through verified language pruning, deduplication, or representation changes; generated trie headers currently total 2.22 MB. | 0 to +8 KB depending on lookup/lazy-loading design. | Medium: flash latency, language coverage, and X3 memory need measurement. | No version change if break opportunities stay identical; changed line breaks require cache invalidation and possibly a version/contract update. | P1 |
| Further EPUB responsiveness | Improves first-page readiness, page turns, SD access, and warm-path behavior on large or image-heavy books. | +5–30 KB likely. | 0 to +8 KB temporary/work-queue budget. | Medium, especially around X3 SD and watchdog timing; lower on X4. | Prefer none; page content/order changes require cache review. | P2 |
| EPUB memory and fragmentation resilience | Reduces long-book failures, allocation fragmentation, and recovery risk without changing visible output. | +5–40 KB likely for bounded allocators/reuse and diagnostics. | Target 0 to −8 KB peak; temporary buffers must remain bounded. | Medium on X3; low-to-medium on X4. | None if allocation strategy is internal and serialized data is unchanged. | P2 |
| Remaining non-Arabic EPUB typography/layout improvements | Improves CSS fidelity for margins, lists, indentation, line-height, punctuation, and uncommon block structures. | +5–50 KB likely. | 0 to +8 KB. | Medium; layout complexity and page-fit changes need X3 testing. | Geometry changes invalidate existing section pages; do not implement without an explicit cache-version decision. | P2 |

## HIGH RISK

| Candidate | Expected user benefit | Flash saving/cost | RAM impact | X3/X4 risk | Cache/version implications | Priority |
|---|---|---:|---:|---|---|---|
| Broad Arabic/Quran typography or shaping changes | Better edge-case fidelity for scripts and marks not covered by the 1.6.1 release. | +10–80 KB likely. | +4–20 KB possible. | High on X3; Arabic/Quran behavior is explicitly frozen for this baseline. | Shaping/fallback contracts and pagination may change; likely cache invalidation/version work. | P3 / defer |
| CBZ/Manga preparation and cache architecture | Enables the documented future manga workflow and stronger archive/page caching. | +30–150 KB likely. | +8–32 KB transient or cached state. | High: storage, SD, cache, and X3 memory interactions. | New cache identities/format decisions are expected; follow `docs/CBZ_MANGA_PLAN.md` audit first. | P4 / defer |
| PDF and FB2 readers | Adds two currently unimplemented document families. | +100–500 KB or more. | +16–64 KB or more. | High; broad parser/rendering scope and X3 constraints. | New reader/cache paths and compatibility contracts. | P5 / defer |

## Ordering rule

For remaining work, start with the P0 regression and measurement work, then investigate the Ubuntu
font margin opportunity because it is the clearest path to recover the current
slot headroom. Do not implement a candidate that changes output, pagination,
Arabic/Quran behavior, or cache semantics until its measurements and a
separate approval are recorded.
