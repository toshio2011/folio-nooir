# Folio Nooir Handoff

Read [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md) for the complete project
history, decisions, and feature inventory.

## Current position

- Active development line: Folio Nooir **1.6.2** investigation
- Known-good baseline: released Folio Nooir **1.6.1**
- Authoritative branch: `codex/folio-nooir`
- 1.6.1 source checkpoint before release preparation: `c13eda8c490b53c0d787d641e144b4e1d332478b`
- Published 1.6.1 tag/release commit: `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Current fetched branch tip: `7fa773d73457a225dc99cb0e19da76af824b2da5`, a README-only
  update on top of the 1.6.1 release commit; local HEAD matches
  `origin/codex/folio-nooir`.
- FreeInk is a real Nooir dependency through the `freeink-sdk` submodule.
- The 1.5.10 baseline uses the Nooir-specific FreeInk commit
  `958720659ea289ae325e83db20049d0ea844800d` (`9587206`).
- The completed 1.6.0 source, translations, and feature inventory are
  integrated into `codex/folio-nooir` at merge `0f1bd556`; the branch also
  contains the dedicated 1.6.0 README update at `b12f2732` and the latest
  README-only remote update at `ccddded2`.
- The previous safety checkpoint was `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`, which is an ancestor of the current development branch.
- The normal/default firmware build has succeeded. Carousel/HQ cover behavior
  and the Statistics/Sleep work have been physically exercised on X4. The
  1.6.1 release is complete; 1.6.2 is currently a baseline and investigation
  pass, with no feature implementation authorized yet.
- The existing CBZ reader and cache behavior listed below are 1.6.0 baseline
  functionality.
- The completed 1.6.1 focus was EPUB reading quality, especially Arabic/RTL
  support, text shaping, fonts, and layout. The Arabic/EPUB foundation,
  typography work, EOF finalization, glyph-bound compensation, and warm-turn
  path are included in the release candidate being frozen below.
- The detailed future CBZ/Manga plan is preserved in
  `docs/CBZ_MANGA_PLAN.md` and is deferred until after the EPUB phase.
- CBZ/Manga preparation and cache improvements remain a future planned phase,
  deferred until after the EPUB phase. Their detailed architecture and design
  must still be planned and audited separately before implementation.

## 1.6.1 released baseline

- The release retains the completed 1.6.0 baseline and the planned
  CBZ/Manga work remains future work in `docs/CBZ_MANGA_PLAN.md`.
- The 1.6.1 EPUB work includes Arabic fallback, shaping/RTL integration,
  Quranic-mark bounds compensation, malformed-XML recovery, generic block
  flow, bounded CSS typography, EOF/page finalization, and the warm page-turn
  fast path. Section cache version is `41`, with shaping/fallback contract
  discriminators retained.
- Temporary physical missing-glyph and EPUB memory-pressure diagnostics were
  used during investigation and have been removed from the release source.
  The functional fallback, font grouping, and rendering fixes remain.
- Physical X4 validation covered the EPUB fixes and Arabic/Quran reading paths.
  Physical X3 validation is not claimed here; the shared X3/X4 code paths and
  simulator validation remain useful but do not replace that hardware check.
- A fresh local `gh_release` build is verified from this branch: linked flash
  is `6,482,287 / 6,553,600` bytes, leaving `71,313` linked bytes; the padded
  `firmware.bin` is `6,496,144` bytes, leaving `57,456` bytes. PlatformIO
  static RAM is `53,500 / 327,680` bytes. These are baseline measurements,
  not a release-publication or flashing action.
- The host regression suite is `171/171` passing. Physical X4 validation is
  recorded; physical X3 validation is not claimed. The separate WSL
  simulator mirror is dirty and must not be synchronized or reset blindly.
- Existing ignored build outputs must not be treated as release assets unless
  their source/configuration is positively verified.

## Completed work

- Direct CBZ reader with ComicInfo metadata, covers, thumbnails, bounded page
  indexing/extraction, Fit Width/Fit Page/Landscape/Zoom, page picker,
  RTL/LTR navigation, bookmarks, persistent cache replay, queued navigation,
  X4 three-page read-ahead, and conservative X3 read-ahead.
- CBZ read-ahead uses the same cache across Fit Width, Landscape, and Zoom;
  it shows reserved-area progress and a Next-ready state, yields to foreground
  navigation, and cancels/queues actions instead of silently blocking input.
- CBZ Reset Progress removes `progress.bin`; per-book Clear Reading Cache
  removes generated reader pages without removing progress; global Clear
  Reading Data retains its broader metadata/statistics cleanup behavior.
- Web transfer can optionally normalize progressive CBZ JPEGs to baseline
  JPEGs; baseline JPEGs and PNGs are not re-encoded.
- EPUB bounded JPEG/PNG handling, large/progressive-image safeguards, safe
  PixelCache behavior, and shared long-operation loading feedback.
- EPUB clipping/highlight restoration preserving regular, bold, and mixed
  styles through save/load and reflow.
- Library/Recent metadata behavior fixes, XTC exit/cover preservation, and
  KOReader Sync 2xx handling.
- Web To-Do editing avoids refresh collisions and duplicate saves; Clock &
  Weather sync guards duplicate requests, feeds the watchdog during slow
  network work, and temporary station-Wi-Fi loss no longer ends web mode.
- Power-lock transitions avoid redundant active requests while retaining the
  existing sleep and deep-sleep paths.
- Native X3/X4 simulator support and documentation are available through the
  shared source and `simulator_x3`/`simulator_x4` environments.
- The independent Carousel theme supports persisted 3-Cover and 5-Cover
  layouts, circular navigation, small-collection handling, screen-derived
  trapezoidal cover geometry, opaque far-to-near rendering, and the graphical
  Library path.
- Folio Nooir Recent and Finished each support independent `3 Covers`,
  `4x2 Grid`, `3-Cover Carousel`, and `5-Cover Carousel` layouts. Folio
  Carousel rendering remains inside the shelf region; the graphical Folio
  Library remains unchanged.
- Carousel centers and Statistics Sleep screens prefer an explicitly prepared
  valid `thumb_360.bmp` and immediately fall back to `thumb_220.bmp`. Side
  covers, Folio shelf cards, and Statistics Books use the existing 220px
  source. Navigation and sleep never synchronously generate HQ covers.
- Carousel source reuse keeps a six-handle cache, protects only the current
  frame's source paths from LRU eviction, caches unavailable-HQ probes during
  the activity session, and uses shared perspective-rendering fast paths.
- Reading Statistics provides Overview, Calendar, Books, and Achievements
  tabs, persistent time/session/page data, bounded 730-day history, streaks,
  averages, circular navigation, and twenty derived achievements. Reading
  Stats Sleep and Minimal Stats Sleep use bounded cached-cover layouts, while
  legacy Cover, Overlay, and Clipping sleep modes retain their full-screen
  rendering behavior.
- Settings persist the independent Carousel layout and Folio Recent/Finished
  layouts separately, with migration/default handling and translations added
  through the normal i18n source workflow.
- PDF and FB2 readers are not implemented; the repository contains feasibility
  documentation only.

## FreeInk dependency and publishing state

The nested `freeink-sdk` contains the only Nooir-specific SDK change used by
the 1.5.10 baseline:

```text
JD_FASTDECODE 1 -> 0
local commit: 958720659ea289ae325e83db20049d0ea844800d
local branch: nooir-1.5.10-tjpgd
```

This selects TJpgDec's portable Huffman path, which was needed to avoid the
grayscale artifacts seen with the fast path. FreeInk must therefore be
fetchable for anyone cloning Nooir; the parent must pin this exact tested
commit rather than track a moving branch.

The nested checkout is now configured conventionally:

```text
origin   https://github.com/toshio2011/freeink-sdk.git       (user fork)
upstream https://github.com/Free-Ink/freeink-sdk.git         (official)
branch   nooir-1.5.10-tjpgd
```

The fork was created under `toshio2011`, and branch `nooir-1.5.10-tjpgd` was
pushed successfully. Remote verification resolves it to
`958720659ea289ae325e83db20049d0ea844800d`. The parent tree points
`.gitmodules` at the fork and pins the exact tested `9587206` submodule
commit. The pin is committed in the authoritative history.

## Immediate next steps

1. Keep the 1.6.1 release behavior frozen while reviewing the focused 1.6.2
   EPUB regression coverage added for 1.6.2; do not change production behavior
   until an investigation task is approved.
2. Continue physical X3 regression validation of the released shared paths
   when hardware access is available; X4 evidence does not substitute for it.
3. If simulator validation is needed, first inspect the separate dirty WSL
   mirror and preserve its unique work; do not synchronize or reset it blindly.
4. To sync FreeInk later, fetch `upstream`, merge or rebase deliberately,
   test Nooir, push the tested branch to `origin`, and only then update the
   parent pointer to a new exact commit. Avoid a blind `git pull` inside
   `freeink-sdk`.
5. Review `docs/FOLIO_1.6.2_BACKLOG.md` and approve one investigation at a
   time. Keep cache format, pagination, rendering quality, and Arabic/Quran
   behavior unchanged unless a later task explicitly authorizes otherwise.
6. After the EPUB phase, return to the preserved CBZ/Manga preparation and
   cache plan and complete its separate architecture/safety audit before
   implementation.
7. Review any future post-1.6.2 source change with the X3 safety, cache, storage,
   and format-isolation constraints below before implementation.
8. Create or upload release artifacts only from a positively identified,
   appropriately built image; never use an arbitrary ignored `firmware.bin`.

## Resource map

- Repository: <https://github.com/toshio2011/folio-nooir>
- Authoritative development branch: `codex/folio-nooir`
- 1.6.1 release tag commit: `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Current 1.6.2 branch tip: `7fa773d73457a225dc99cb0e19da76af824b2da5`
- 1.6.1 source checkpoint before release preparation: `c13eda8c490b53c0d787d641e144b4e1d332478b`
- Previous 1.6.0 safety checkpoint: `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`
- WSL simulator mirror: `~/side-wsl`
- Simulator instructions: `docs/simulator.md`
- Cache/format reference: `docs/file-formats.md`
- Nested SDK: `freeink-sdk/`
- SDK fork target: <https://github.com/toshio2011/freeink-sdk>
- SDK official upstream: <https://github.com/Free-Ink/freeink-sdk>
- CrossPoint reference: <https://github.com/crosspoint-reader/crosspoint-reader>
- CrossInk reference: <https://github.com/uxjulia/CrossInk>
- CrossLink reference: <https://github.com/DaisonChun/crosslink>
- vCodex/Codex reference: <https://github.com/marcoand75/cpr-vcodex-steroids>
- Flowe OS reference: <https://github.com/andrewjiang/flowe-OS>
- Xteink X3 source/releases:
  <https://gitee.com/daixinchun/xteink-x3/releases#release-v20260617>

Local EPUB/CBZ test files are external fixtures, not project resources; do
not stage or copy them into the repository.

## Working-tree rules

- Preserve the committed `freeink-sdk` pin at the verified fork commit.
- Keep `origin` pointed at the user fork and `upstream` pointed at official
  FreeInk. Never push Nooir work to `Free-Ink/freeink-sdk`.
- Keep the parent submodule at one exact tested SDK commit; do not use
  `git pull` blindly inside the submodule.
- Leave `.codex-*`, `_epub-inspect*`, `codex-work-monitor/`, logs, probes,
  caches, binaries, and build output untouched and unstaged.
- Preserve the released 1.6.1 Arabic/EPUB fixes and keep future diagnostics,
  probes, logs, caches, and arbitrary build outputs out of release commits.
- Treat the existing 1.6.0 CBZ reader as baseline functionality. CBZ/Manga
  preparation and cache work is deferred until after the EPUB phase and its
  separate planning and architecture audit.
- Use explicit Git staging; never use `git add -A`, reset, checkout, or
  force-push for this handoff. Keep documentation updates separate from
  source changes when preparing the next checkpoint.
