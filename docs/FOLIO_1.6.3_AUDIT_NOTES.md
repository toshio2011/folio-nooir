# Folio Nooir 1.6.3 Audit Notes

Research-only notes for the 1.6.3 development line. These findings are intended to make the next Codex implementation session start from verified questions instead of rediscovering context. No production code change is authorized by this document.

## 2026-09-18 — flash/headroom source audit

### Confirmed source findings

1. **Built-in fonts are a primary measurement target, not yet a proven saving.**
   `lib/EpdFont/builtinFonts/` contains generated built-in font payloads for Arabic, Noto Sans, Noto Serif and Ubuntu across multiple sizes/styles. Individual generated headers are large, but source/header size must never be reported as firmware saving. Only the linked `gh_release` ELF/map and resulting `firmware.bin` delta count.
2. **Do not remove Arabic/Quran fallback blindly.**
   Arabic/Quran rendering is a released, physically validated compatibility boundary. Any font experiment must first identify which built-in faces/sizes are actually linked and which paths require them. Preserve fallback and shaping behavior.
3. **Translation trimming already has infrastructure.**
   The repository's i18n generation path includes a `--strip-unused` mechanism. This is a low-risk measurement candidate before deleting languages or changing user-visible translation coverage. Verify exactly how the release build invokes generation before enabling/changing anything.
4. **OPDS is not a greenfield feature.**
   The repository already contains `lib/OpdsParser/OpdsParser.cpp/.h`. The parser includes bounded feed parsing concepts such as navigation/acquisition entries, search-template handling and pagination links. The next task is to trace whether the activity/UI/network/download/persistence path is currently wired and reachable, then classify each missing piece. Do not describe OPDS as simply absent until that reconciliation is complete.
5. **Bluetooth also has inherited scaffolding.**
   `src/BleInput.cpp/.h` exists and wraps FreeInk BLE HID-host behavior, including teardown/release behavior. The normal production path still needs to be traced to determine compile/link reachability and what FreeInk capabilities are actually enabled. Bluetooth remains experimental: do not enable it in normal firmware before exact flash/RAM/heap measurements.
6. **Existing build optimization must be inventoried before proposing duplicates.**
   Nooir already contains selective/disabled component choices and size-oriented configuration. The linker/map audit should distinguish code that exists in the tree from code that is actually linked into `gh_release`.

### Important interpretation

The current flash-recovery hypothesis is **font/generated-data/link reachability first**, but nothing has yet proved how many bytes can be recovered. The goal of the next build-capable session is measurement, not deletion.

## Work that can continue without Codex/local builds

Research can continue safely while no build-capable Codex session is available:

- trace the exact built-in font registry and every reference to Arabic/Noto/Ubuntu faces;
- trace `--strip-unused` from generator to PlatformIO/release build invocation;
- trace OPDS parser callers, UI/activity wiring, network/TLS/download path, settings/persistence and library refresh;
- trace `BleInput` callers and compile guards, then identify which FreeInk BLE objects/libraries would become linked if enabled;
- inspect hyphenation generated data and lookup paths;
- inspect theme/assets/web resources for compile/link reachability;
- compare current CrossPoint/CrossInk/InkPointX/CrossPDF/CrossLink implementations and record exact commits/PRs worth revisiting;
- identify likely linker symbols/object files to look for later, so the map audit has a checklist.

Research findings must be labeled **confirmed source fact**, **upstream observation**, or **hypothesis requiring build measurement**.

## Next build-capable/Codex plan

When Codex/local build access returns:

1. **Freeze the comparison point.** Confirm branch/HEAD and reproduce normal `gh_release` before modifying source. Compare against the released 1.6.2 measurements already recorded in the backlog.
2. **Produce map evidence.** Preserve the ELF/map/size output and rank linked contributors by object/symbol/category: fonts, hyphenation, i18n, themes/assets, web/network/TLS, image decoders, inherited features.
3. **Run one-variable experiments.** Build baseline, then independently test candidates such as unused-i18n stripping, safe built-in-font reductions/move-to-SD ideas, hyphenation representation/reduction and unreachable inherited feature removal. Revert between experiments.
4. **Record exact deltas.** For every experiment record linked flash, padded `firmware.bin`, app-slot margin, static RAM and—where runtime behavior is touched—free heap/largest block.
5. **Keep only evidence-backed wins.** No partition enlargement. No Arabic/Quran, pagination, cache-format, KOSync, image-quality or recovery regression for flash savings.
6. **Only after headroom recovery**, resume the wider 1.6.3 sequence: upstream EPUB/font/image memory deltas, SD/SPI benchmark, small safe fixes, then measured feature prototypes.

