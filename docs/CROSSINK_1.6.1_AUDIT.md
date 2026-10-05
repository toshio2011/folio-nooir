# CrossInk 1.6.1 candidate reconciliation — current checkout

**Updated:** 2026-10-05

**Target:** Folio Nooir 1.6.3 RC2.3
**Scope:** narrow reconciliation of the CrossInk candidates named in the
current Nooir backlog and handoff.

The previously referenced `CROSSINK_1.6.1_AUDIT.md` was not present in this
checkout or the searched project documentation locations. This is a new
source-based reconciliation, not a recovered copy of that missing audit and
not a claim that the upstream 1.6.1 source diff was re-audited. Candidate
identifiers are retained only where they already appear in Nooir's notes.
No source change is authorized by this document.

## Candidate dispositions

| Candidate | Classification | Current Nooir evidence | Decision |
|---|---|---|---|
| SD-font space-width fallback | ✅ Already solved | `GfxRenderer::getSpaceWidth()` and `getSpaceAdvance()` use the SD advance fast path only for a nonzero space metric; a missing space entry falls back through the registered `EpdFont` glyph-miss path. `test/gfx_space_advance/GfxSpaceAdvanceTest.cpp` covers zero cached metric fallback. | Do not port or duplicate. Preserve the fixed-point space-advance tests. |
| Empty CSS span / inline spacing (noted as CrossInk #748) | 🟡 Useful defer | The current chapter parser handles inline spans in the shared text stream, carries inline style state, and reuses empty text blocks; the CSS cascade rejects empty selectors and has `display:none` coverage. The exact upstream #748 patch and a matching failing fixture are unavailable. | Do not claim semantic equivalence from the issue title. Revisit only with the exact upstream diff and a focused EPUB fixture proving a remaining defect. |
| KOSync oversized progress response bound | 🟡 Useful defer | `KOReaderSyncClient::getProgress()` currently calls `SecureHttpClient::getString()` and parses the resulting body without a Nooir-level byte cap. This is a real unbounded-response surface, but adding a cap safely requires checking the streaming/client body path and compatibility with extended `position` metadata. | Keep as a concrete hardening candidate; implement separately with bounded streaming and tests for ordinary, extended, malformed, and oversized responses. Not part of the BLE/memory patch. |
| Global-vs-per-book reader setting application | ✅ Current behavior is global | Reader typography/render settings are read from global `SETTINGS` and applied to the active Reader; the current user-facing settings flow does not select a per-book settings profile. | Treat global behavior as current product semantics. Do not introduce per-book persistence or silently alter existing books. |
| Other parser/cache microfixes attributed to the missing audit | 🟡 Useful defer | The exact candidate list, upstream commit IDs, and expected fixtures were not available in the missing document. Existing Nooir parser/cache code already contains focused regression tests and transactional CSS-cache handling. | No blind patch. Recover the exact source reference and add a failing regression fixture before considering a port. |
| Memory-aware EPUB layout classification | 🟡 Audit/notes only | `CssParser::resolveStyle()` has a low-heap graceful fallback that returns default styling below its existing threshold; Section build work is coordinated with BLE lifecycle and finalized Section caches remain the navigation source. | Preserve readable degradation and current indexing policy. Do not add upstream layout classifications without measured X4/X3 evidence. |
| LibraryIndex | 🟡 Audit/notes only | No LibraryIndex implementation change is required by the physical RC2.3 findings; the available request was to retain this as a source-audit note. | Keep outside this bounded source pass. Record a separately scoped audit if a measured library/indexing defect appears. |

## Preservation notes

- No CrossInk code was ported in this reconciliation.
- `SECTION_FILE_VERSION=41`, `JD_FASTDECODE=0`, EPUB pagination, XTC/XTCH,
  Arabic/Quran behavior, image quality, and existing KOSync wire semantics
  remain release constraints.
- The missing historic audit must not be represented as recovered evidence.
