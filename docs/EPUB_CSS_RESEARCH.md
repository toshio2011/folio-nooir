# Folio Nooir EPUB/CSS research record

Status: durable planning reference, recorded 2026-09-22. This document is
documentation only. It does not authorize firmware or source changes.

The record combines the referenced **CrossPoint Sync URL** research thread with
a read-only audit of the Nooir source at the documentation starting tip
`90b5af34` on `codex/folio-nooir`. The authoritative checkout is
`D:\fatiha\side`; the archived C: and pre-migration WSL checkouts are outside
this work and must remain untouched.

## Decision in one paragraph

Do not replace Nooir's EPUB engine. Strengthen the good engine it already has,
one measured improvement at a time, while treating speed, memory, cache
validity, and readable fallback as part of correctness. Nooir should understand
the CSS that matters to books, not attempt to become a browser.

## Evidence markers

- **[VERIFIED — Nooir source]** observed in the checkout named above during this
  documentation pass.
- **[PROJECT BASELINE]** already recorded in `HANDOFF.md`,
  `PROJECT_CONTEXT.md`, or the 1.6.3 audit/backlog.
- **[RESEARCH — conversation]** finding from the referenced research thread;
  useful direction, but not independently rerun in this pass.
- **[REVERIFY BEFORE IMPLEMENTATION]** must be checked against the exact
  upstream revision, current Nooir code, a fixture, or hardware measurements
  before it becomes an implementation requirement.

## Goals and constraints

1. Improve EPUB fidelity for real books: cascade correctness, inherited
   typography, lists, page breaks, tables, malformed XHTML, entities, images,
   RTL/LTR mixtures, and publisher spacing.
2. Preserve the released reader foundation: Arabic/Quran shaping and fallback,
   RTL behavior, image quality, pagination, clipping/highlighting, progress and
   KOSync, XTC/XTCH, CBZ, recovery, and section-cache compatibility unless a
   separately measured change justifies an explicit cache decision.
3. Respect X3/X4 constraints: small and fragmented heaps, limited flash,
   SD latency, e-ink refresh cost, and the absence of desktop-style background
   workers.
4. Keep metadata/library browsing separate from expensive reading layout.
5. Prefer bounded work, compact representations, resumability, and graceful
   degradation. If publisher CSS is too large or broken, show the text with
   reduced styling rather than refusing to open the book.
6. Implement one isolated change at a time. Every accepted change needs a
   source/fixture explanation, flash and static-RAM delta, heap and largest
   allocatable-block evidence where relevant, and X3/X4 regression coverage.

## Current verified Nooir capabilities

### CSS properties and values

**[VERIFIED — Nooir source]** `CssStyle` currently represents text alignment,
italic/bold weight and style, underline/line-through decoration, text indent,
four margins, four paddings, image width/height, `display`, direction,
vertical-align super/sub, font size, and line height. Lengths retain units for
later resolution and include `px`, `em`, `rem`, `pt`, percentages, and unitless
values. The renderer intentionally supports a book-oriented subset rather than
the full browser property space.

**[VERIFIED — Nooir source]** CSS parsing is deliberately bounded: a 512-byte
read buffer, stack parsing buffers, a 1,500-rule limit, a 256-byte selector
limit, and a 48 KiB free-heap floor below which CSS resolution returns an empty
style. These are current safety controls, not proposed universal constants.

### Selectors and cascade

**[VERIFIED — Nooir source]** The parser accepts element selectors, class
selectors, `tag.class`, ID selectors, `tag#id`, and comma-separated groups. It
rejects descendant/child combinators, attribute selectors, pseudo selectors,
wildcards, media-query content, `@import`, and `@font-face`-style rules.

**[VERIFIED — Nooir source]** Nooir resolves a tag rule, then individual class
rules, then individual `tag.class` rules, then ID and `tag#id` rules. It accepts
multiple class names on an element, but does not represent a compound selector
such as `.chapter.centered` or `p.chapter.centered`. It also has no explicit
specificity/source-order field; the current `unordered_map` representation is
not a complete CSS cascade model.

**[VERIFIED — Nooir source]** `!important` is recognized only as a trailing
value suffix in selected value paths and stripped before interpretation. There
is no separate importance rank in the stored style, so its real cascade
semantics require a fixture before any claim of support is made.