## Map-audit checklist for the next session

Search the release map/ELF for:
- built-in Arabic/Noto Sans/Noto Serif/Ubuntu font symbols and registry tables;
- hyphenation trie/data symbols;
- i18n tables/strings that survive release linking;
- OPDS parser/UI/network symbols;
- BLE/FreeInk HID host symbols;
- theme-specific renderers and compiled image/icon assets;
- web server/static assets;
- TLS/WolfSSL objects;
- JPEG/PNG/CBZ decoder objects;
- duplicate or unexpectedly retained activities.

Do not infer linked cost from repository file size. The map and normal release binary are authoritative.


## 2026-09-18 — font, hyphenation and i18n trace

### I18n: stronger finding — unused stripping is already active in PlatformIO builds

**Confirmed source fact:** `platformio.ini` runs `pre:scripts/gen_i18n.py` for the base firmware build and the native simulators. At the bottom of `gen_i18n.py`, the SCons/PlatformIO path calls `main(strip_unused=True)`. Therefore the normal PlatformIO generation path already strips detected unused `STR_*` keys.

**Consequence:** do **not** give Codex a task to simply “enable `--strip-unused`”; that optimization is already active. The useful follow-up is to capture the generator's current language/string/flash report during a clean release build, verify generated files are current, and only investigate deeper representation/dedup changes if the report shows worthwhile remaining cost. Deleting languages is not currently justified.

### Hyphenation: all ten language tries are directly included by the registry

**Confirmed source fact:** `lib/Epub/Epub/hyphenation/LanguageRegistry.cpp` directly includes generated tries for **de, en, es, fi, fr, it, pl, ru, sv and uk**, constructs ten static `LanguageHyphenator` instances and exposes all ten through one registry. `Hyphenator.cpp` selects among them from EPUB language metadata, including ISO-639-2 normalization.

**Confirmed source fact:** the generated trie headers are raw `constexpr uint8_t[]` firmware data produced from Hypher binary automata. The repository documentation explicitly says the reader keeps these automata in flash.

**Important hypothesis requiring map/build measurement:** because the registry references all ten descriptors, the generated tries are strong candidates for real linked flash cost rather than merely large repository files. The linker map must prove the exact contribution. German is especially worth checking: its generated C++ header is about 1.29 MB of source text, much larger than the others, but this is **not** the binary byte cost and must not be reported as a saving.

**Potential design investigations, not implementation decisions:**
- determine the actual byte length of each generated `*_trie_data[]` array from symbols/map;
- test whether uncommon-language tries could become optional SD resources while retaining English or another minimal built-in fallback;
- alternatively investigate a build-time language subset only if Nooir can preserve a sensible multilingual user experience;
- compare upstream CrossPoint/CrossInk treatment before inventing a new format;
- preserve explicit/soft-hyphen and fallback line-breaking behavior even when no language trie is available.

This area may be a more promising flash target than i18n because i18n already strips unused keys, while the language registry deliberately references every hyphenation automaton.

### Fonts: the built-in registry is broad and the generated fonts already mix fallback scripts

**Confirmed source fact:** `lib/EpdFont/builtinFonts/all.h` includes:
- Noto Serif 12/14/16/18 in regular, bold, italic and bold-italic;
- Noto Sans 12/14/16/18 in regular, bold, italic and bold-italic;
- Noto Sans 8 regular;
- Arabic 12/14/16/18 regular and bold;
- Ubuntu 10 and 12 regular and bold.

`src/fontIds.h` exposes IDs for the four Noto Serif sizes, four Noto Sans sizes, four Arabic sizes, UI 10/12 and the small font.

**Confirmed source fact:** generated UI/small fonts are not Latin-only. For example, `notosans_8_regular` and `ubuntu_10_regular` are generated with Noto Sans Hebrew/Arabic sources and explicit Hebrew/Arabic/presentation-form intervals. This means “remove Arabic from UI fonts because there is a separate Arabic reader font” is **not safe as an assumption**. UI metadata, menus or fallback rendering may rely on that coverage.

**Confirmed source fact:** EpdFont supports style families with regular/bold/italic/bold-italic fallbacks, while SD-card font infrastructure already exists (`SdCardFont`, manager and registry). This makes “move optional reader faces/styles to SD” architecturally plausible, but the default/fallback/boot-safe set must be traced first.

