# Theme flash analysis — Folio Nooir 1.6.0

Status: static/source analysis only. No theme was removed, no default was changed, and no permanent source or build configuration change was made for this investigation.

## Baseline

- Branch: `feat/cbz-persistent-read-ahead`
- HEAD: `1feb5ae1ccae738dd6a5620d2b0280cef7020e8d`
- Active development version: `1.6.0` in [`platformio.ini`](../platformio.ini:7)
- Existing default artifact: `.pio/build/default/firmware.bin`, 5,948,928 bytes
- Active OTA application slot: `0x640000` = 6,553,600 bytes in [`partitions.csv`](../partitions.csv:4-5)
- Used: 90.77%; remaining headroom: 604,672 bytes

The artifact, ELF, object files, and linker map already present in `.pio/build/default` were used as the measurement baseline. A fresh build was not pursued after PlatformIO hit the existing Windows permission/encoding problems; this task did not require toolchain troubleshooting.

## Theme architecture

The five selectable themes are registered in [`CrossPointSettings.h`](../src/CrossPointSettings.h:223-224), exposed by [`SettingsList.h`](../src/SettingsList.h:174-176), and constructed by the switch in [`UITheme.cpp`](../src/components/UITheme.cpp:31-57).

- `BaseTheme` is shared implementation for Classic and is also the base of Lyra and RoundedRaff. Its code therefore remains when any of those derived themes remain.
- `Lyra3CoversTheme` derives from `LyraTheme` and adds one cover-rendering override.
- `FolioNooirTheme` derives from `LyraTheme`; it is also referenced directly by `FolioLibraryActivity` and `RecentBooksActivity`. It is not an independently detachable visual-only module.
- No theme directory contains a separate bitmap, font, or other theme-only binary asset. The themes use shared renderer, font, icon, and translation infrastructure.
- The five theme labels are present across 31 translation YAML files. Removing only a constructor/switch case would not remove those generated translation entries.

The linker map shows fine-grained per-function sections and a `Discarded input sections` area, so unused functions are already eligible for garbage collection. For example, Folio Nooir’s currently unused `drawCoverProgress` and `drawLibrarySummary` functions appear in discarded sections rather than the linked image.

## Linked-size evidence and estimates

The following linked symbol spans were measured from `firmware.elf`. They include each class’s surviving methods, vtable, and (where applicable) metrics object. They are an upper-bound indicator for a theme-only removal, not a promised A/B delta: shared helpers, switch code, strings, alignment, and inherited methods are accounted for separately in the ranges.

| Theme | Linked class span | Estimated removable flash | Confidence | Interpretation |
|---|---:|---:|---|---|
| Classic | 9,774 B | 0.1–0.5 KB | High | `BaseTheme` remains needed by the other themes, so removing Classic mainly removes its selector path and small construction/logging code. |
| Lyra | 5,670 B | 0.5–1.5 KB alone; roughly 9–15 KB only with the Lyra-derived family | Medium | Lyra’s implementation remains needed by Lyra 3 Covers and Folio Nooir. Removing it alone is not a valid simple exclusion. |
| Lyra 3 Covers | 1,384 B | 1.5–2.5 KB | High | One isolated override, vtable, metrics object, and selector path; Lyra/Base code remains shared. |
| RoundedRaff | 4,394 B | 4.5–7 KB | High | The largest straightforward standalone theme candidate; its BaseTheme dependency remains shared. |
| Folio Nooir | 1,920 B | 1.5–2.5 KB as theme-only code | Medium | Its class body is small, but direct casts and shelf calls in the home activities mean removal would require behavior/code-path changes. |

For reference, the raw compiled object files are much larger (`.o` files range from roughly 0.5–1.1 MB) because they contain unlinked sections, compiler metadata, and COMDAT/template material. Those raw object sizes are not flash savings.

The full Lyra-derived family is the only potentially larger grouping: Lyra, Lyra 3 Covers, and Folio Nooir together account for about 9 KB of surviving class/vtable/metrics spans before switch and alignment effects. Any additional recovery from removing the bookshelf activities would be feature cleanup, not a theme-only saving, and would change behavior.

## Decision

No existing theme is likely to be a decisive application-partition strategy for Carousel + FB2 + bounded PDF. RoundedRaff is the best standalone candidate for a later precise A/B build, but its likely saving is only about 0.7–1.1% of the 604,672-byte current headroom. Removing the whole Lyra-derived family could plausibly recover around 9–15 KB of theme-related flash, still only about 1.5–2.5% of current headroom and at materially larger UI/feature scope.

Therefore:

1. Do not remove Folio Nooir solely for flash size.
2. Do not plan Carousel/PDF/FB2 around theme removal; the likely savings are too small and the exact converter costs remain the dominant unknowns.
3. If precise follow-up measurement is desired later, prioritize RoundedRaff first, then a separately scoped Lyra + Lyra 3 Covers + Folio family experiment. No theme appeared unusually large enough to justify broad A/B-build work now.