### Inheritance, inline precedence, and style lifetime

**[VERIFIED — Nooir source]** An inline `style="..."` is parsed after the
stylesheet result and applied over it. An HTML `dir="rtl"` or `dir="ltr"`
attribute then overrides CSS direction. Direction is carried into descendants
when not redefined. Bold, italic, decoration, superscript/subscript, redaction,
typography, and accumulated block styles use bounded parser stacks and depth
tracking.

This is a strong constrained-reader foundation, but it is not a general CSS
inheritance engine. **[REVERIFY BEFORE IMPLEMENTATION]** Test each property
that is proposed for richer inheritance, especially reset behavior when a
nested element closes and the next sibling begins.

### Existing layout and resilience behavior

**[VERIFIED — Nooir source]** `display:none` is checked before tag-specific
content paths. Images can fall back to alt text when image processing fails.
Tables already have a bounded row/text path; they are not yet evidence of
browser-like grid fidelity. RTL, typography limits, EOF finalization, anchor
maps, page breaks at relevant boundaries, malformed-XML recovery, and the warm
page-turn path are part of the released baseline documented elsewhere.

**[VERIFIED — Nooir source]** The Expat-based chapter parser is resumable and
can retry an invalid-token parse with bare-ampersand recovery. Entity tables,
bounded input buffers, allocation-failure propagation, and parser teardown paths
are present. This is conservative recovery, not a promise to normalize every
malformed HTML dialect.

## Findings from other constrained readers

### Microreader