**Next source-only font trace:**
1. identify the translation unit that includes `builtinFonts/all.h` and constructs each built-in `EpdFontFamily`;
2. map every font ID to UI, EPUB reader, fallback and Arabic/Quran call sites;
3. distinguish mandatory boot/UI fonts from optional reader typography;
4. identify whether every style/size is referenced strongly enough to force its generated data into the release;
5. compare CrossInk/CrossPoint current built-in-vs-SD font strategy;
6. prepare one-variable build experiments for Codex, but make no font deletion before map evidence and Arabic/Quran/UI regression tests.

### Updated priority for the next build-capable session

The first measurement queue is now:

**hyphenation tries → built-in font families/styles → i18n report/representation → themes/assets/inherited features**

Reason: i18n unused-key stripping is already enabled, whereas the source currently shows ten explicitly registered embedded hyphenation automata and a broad built-in font set. This is a prioritization hypothesis based on source reachability; exact savings still require `gh_release` map/ELF evidence.


## 2026-09-18 — built-in font reachability trace

### Exact construction/registration path

**Confirmed source fact:** `src/main.cpp` is the translation unit that includes `<builtinFonts/all.h>`, constructs the global built-in `EpdFont` / `EpdFontFamily` objects and registers them with `GfxRenderer` in `setupDisplayAndFonts()`.

The normal release build (no `OMIT_FONTS`) explicitly constructs and registers:

- Noto Serif 12/14/16/18: regular + bold + italic + bold-italic;
- Noto Sans 12/14/16/18: regular + bold + italic + bold-italic;
- Arabic 12/14/16/18: regular + bold;
- Noto Sans 8 regular as `SMALL_FONT_ID`;
- Ubuntu 10 regular + bold as `UI_10_FONT_ID`;
- Ubuntu 12 regular + bold as `UI_12_FONT_ID`.

The 14 pt Noto Serif and Arabic families plus the three UI families sit outside the `#ifndef OMIT_FONTS` block. The remaining reader sizes/families sit inside it. This is useful because the source already has a concept of a minimum font set, but `OMIT_FONTS` is **not** a proposed production configuration until behavior and size are measured.

### Reader and Arabic fallback coupling

**Confirmed source fact:** `setupDisplayAndFonts()` registers Arabic fallback per point size. Noto Serif and Noto Sans reader families at 12/14/16/18 route missing Arabic glyphs to matching Arabic 12/14/16/18 families. The renderer also keeps a point-size Arabic fallback map for SD reader fonts.

**Consequence:** the dedicated Arabic families are not isolated “Quran-only fonts.” They are part of the general EPUB/SD-font fallback path. Removing them would change Arabic rendering for ordinary books and potentially SD reader fonts. Any size reduction experiment involving Arabic must therefore include normal Arabic EPUB + Quran fixtures + SD-font Arabic fallback, not just English EPUB tests.

### UI font coupling

**Confirmed source fact:** `SdCardFontSystem.cpp` defines built-in UI sizes as:
- `SMALL_FONT_ID` = 8 pt;
- `UI_10_FONT_ID` = 10 pt;
- `UI_12_FONT_ID` = 12 pt.

When the selected SD family has CJK coverage, Nooir loads matching 8/10/12 pt SD files and registers them as CJK fallbacks for those built-in UI IDs. Latin UI remains on the built-in fonts.

**Consequence:** Nooir already has the same broad architectural direction documented by current CrossPoint: small built-in UI fonts plus size-matched SD fallback for scripts that need larger coverage. The safe flash question is therefore not “can UI fonts move to SD?” but “what is the smallest boot-safe built-in UI coverage we can preserve while retaining multilingual fallback?”

### Upstream comparison

Current CrossPoint documentation says its normal reader exposes two built-in reader families (Noto Serif and Noto Sans) and SD fonts for additional families. Its SD-font documentation says CJK UI fallback reuses the selected SD family at 8/10/12 pt instead of embedding a large CJK set in flash.

Current CrossInk documentation says its built-ins are Lexend Deca and Bitter, with custom families on SD. CrossInk v1.4.0 explicitly removed a built-in ChareInk family to reduce firmware size and made it downloadable instead. This is strong prior art for moving **optional reader families** out of firmware while keeping a small built-in safety set.

