# Folio Nooir 1.6.2 Investigation Backlog

This is an investigation backlog, not approval to implement features. The
released `1.6.1` tag at `2c817a73` is the behavioral baseline. The current
branch tip is `7fa773d7`, a README-only update after that release. Until a
candidate is approved, preserve rendering quality, Arabic/Quran behavior,
pagination, section-cache format, book data, and SD-card data.

## Baseline signals

- `SECTION_FILE_VERSION` is `41`.
- Fresh local `gh_release`: linked firmware is `6,482,287 / 6,553,600` bytes,
  with `71,313` linked bytes remaining. The padded `firmware.bin` is
  `6,496,144` bytes, leaving `57,456` bytes in the app slot.
- PlatformIO static RAM is `53,500 / 327,680` bytes. The linker DRAM table is a
  separate measure: `120,501 / 321,296` bytes.
- Host regression coverage is `171/171` passing.
- Ubuntu built-in font headers total `1,727,246` source bytes. Generated
  hyphenation trie headers total `2,215,571` source bytes. These source totals
  are signals for investigation, not direct estimates of linked flash use.
- Physical X4 validation is recorded; physical X3 validation is not claimed.
  The WSL simulator mirror is currently dirty and is not a safe blind-sync
  target.

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

Start with the P0 regression and measurement work, then investigate the Ubuntu
font margin opportunity because it is the clearest path to recover the current
slot headroom. Do not implement a candidate that changes output, pagination,
Arabic/Quran behavior, or cache semantics until its measurements and a
separate approval are recorded.