**[RESEARCH — conversation; source reference: [Microreader](https://github.com/CidVonHighwind/microreader)]**

The useful lesson is not “copy the renderer.” Microreader models a selector as
an element, one ID, and multiple required classes, calculates specificity, and
uses source order as the equal-specificity tie-breaker. Its book-oriented CSS
work also covers several items worth comparing against Nooir: page-break
properties, list suppression, floats, drop caps, small caps, text transforms,
simple borders, percentage width, margin handling, and a bounded stylesheet
cache.

Microreader also records timing around text/entity processing and element/style
processing. A particularly valuable streaming detail is retaining a partial
entity at the end of one input chunk and joining it to the next chunk before
decoding. UTF-8 boundaries need the same discipline.

**[REVERIFY BEFORE IMPLEMENTATION]** Confirm the current Microreader source,
its exact cache eviction policy, margin-collapse behavior, and whether each
feature remains suitable for Nooir's parser and font constraints.

### Current CrossPoint

**[RESEARCH — conversation; source reference: [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)]**

The current CrossPoint design was traced as cooperative incremental work inside
the reader event loop, not as a free-running desktop worker:

```text
open section -> build a small initial window -> show a page
     -> event loop gets a bounded build opportunity
     -> check free heap and largest allocatable block
     -> build a few more pages only when the reader nears its frontier
     -> yield back to input/rendering
```

The research notes describe small build chunks, a read-ahead window, partial
section persistence, incomplete-versus-valid-partial sentinels, temporary cache
files, persistent inflated XHTML, deliberate release of rebuildable font/image
state before heavy phases, and idle font prewarming. Large books reportedly
became much faster to open because the reader becomes usable before the entire
spine item is laid out.

**[REVERIFY BEFORE IMPLEMENTATION]** The exact upstream constants, source
commits, release-note performance claims, and the reported zero-allocation
`resolveStyle` path must be rechecked against the target CrossPoint revision.
The referenced discussion mentioned approximately five-second large-book
opens, roughly 10% chapter-parse improvement, roughly half cached page-load
time, 7–9% text-layout improvement, and about 12,000 avoided allocations per
page; these are research leads, not Nooir acceptance numbers.

### PapyriX

**[RESEARCH — conversation; source reference: [PapyriX Reader](https://github.com/bigbag/papyrix-reader)]**

PapyriX is useful as a record of tradeoffs. Its history combines Microreader-
influenced CSS with explicit safety limits, low-memory CSS skipping, on-demand
page-cache extension, preservation of the previous valid cache when an
extension fails, oversized-spine handling, parser no-progress guards, and
recovery from malformed markup/entities. It also explored lending framebuffer
memory during parsing.

The lesson is to preserve successful work and text even when the next styling
or layout step cannot fit. **[REVERIFY BEFORE IMPLEMENTATION]** Confirm the
current PapyriX cache/recovery code and the exact framebuffer-lending lifetime
before treating either as a Nooir design.

TenorGroup/cross and other CrossPoint-derived forks are useful comparison
points, but they are not automatically independent renderer designs.

## Nooir cache and memory architecture

### Current source/layout separation

**[VERIFIED — Nooir source]** Nooir has separate layers for source preparation,
CSS rules, and laid-out sections:

- CSS rules are persisted as `css_rules.cache` with CSS cache version `10`.
- Section layout is persisted with `SECTION_FILE_VERSION = 41`.
- Inflated XHTML is stored per book and spine item in an HTML cache independent
  of reader layout settings. A successful inflate is promoted immediately,
  before layout finishes, using a temporary file and rename.
- Font caches that can be rebuilt are released before section CSS/layout
  allocations; metadata and fallback state remain available.
- A render specification carries layout-affecting inputs such as viewport,
  font, spacing, indentation, hyphenation, embedded-style, and image behavior.
  Any new input that changes geometry must be included in cache identity or
  invalidate the layout cache.

### Incremental, partial, and resumable sections

**[VERIFIED — Nooir source]** The parser can remain alive across `parseStep()`
calls. Current reader constants are:

| Control | Current value | Purpose |
|---|---:|---|
| Initial/render build chunk | 8 pages | Make the requested page/window usable in bounded work. |
| Background build tick | 2 pages | Cooperative event-loop extension. |
| Build-ahead window | 5 pages | Stop background work when enough pages are ready. |
| Partial-extension start margin | 15 pages | Do not resume a partial chapter until the reader approaches its watermark. |
| Background free-heap floor | 32 KiB | Defer optional build ticks under total-heap pressure. |
| Background MaxAlloc floor | 16 KiB | Defer when fragmentation leaves no safe contiguous block. |
| Idle prewarm debounce | 400 ms | Avoid competing with active page turns. |

**[VERIFIED — Nooir source]** A section destructor suspends active work. A
partial cache stores valid pages and parse-watermark metadata; valid partial
pages remain readable while a replacement layout is built in a temporary
section file. Version `0` means incomplete, a derived sentinel means valid
partial, and `41` means finalized. Older firmware treats the partial sentinel
as unknown and rebuilds, which is a safe downgrade behavior.

The temporary file and late version write prevent an incomplete layout from
being accepted as complete. **[REVERIFY BEFORE IMPLEMENTATION]** The final
remove/rename window should be tested for power-loss behavior if stronger
“last-known-good cache always survives” guarantees are required; the current
source comments acknowledge a narrow swap failure window.

### Heap, fragmentation, framebuffer, and prewarming policy

**[VERIFIED — Nooir source]** Background section work gates on both
`ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()`. Idle prewarming also checks
heap/MaxAlloc floors, render ownership, framebuffer availability, and a 400 ms
quiet period. The reader has a scoped framebuffer loan around the heavy initial
section-start/inflation phase; background chunks run without that loan.

These mechanisms align with the CrossPoint/PapyriX research, but exact values
must remain hardware measurements rather than folklore. A future memory policy
may be expressed as comfortable/constrained/critical, but that is an internal
design candidate, not a current API.

## Malformed XHTML, entities, and fallback policy

The desired boundary is:

```text
bounded, conservative normalization
        -> XHTML parser
        -> CSS/style resolution
        -> layout
        -> cached pages
```

Borrow only repairs with a high-confidence intended meaning: common undeclared
entities, bare ampersands, harmless void-element/XML compatibility issues,
BOM/whitespace oddities, and known EPUB boundary cases. Do not aggressively
rewrite publisher content. If normalization or CSS fails, preserve text and
fall back to simpler styling.

The torture set must include entities split across parser chunks, UTF-8 split
at a buffer boundary, invalid XML tokens, missing/undeclared entities, void
elements, bare attributes, unexpected EOF, parser no-progress input, partial
spine items, huge paragraphs, and hidden or page-map elements immediately before
EOF. **[REVERIFY BEFORE IMPLEMENTATION]** Confirm which cases the current
Expat retry and entity code already covers before adding another normalizer.

## Performance requirements and benchmark metrics

The hard requirement is: **Nooir may become more correct, but normal reading
must remain snappy.** Do not accept a feature because it renders one fixture
better if it worsens page turns, memory floor, cache recovery, or X3 stability.

Record at least:

| Stage | Metrics |
|---|---|
| Open/metadata | Metadata time; library browsing must not invoke full layout. |
| Source preparation | ZIP/XHTML inflate time, bytes, retries, persistent HTML-cache hit rate. |
| CSS | CSS parse time, rule count, cache load/save time, resolution time, allocation count. |
| Initial layout | Pages built, elapsed time, first-readable-page time, SD reads/writes. |
| Cooperative build | Pages per tick, tick time, minimum free heap, minimum MaxAlloc, pause/resume count. |
| Cached reading | Section/page load time, page-turn latency, render time, e-ink refresh time. |
| Resource footprint | Linked flash, padded firmware, app-slot margin, static RAM, peak/free heap, largest block, cache bytes. |
| Correctness | Page geometry, text retention, anchors, RTL, glyph fallback, images, cache reopen, sleep/navigation interruption. |

**[PROJECT BASELINE]** Existing project records must remain distinct: released
1.6.2 is `6,492,279` linked bytes / `6,506,128` padded bytes / `47,472`
app-slot bytes / `53,492` static RAM; the clean 1.6.3 audit control is
`6,493,837` / `6,507,680` / `45,920` / `53,140`. A separate fresh migration
validation recorded `6,497,905` / `6,511,760` / `41,840` / `53,576` and is not
silently interchangeable with the other controls.

## EPUB torture corpus

Materialize these as small, targeted fixtures before implementation. The names
are a suggested corpus, not files added by this documentation change:

1. normal paragraphs and headings;
2. huge stylesheet;
3. 1,500+ rules and long selectors;
4. compound classes (`.chapter.centered`, `p.chapter.centered`);
5. equal-specificity source-order conflicts;
6. ID versus class/tag conflicts;
7. inline style versus stylesheet;
8. `!important` conflicts;
9. nested inheritance and 20-level style-stack unwind;
10. margin/padding leakage across sibling paragraphs;
11. relative font sizes and line heights;
12. hidden elements, boolean `hidden`, and trailing hidden content;
13. page-break before/after and TOC anchor boundaries;
14. nested unordered/ordered lists and list-style suppression;
15. giant table, colspan, captions, long words, too many columns, malformed rows;
16. floats and drop caps;
17. huge paragraph and huge single-spine item;
18. malformed XHTML, bare attributes, void tags, EOF, and no-progress input;
19. undeclared/bare/partial entities and split UTF-8;
20. huge/missing/progressive images and image alt-text fallback;
21. mixed RTL/LTR and direction inheritance/override;
22. CSS absent, CSS disabled, CSS too large, and CSS allocation failure;
23. cache interruption, sleep/navigation suspension, partial resume, downgrade,
    and stale render-spec invalidation;
24. deliberately evil combination of the above.

Every fixture needs both a correctness expectation and a resource expectation.
“Parser recognized the property” is not sufficient evidence.

## Candidate mechanisms and disposition

| Mechanism | Disposition | Nooir decision |
|---|---|---|
| Existing bounded `CssStyle`, resource-aware parsing, ID selectors, inline precedence, direction inheritance, style stacks | **KEEP** | Protect and regression-test before changing representation. |
| Microreader selector model: required class set, specificity, source order | **BORROW** | Adapt semantics; keep Nooir's compact property style and bounded storage. |
| CrossPoint cooperative section building, small chunks, read-ahead, free-heap + MaxAlloc gates | **BORROW / KEEP** | Already present in Nooir in analogous form; measure and refine rather than transplant. |
| Persistent inflated-XHTML cache independent of layout settings | **KEEP** | Already present; retain immediate promotion after successful inflate. |
| Partial/resumable/transactional section cache | **KEEP / ADAPT** | Keep sentinels and temporary files; test the final swap/power-loss window. |
| PapyriX parser no-progress guards and conservative malformed-input recovery | **BORROW / ADAPT** | Add only for a demonstrated fixture gap. |
| Partial entity retention across chunks | **BORROW / BENCHMARK** | Add only if Nooir's streaming boundary fixture fails. |
| Flat bounded selector pool instead of `unordered_map` | **BENCHMARK** | A/B parse, resolve, allocation, heap, flash, and firmware behavior first. |
| CSS cache budget/eviction | **BENCHMARK / ADAPT** | Consider only with a clear ownership and invalidation plan. |
| Page breaks and list improvements | **ADAPT** | Likely high value and low visual complexity; fixture first. |
| Margin collapsing/caps | **BENCHMARK / ADAPT** | Compare with Nooir's existing spacing behavior; avoid silent pagination drift. |
| Compact table/grid renderer with stacked fallback | **ADAPT / HIGH-RISK** | Preserve cell text; require geometry, cache, RAM, and X3 evidence. |
| Floats and drop caps | **BENCHMARK / HIGH-RISK** | Defer until correctness/resource headroom work is stable. |
| Small caps, text-transform, decorative borders | **BENCHMARK** | Later typography work; do not outrank cascade or fallback. |
| Idle page/font prewarming | **KEEP / BENCHMARK** | Existing policy is conditional; prove battery/SD/latency benefit. |
| Framebuffer lending during parsing | **BENCHMARK / HIGH-RISK** | Use scoped ownership only; reject on display corruption or lifecycle risk. |
| Full browser selectors, DOM, flexbox, grid, animations, shadows, arbitrary positioning | **REJECT** | Outside the book-reader target and resource budget. |
| Wholesale Microreader/CrossPoint/PapyriX parser replacement | **REJECT** | Nooir already has valuable low-allocation and recovery behavior. |
| Unmeasured CSS fidelity that slows page turns or loses text | **REJECT** | Readability, recovery, and responsiveness win. |

## Phased implementation order

This is a research-derived order, not permission to implement all phases in one
release.

1. **Reverify the baseline.** Pin the exact Nooir and upstream revisions;
   preserve cache version `41`; confirm current selector, entity, table, and
   partial-cache behavior with focused fixtures.
2. **Instrument and build the control matrix.** Capture the metrics above on
   normal `gh_release`, simulator X3/X4, and physical hardware where claimed.
3. **Fix cascade correctness first.** If fixtures prove the gap, add compound
   selectors, specificity, and source order in a compact bounded representation;
   then decide whether `!important` is needed by real books.
4. **Harden parser boundaries and fallback.** Add only demonstrated entity,
   malformed-XHTML, no-progress, hidden/EOF, and CSS-OFF gaps. Text must survive
   stylesheet failure.
5. **Review cache identity and failure semantics.** Confirm every geometry input,
   CSS version, temporary-file path, partial watermark, power-loss case, and
   downgrade behavior before changing the format.
6. **Benchmark memory/layout structures.** Compare the current map with any flat
   selector store; measure tables, margin behavior, CSS cache policy, framebuffer
   loan, and prewarming independently.
7. **Add high-value book layout.** Page breaks and lists first; then bounded
   tables. Floats, drop caps, and decorative typography remain later candidates.
8. **Run the full torture/regression gate.** Include Arabic/Quran, RTL/LTR,
   images, cache reopen, sleep/navigation interruption, XTC/XTCH, CBZ, KOSync,
   OTA, Font Manager, and recovery checks.
9. **Record one-variable results.** Each accepted source change gets its own
   commit, measurements, cache-version decision, and rollback criterion. Freeze
   the mature X3/X4 path before broader device ports or unrelated formats.

## References and re-verification checklist

- Nooir implementation: `lib/Epub/Epub/css/CssParser.*`, `CssStyle.h`,
  `ChapterHtmlSlimParser.*`, `Section.*`, `EpubReaderActivity.*`.
- Project controls and upstream audit: `HANDOFF.md`, `PROJECT_CONTEXT.md`,
  `docs/FOLIO_1.6.3_AUDIT_NOTES.md`, `docs/FOLIO_1.6.3_BACKLOG.md`, and
  `docs/simulator.md`.
- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
- [Microreader](https://github.com/CidVonHighwind/microreader)
- [PapyriX Reader](https://github.com/bigbag/papyrix-reader)
- [TenorGroup/cross](https://github.com/TenorGroup/cross) as a derivative
  comparison only.

Before implementation, reverify every item marked **[REVERIFY BEFORE
IMPLEMENTATION]**, refresh upstream findings at the chosen commit, and replace
conversation-level claims with source links, fixture output, or hardware
measurements. The durable conclusion remains: **better EPUB correctness,
bounded resources, readable fallback, and a snappy Nooir.**