Nooir is already structurally close to this model, but its normal build still registers both full Noto Serif and full Noto Sans reader families across four sizes/styles, plus dedicated Arabic fallback families.

### Strongest font experiment for a build-capable session

Do **not** start by touching Ubuntu/NotoSans-8 UI fonts or Arabic fallback.

The cleanest first A/B experiment is:

1. baseline normal `gh_release`;
2. build a temporary measurement variant that removes only the **built-in Noto Sans reader family** (12/14/16/18 and four styles), while leaving:
   - Noto Serif default reader family intact;
   - Arabic fallback families intact;
   - Noto Sans 8 small UI font intact;
   - Ubuntu 10/12 UI fonts intact;
   - SD font support intact;
3. compare firmware binary/map symbols;
4. do not merge the removal yet;
5. inspect settings behavior when an existing user has Noto Sans selected and define a safe migration/fallback before any production change.

Why this is the cleanest first experiment: Noto Sans is an alternate built-in reader family, while the UI and Arabic paths have stronger boot/multilingual coupling. CrossInk's removal of an optional built-in family provides upstream precedent for exactly this class of tradeoff.

### Second font experiment, only after the first

If removing/moving Noto Sans produces meaningful savings, investigate packaging it as downloadable `.cpfont` files through the already-existing Font Manager rather than deleting the choice entirely. This preserves user choice while moving optional typography to SD.

Only after that should Codex measure whether multiple Noto Serif point sizes/styles can be reduced or delegated to SD. That is more invasive because Noto Serif is the default boot-safe reader family and style fallback affects EPUB CSS rendering.

### Measurement requirements

For every font A/B:
- record `firmware.bin` bytes and exact delta;
- inspect map/ELF symbols for removed generated font arrays/metadata rather than relying on header source size;
- record static RAM delta;
- cold boot with no SD font installed;
- open default English EPUB;
- test bold/italic/bold-italic CSS;
- test Arabic EPUB and Quran fixture;
- test an SD font and dictionary font;
- test CJK UI fallback if a suitable SD family is present;
- test an existing settings file that names the removed built-in family.

No production font removal should be merged from size numbers alone.


## 2026-09-18 — hyphenation and UI-language storage audit

### Hyphenation is independent from UI-language availability

**Confirmed source fact:** Nooir currently has **31 UI translation YAML files**, while the EPUB hyphenation registry embeds only **10 language tries**: de, en, es, fi, fr, it, pl, ru, sv and uk.

Therefore UI language and book hyphenation language are separate concerns. Removing a hyphenation trie would not remove that UI language, and adding an UI translation does not automatically add book hyphenation.

`Hyphenator.cpp` chooses a trie from EPUB language metadata/BCP-47 primary tags, with ISO-639-2 normalization. If no language-specific trie exists, explicit hyphens/soft hyphens and apostrophe handling still work; the caller may also request generic fallback break positions. This behavior must be preserved if tries ever move off flash.

### Hyphenation source footprint strongly justifies map measurement

Repository generated-header sizes currently show a very uneven distribution:

- German source header: ~1,289,474 B
- Russian: ~208,727 B
- English: ~168,745 B
- Swedish: ~147,797 B
- Ukrainian: ~133,527 B
- Polish: ~97,391 B
- Spanish: ~85,633 B
- French: ~44,003 B
- Italian: ~10,044 B
- Finnish: ~8,166 B

These are **repository/source sizes only** and are not flash costs. The useful fact is structural: `LanguageRegistry.cpp` includes every generated header and constructs a global `LanguageHyphenator` for every descriptor, so all ten are intentionally reachable. The map/ELF must report each `*_trie_data[]` symbol's real binary size.

### Strong hyphenation experiment sequence for Codex

1. Baseline `gh_release` + map.
2. Record each embedded trie symbol's exact size and total.
3. Temporary A/B build with one large non-default trie (German is the obvious measurement probe) excluded from the registry; record exact binary delta. This verifies whether map attribution matches final image change.
4. If the total is meaningful, prototype **SD-backed optional hyphenation resources** rather than deleting language support.
5. Keep at least a boot-safe/default behavior when SD resources are absent or corrupt.
6. Test EPUB metadata forms such as `de`, `de-DE`, `deu`/`ger`, plus English and Cyrillic examples.
7. Test explicit hard/soft hyphens, apostrophes, fallback line breaking and CJK no-visible-hyphen behavior.

**Upstream observation:** CrossPoint community planning has explicitly discussed moving fonts, hyphenation and translations out of the firmware binary, and current CrossPoint continues adding language-specific hyphenation. This supports investigating SD-backed resources, but it is not evidence that Nooir can adopt such a change without its own measurements/tests.

### UI language system: 31 languages are compiled into the generated i18n tables

**Confirmed source fact:** Nooir currently contains translation YAMLs for 31 UI languages:
Arabic, Belarusian, Bosnian, Catalan, Czech, Danish, Dutch, English, Finnish, French, German, Hebrew, Hungarian, Indonesian, Italian, Kazakh, Lithuanian, Norwegian, Polish, Portuguese-BR, Portuguese-PT, Romanian, Russian, Slovak, Slovenian, Spanish, Swedish, Turkish, Ukrainian, Valencian and Vietnamese.

The generator treats English as the reference language. Missing keys in another language fall back to English.

**Confirmed source fact:** the generated representation already has two flash-saving mechanisms:

1. PlatformIO runs `gen_i18n.py` with `strip_unused=True`, removing translation keys not referenced by `src/` or `lib/`.
2. For every non-English language, a string identical to English is **not duplicated** in that language's string blob. Its 16-bit offset sets bit 15 and points back into the English blob.

This means Nooir's i18n implementation is already more compact than a naive “31 complete copies of every string” design.

### The likely remaining i18n cost is offset-table scaling

**Confirmed source fact:** every compiled language still receives a `uint16_t OFFSETS_<LANG>[]` entry for **every retained StrId**, even when many values fall back to English. The generator's own report computes this fixed component as:

`number_of_languages × retained_string_keys × 2 bytes`

plus the deduplicated UTF-8 string blobs.

With 31 languages, each retained UI string key costs **62 bytes of offset-table flash across all languages**, before any translated text bytes. The exact retained-key count after stripping must be captured from a real generator run; do not estimate it from the ~618 lines in `english.yaml`.

This makes offset-table representation a more credible i18n optimization target than simply deleting untranslated/English-identical strings, because the latter are already deduplicated.

### Language completeness is not uniform

**Confirmed source observation:** translation files have materially different amounts of authored content. For example the current repository files have roughly 618 lines for English, 546 for German, 428 for Hebrew and 413 for Arabic. Missing translation keys intentionally fall back to English.

These line counts are **not** a quality score and should not be used to delete a language. They simply mean the flash cost of translated blobs differs by language while the per-language offset table remains fixed for all retained keys.

### UI-language experiment sequence for Codex

Before considering removal/offloading of any language:

1. Run the generator during a clean normal build and preserve its table:
   - language count;
   - retained/unused key count;
   - each language's deduplicated string bytes;
   - total string-blob bytes;
   - total offset-table bytes.
2. Confirm the generated output is actually linked as reported.
3. Use map/ELF to attribute `STRINGS_*_DATA` and `OFFSETS_*` symbols.
4. Only if i18n is a meaningful share of the recoverable flash, investigate representation changes.

Potential representation experiments, in preferred order:
- reduce/deduplicate offset-table storage without changing user-visible language coverage;
- investigate sparse overrides for non-English languages rather than a full offset array per language;
- investigate SD-loaded optional language packs only as a larger architectural experiment, with English built in as recovery fallback;
- do **not** start by deleting languages.

### Important language/boot constraints

Any future SD-loaded language design needs:
- English always available without SD;
- safe fallback if the selected language pack is missing/corrupt;
- migration compatibility for persisted `SETTINGS.language`;
- preservation of the generator's frozen V1 language migration table;
- language names/menu available early enough to recover/change language;
- Arabic/Hebrew RTL UI tested separately from reader-language fonts;
- no assumption that UI language equals book language, keyboard language or hyphenation language.

Current CrossPoint settings explicitly separate UI language from keyboard-layout choice because book/input language can differ from UI language. Nooir should preserve the same conceptual separation.

### Revised flash-recovery research order

Based on source reachability rather than guessed byte savings:

1. **Hyphenation map symbols** — ten intentionally embedded automata, uneven and potentially substantial.
2. **Optional built-in reader family measurement** — especially Noto Sans reader sizes/styles, while preserving UI + Arabic safety fonts.
3. **I18n generator/map report** — especially the fixed per-language offset tables; unused-key stripping and English-string dedup are already active.
4. Themes/assets/web/inherited linked code.
5. Only then choose which architecture is worth implementing.

The goal remains to preserve multilingual capability while making optional resources pay their flash cost only when they are actually needed.
