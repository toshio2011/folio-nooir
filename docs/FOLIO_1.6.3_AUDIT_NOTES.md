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


## 2026-09-18 — candidate SD resource-pack architecture

### Status

**INVESTIGATE — architecture candidate, not an approved implementation.**

The flash audit now has three related classes of optional/static resources that may be better paid for from SD rather than permanently from the application image:

1. non-English UI translations;
2. language-specific EPUB hyphenation tries;
3. optional reader font families/styles.

Nooir already has important pieces of the required pattern: built-in fallback fonts, SD font discovery/Font Manager, English translation fallback, and graceful no-language-hyphenator behavior. This makes a shared SD-resource direction worth measuring before building three unrelated loaders.

### Proposed safety model

Keep a **small recovery-safe core in firmware**:
- English UI strings and language name/recovery controls;
- the minimum boot-safe UI font set;
- the default boot-safe reader font/fallback required to open a book without optional SD resources;
- enough line-breaking behavior to remain usable if an optional hyphenation pack is absent;
- resource loader/validation/versioning code.

Move only resources proven optional and worthwhile by measurement.

A missing, corrupt or incompatible SD resource must never prevent boot, Settings access, reading with the built-in fallback, or changing back to English/default resources.

### Translation packs

Candidate layout, naming still provisional:

`/.crosspoint/languages/<code>.lang`

A language pack should contain only what differs from the built-in English reference where practical, rather than another complete copy of the English table.

Before designing the binary format, Codex must measure current `STRINGS_*_DATA` and `OFFSETS_*` cost. If the recoverable flash is small, do not add a complex loader.

Required behavior:
- English always built in;
- persisted language code/enum migrates safely;
- missing selected pack -> English fallback, not boot failure;
- corrupt/version-mismatched pack -> reject + English fallback;
- language selection clearly distinguishes installed vs available packs;
- Arabic/Hebrew UI direction/rendering remains tested;
- V1 `language.bin` migration compatibility preserved.

### Hyphenation packs

Candidate layout:

`/.crosspoint/hyphenation/<primary-tag>.trie`

The existing serialized trie representation is already a natural starting point because `SerializedHyphenationPatterns` is a descriptor over byte data. Investigate whether the same serialized bytes can be read/mapped/buffered from SD without redesigning Liang behavior.

Potential policy:
- keep English built in initially, unless measurement later proves even that is worth externalizing;
- load a language trie on demand from EPUB language metadata;
- cache only the currently needed resource or a tightly bounded set;
- missing/corrupt trie -> existing explicit-hyphen/soft-hyphen/apostrophe/fallback behavior, not reader failure.

Do not assume SD I/O is fast enough. Measure chapter/page-turn impact and heap/largest-block behavior before production use.

### Optional font packs

Continue using the existing SD font/`.cpfont` and Font Manager direction rather than inventing a second font-pack system.

The first candidate remains the optional built-in Noto Sans reader family. If map/build evidence shows worthwhile savings, investigate shipping/installing it through Font Manager while retaining the minimum built-in UI/default-reader/Arabic safety set.

### Shared resource infrastructure

If two or more categories prove worth externalizing, investigate one small shared layer for:
- resource type + version;
- language/resource identifier;
- length;
- CRC/checksum;
- compatibility/version field;
- atomic install/update;
- validation before activation;
- missing/corrupt fallback;
- optional catalog/download integration.

Do **not** create a generic resource framework first. Prove the flash savings of each category, then extract shared infrastructure only where it reduces total code/maintenance.

### Installation/update UX

Longer-term options, in order of implementation simplicity:

1. manual SD copy;
2. web-transfer install/manage;
3. reuse/extend the existing Font Manager/catalog pattern for downloadable language/hyphenation resources;
4. optional bundled resource-pack installer.

The firmware must remain fully recoverable without network access.

### Critical accounting rule

Moving data to SD is only a win if:

`flash removed - loader/validation/catalog code added = meaningful net flash recovery`

For every prototype record:
- original linked resource bytes;
- new loader/metadata code bytes;
- net application-image saving;
- static RAM delta;
- peak heap/largest-block during load;
- SD bytes used;
- first-load latency;
- steady-state page/menu latency.

A resource migration that saves little flash while increasing heap fragmentation or making boot/reading dependent on SD integrity should be rejected.

### Candidate experiment order

When build access returns:

1. measure all ten hyphenation trie symbols and total;
2. measure Noto Sans reader-family symbols and total;
3. measure i18n string blobs vs offset tables;
4. rank by recoverable bytes;
5. prototype **one category only** — preferably the largest cleanly separable resource;
6. measure loader overhead and net saving;
7. only then decide whether a shared SD resource-pack architecture is justified.

This keeps the idea evidence-driven: the desired end state may be a smaller firmware core with richer optional SD resources, but only if the measured economics support it.


## 2026-09-18 — themes, embedded images and web-assets reachability audit

### Themes: prior measurement already answers most of this bucket

**Confirmed source fact:** Nooir already contains `docs/theme-flash-analysis.md`, based on an older 1.6.0 ELF/map. It found fine-grained linker GC was active and unused theme functions were discarded. The historical estimates were small: RoundedRaff was the largest clean standalone candidate at roughly 4.5–7 KB; Lyra 3 Covers roughly 1.5–2.5 KB; removing Classic alone was negligible because `BaseTheme` remains shared.

**Confirmed current-source fact:** `UITheme.cpp` now constructs six selectable themes: Classic, Lyra, RoundedRaff, Lyra 3 Covers, Folio Nooir and Carousel. Folio Nooir/Carousel share Lyra metrics, and Folio Nooir remains behaviorally tied to bookshelf activities rather than being a detachable bitmap skin.

**Interpretation:** themes remain a lower-priority flash target than hyphenation/fonts/i18n. The old numbers must not be treated as current 1.6.2 measurements, especially because Carousel was added later. Future map audit should refresh theme symbols, but broad theme removal should not lead the recovery plan.

### Repository screenshots are not firmware assets

**Confirmed source fact:** the multi-megabyte JPEG/PNG files under `docs/images/` are documentation assets. Their repository size is irrelevant to application flash unless a build step explicitly embeds them. No such embedding path was found in this audit.

Do not waste Codex time optimizing/removing documentation images for firmware size.

### Device image headers are deliberately embedded

**Confirmed source fact:** `src/images/` contains generated/compiled image headers such as:
- `LoadingIcon.h`;
- `MoonIcon.h`;
- `Logo120.h`;
- `NooirLogo360.h`.

The corresponding PNG/SVG source files are not themselves proof of flash use; the C/C++ headers referenced by firmware are the measurement target.

**Confirmed source fact:** `main.cpp` directly includes and renders `LoadingIcon.h` during quick-resume wake. Other logo/sleep imagery must be traced through their call sites before deciding whether any can move to SD.

**Interpretation:** these are worth map-symbol accounting but are unlikely to rival the larger data buckets. Preserve boot/recovery visibility and quick-resume behavior.

### Web UI is a real embedded flash bucket, but source HTML sizes overstate it

**Confirmed source fact:** `scripts/build_html.py` walks `src/`, minifies HTML, gzip-compresses HTML/JS at compression level 9, and emits generated `constexpr ... PROGMEM` byte arrays.

**Confirmed source fact:** `CrossPointWebServer.cpp` includes and serves the generated arrays for:
- Files/Transfer;
- Fonts;
- Home;
- Library;
- Stats;
- Settings;
- To-Do;
- JSZip.

Therefore the web pages are deliberately linked firmware resources, not SD-hosted files.

The raw source files currently include a very large `FilesPage.html` (~233 KB) and `jszip.min.js` (~98 KB), but **raw source bytes are not flash cost** because the build gzip-compresses them. Future build output/map must capture the generated compressed sizes and final linked symbols.

### JSZip deserves a dedicated measurement

**Confirmed source fact:** Files/Transfer loads `/js/jszip.min.js`, and the web server exposes a dedicated JSZip handler. The raw minified JS source is ~97.6 KB before gzip.

**Hypothesis requiring build measurement:** JSZip may be one of the larger individual web static assets even after gzip. Determine exactly which Transfer workflow requires browser-side ZIP handling (for example CBZ preparation/upload) before considering removal, replacement, lazy external hosting, or SD hosting.

Do not remove it merely because the raw file is large: web-transfer CBZ/JPEG preparation is released functionality and must remain intact unless a replacement is proven.

### Web assets and the SD-resource idea

Unlike translations/hyphenation, moving the entire web UI to SD has a less attractive failure model: File Transfer is itself a recovery/management path, so making its core page depend on SD files can reduce robustness.

Preferred investigation order:
1. keep a minimal built-in web management/transfer page;
2. measure each compressed page/JS symbol;
3. identify unusually large optional pages/scripts;
4. only if worthwhile, consider optional richer web assets on SD with a built-in fallback page;
5. never make firmware update/recovery/file transfer depend solely on an optional SD web bundle.

### Build-capable web measurement checklist

Capture `build_html.py` output and map/ELF symbols for every generated asset:
- original source bytes;
- minified bytes;
- gzip bytes;
- final linked symbol bytes.

Then rank:
`FilesPageHtml`, `FontsPageHtml`, `HomePageHtml`, `LibraryPageHtml`, `SettingsPageHtml`, `StatsPageHtml`, `ToDoPageHtml`, `jszip_minJs`.

Run one-variable A/B only for a genuinely large optional asset. Account for any replacement/fallback code.

### Revised priority after this audit

No source evidence currently promotes themes or small device icons above the existing top candidates.

Current order remains:
1. hyphenation tries;
2. optional built-in reader fonts;
3. i18n strings/offset tables;
4. **compressed web assets, especially JSZip/Transfer**;
5. current theme symbols (refresh the old 1.6.0 measurements);
6. small embedded boot/UI images;
7. inherited linked functionality.

The next source-only audit should focus on **inherited feature reachability and network/TLS dependencies**, because removing an apparently unused feature only matters when it also lets substantial dependent libraries fall out of the link.


## 2026-09-18 — inherited feature reachability and network/TLS audit

### WolfSSL is intentional shared infrastructure, not obvious dead weight

**Confirmed source fact:** `platformio.ini` deliberately enables the FreeInk secure-network path with `FREEINK_NET_WOLFSSL=1` and explicitly configures wolfSSL features including TLS 1.3, SP ECC, HKDF, supported curves, Curve25519, FFDHE-2048, RSA-PSS and SNI. The comments document that the SP ECC path was selected to avoid large temporary big-number allocations under the low-heap reading-session conditions.

**Confirmed source fact:** `HttpDownloader.cpp` selects `SecureHttpClient` whenever `FREEINK_NET_WOLFSSL` is defined. This downloader is shared by HTTPS functionality rather than belonging to one optional screen.

**Confirmed source fact:** `OtaUpdater.cpp` deliberately routes GitHub release checks and firmware downloads through `HttpDownloader`. Its comments state that this avoids the precompiled esp-tls/mbedTLS path for the required TLS behavior and reuses wolfSSL redirect handling.

**Interpretation:** removing wolfSSL is not a safe generic flash optimization. It would require replacing a released shared HTTPS transport and revalidating OTA, KOSync, OPDS/font downloads and any other `HttpDownloader` users. Treat wolfSSL as a map-measurement bucket, not a deletion candidate.

### There are two crypto/TLS families for different jobs

**Confirmed source fact:** despite wolfSSL being the network TLS path, `FirmwareFlasher.cpp` directly uses mbedTLS SHA-256 for offline SD firmware-image integrity verification.

**Interpretation:** even if a future network experiment changed TLS stacks, not every mbedTLS object would disappear. Conversely, seeing both wolfSSL and mbedTLS in a map does not by itself prove duplicate HTTPS stacks are linked. Attribute symbols by caller before estimating recoverable flash.

### Network functionality is highly shared

**Confirmed source fact:** `CrossPointWebServer` is not just a decorative browser page. It provides Transfer/file management, library/stats/settings/To-Do/font endpoints, OPDS-server configuration, Wi-Fi/clock-weather settings, WebSocket uploads, UDP discovery and WebDAV.

**Confirmed source fact:** `CalibreConnectActivity` instantiates this same `CrossPointWebServer`, starts mDNS and uses the server for transfer/upload status.

**Confirmed source fact:** `CrossPointWebServer::begin()` always adds a `WebDAVHandler`; the handler implements OPTIONS, PROPFIND, GET, HEAD, PUT, DELETE, MKCOL, MOVE, COPY, LOCK and UNLOCK.

**Interpretation:** deleting a single visible network menu item may recover much less than expected because the web server and its dependencies are shared. Any removal experiment must follow dependency fallout in the linker map, not count source files.

### WebDAV is a clean feature-level A/B candidate

**Confirmed source fact:** WebDAV is registered as a request handler by the built-in web server and has a substantial dedicated implementation.

**Hypothesis requiring build measurement:** if WebDAV is not important to Nooir's intended workflows, compiling it out may provide a measurable but bounded saving while retaining the ordinary web Transfer API. It is a better isolated A/B experiment than removing the entire web server.

Before any change:
- confirm whether current users/docs rely on WebDAV;
- measure `WebDAVHandler` symbols plus helpers that become unreachable;
- verify ordinary browser upload/download/rename/move/delete still works without it;
- do not count generic `WebServer` code as removable unless the map proves it falls out.

### Calibre naming does not mean an independent Calibre protocol stack

**Confirmed source fact:** `CalibreConnectActivity` primarily wraps Wi-Fi selection, mDNS and the existing `CrossPointWebServer`; it does not establish evidence here for a second large independent transfer stack.

**Interpretation:** removing this activity alone is unlikely to remove the web-server implementation while other Transfer features remain. Map it separately, but keep expectations low.

### BLE source exists but normal release capability must be measured carefully

**Confirmed source fact:** `BleInput.cpp/.h` is real application glue around FreeInk `BleKeyboardHost`. Its teardown explicitly returns NimBLE RAM to heap.

**Confirmed source fact:** the default development environment explicitly sets `FREEINK_CAP_BLE_HID_HOST=0`. The normal `gh_release` stanza shown in `platformio.ini` does not explicitly set that macro, while `BleKeyboardHost` remains listed in `lib_deps`.

**Important unresolved point:** do not infer from `lib_deps` alone that NimBLE/BLE is linked into the normal release. FreeInk capability defaults and linker reachability must be checked at the pinned SDK commit, then confirmed in the release map.

**Hypothesis requiring build measurement:** if release capability resolution is disabled/stubbed, `BleInput` may cost only thin application glue or be discarded. If capability defaults unexpectedly enable the host in `gh_release`, this could be a much larger accidental bucket. This is now a high-value map check because Bluetooth is not intended as a normal released Nooir feature yet.

### Existing core/component trimming is already deliberate

**Confirmed source fact:** `platformio.ini` already enables Arduino selective compilation and removes unused cloud components including Insights, RainMaker, diagnostics, scheduling, RCP update, secure-cert-manager and CBOR-related components. Wi-Fi IRAM options are also disabled deliberately to reclaim shared C3 SRAM.

**Interpretation:** avoid generic suggestions such as “turn on selective compilation” or “remove RainMaker”; Nooir already does them. Future inherited-feature cleanup must identify a concrete currently linked object/library.

### Network/TLS map checklist for the next build-capable session

Rank linked flash by library/object/symbol for:
- wolfSSL core and enabled crypto algorithms;
- FreeInk `SecureNet` / `SecureHttpClient`;
- any esp-tls/mbedTLS network objects versus mbedTLS SHA-only firmware validation;
- `HttpDownloader`;
- OTA updater and release JSON parser;
- `CrossPointWebServer`;
- WebSockets;
- WebDAV;
- mDNS;
- UDP discovery;
- OPDS parser/store/activity/download path;
- weather/clock HTTP path;
- KOSync;
- Font Manager network path;
- BLE/NimBLE/`BleKeyboardHost`.

For every candidate, distinguish:
1. direct feature code;
2. shared dependency that remains needed elsewhere;
3. dependency that actually falls out when the feature is disabled.

### New one-variable experiments worth queuing

After the existing hyphenation/font/i18n/web-asset measurements:

1. **WebDAV-off build** — ordinary web Transfer retained.
2. **BLE capability explicit-off release build** — only if the baseline map shows unexpected BLE/NimBLE symbols.
3. **UDP/mDNS discovery isolation** — separately measure convenience discovery code without removing transfer.
4. **Optional web page isolation** — only after compressed generated-symbol ranking.
5. **TLS algorithm audit** — inspect what wolfSSL algorithms are actually retained before changing any compile flags. Never remove an algorithm solely from the config list; test the real servers Nooir supports.

### Updated interpretation

The network stack is not a promising “delete Wi-Fi and win huge flash” target because Nooir intentionally relies on it for OTA, sync, fonts, OPDS and transfer. The useful opportunity is **surgical reachability cleanup**: optional WebDAV/discovery/static-web pieces and any accidentally linked BLE or crypto objects.

The highest-value unresolved question from this audit is now:

> Does the production `gh_release` actually link any BLE/NimBLE host implementation despite Bluetooth not being a released normal feature?

That should be answered from the pinned FreeInk capability definition first and then from the 1.6.2 release map.


## 2026-09-18 — pinned FreeInk BLE capability resolution

### Production BLE uncertainty resolved at source level

**Confirmed source fact:** the pinned Nooir FreeInk commit `958720659ea289ae325e83db20049d0ea844800d` defines `FREEINK_CAP_BLE_HID_HOST` as **0 by default** in `BoardConfig.h`. It is explicitly documented as opt-in and not board-derived. The only compatibility override is the older `FREEINK_CAP_BLE_KEYBOARD` macro; absent that, HID host remains 0.

Therefore the normal Nooir `gh_release`, which does not explicitly enable either BLE capability macro, resolves to:

`FREEINK_CAP_BLE_HID_HOST = 0`

for the shared X3/X4 release.

### Disabled FreeInk BLE library is intentionally stub-only

**Confirmed source fact:** the pinned `BleKeyboardHost/library.json` explicitly says:
- real NimBLE central code compiles only when `FREEINK_CAP_BLE_HID_HOST` is enabled;
- otherwise stub bodies are linked and reference no BLE code;
- NimBLE-Arduino is intentionally **not** declared as a library dependency so disabled builds pull in zero BLE stack code.

**Confirmed source fact:** `BleKeyboardHost.cpp` places all `NimBLEDevice.h`, Preferences, scanning, pairing, connection-task and HID implementation behind:

`#if FREEINK_CAP_BLE_HID_HOST`

The public header is deliberately NimBLE-free.

### Conclusion

The earlier concern that production `gh_release` might accidentally pull the NimBLE host stack merely because `BleKeyboardHost` appears in Nooir's `lib_deps` is **not supported by the pinned source**. Normal 1.6.2 release configuration should compile the stub implementation, not the real BLE host.

This makes BLE a **low-priority flash-recovery target for the current release**. Do not spend implementation time adding another explicit release-off flag purely for size unless the map contradicts the source-level expectation.

### Still verify once in the map

The future release-map audit should nevertheless verify:
- no `NimBLE*` implementation symbols;
- no BLE controller/host objects retained specifically for `BleKeyboardHost`;
- only small stub/application glue, if any.

If NimBLE symbols unexpectedly appear, treat that as a build-system/linker anomaly and investigate immediately.

### Architectural implication for future Bluetooth

This also confirms that Nooir's existing BLE work is well isolated for the later experimental page-turner plan. Enabling Bluetooth should be treated as an explicit feature/profile that adds NimBLE-Arduino and flips the capability, rather than silently growing the normal X3/X4 firmware.

That matches the 1.6.3 roadmap: keep Bluetooth experimental until its exact flash increase, runtime heap cost, reconnect stability, sleep/wake behavior and physical X4 reliability are measured.

### Flash-recovery queue adjustment

Because accidental BLE linkage is now unlikely from source inspection, the current source-informed order is:

1. hyphenation tries;
2. optional built-in reader fonts;
3. i18n strings/offset tables and SD-language-pack economics;
4. compressed web assets / JSZip;
5. WebDAV and discovery convenience features;
6. themes and embedded UI images;
7. TLS/crypto symbol reachability;
8. BLE only as a map sanity check for the normal release.

This finding should be revisited only if the production map disagrees.


## 2026-09-18 — WebDAV, mDNS and UDP discovery reachability

### WebDAV is genuinely separable from ordinary browser Transfer

**Confirmed source fact:** `CrossPointWebServer::begin()` registers ordinary HTTP routes for file listing, download, multipart upload, mkdir, rename, move and delete **before** separately installing `WebDAVHandler` with `server->addHandler(new WebDAVHandler())`.

**Confirmed source fact:** WebDAV additionally collects six DAV-specific headers and implements OPTIONS, PROPFIND, GET, HEAD, PUT, DELETE, MKCOL, MOVE, COPY, LOCK and UNLOCK. Its PUT path uses atomic-ish `.davtmp` replacement and clears book cache after successful writes.

**Interpretation:** compiling out `WebDAVHandler` should not inherently remove Nooir's normal browser Transfer API. It is therefore a clean one-variable A/B candidate. However, DAV clients would stop working and this must be documented/validated before any permanent removal.

**Build experiment:** baseline vs `NOOIR_WEBDAV=0` (temporary compile guard), preserving all ordinary HTTP and WebSocket transfer. Measure firmware/map delta and verify upload/download/mkdir/rename/move/delete through the browser.

### mDNS is optional convenience and scoped to CalibreConnectActivity

**Confirmed source fact:** `CalibreConnectActivity.cpp` directly includes `ESPmDNS.h`, calls `MDNS.begin("crosspoint")` when starting the server, and always calls `MDNS.end()` on exit.

The source comment explicitly says mDNS is optional for the Calibre plugin but helpful to users. The server remains accessible by its displayed IP when mDNS is unavailable.

**Interpretation:** mDNS is a clean convenience-feature A/B candidate. Removing it would lose `crosspoint.local` discovery/name resolution but should not remove the underlying HTTP/WebSocket transfer server.

**Important linker question:** because ESPmDNS is an Arduino/ESP component, exact recoverable flash depends on whether anything else references it. Map/object evidence is required.

### UDP discovery is independent and very small at application level

**Confirmed source fact:** `CrossPointWebServer` owns a `NetworkUDP` object and starts a listener on local UDP port 8134. During `handleClient()`, it accepts the literal discovery packet `hello` and replies with `crosspoint (on <hostname>);<wsPort>`.

**Confirmed source fact:** stopping the web server explicitly stops this UDP listener.

**Interpretation:** this is convenience discovery for clients, separate from actual HTTP/WebSocket transfer. The Nooir application logic itself is tiny. Removing it may save little because UDP networking remains part of the Wi-Fi stack, but it is cheap to A/B after larger candidates.

### WebSocket upload is NOT equivalent to discovery and should stay

**Confirmed source fact:** the same web server starts `WebSocketsServer` on port 81 and maintains binary-upload state/progress. `CalibreConnectActivity` reads this upload status to render received bytes/file completion.

Do not group WebSockets with removable UDP/mDNS discovery. It is part of the fast transfer path and must be measured/changed separately.

### Recommended A/B sequence

After hyphenation/fonts/i18n/web-static ranking:

1. WebDAV off, ordinary Transfer retained.
2. mDNS off, direct-IP Transfer retained.
3. UDP discovery off, direct-IP/WebSocket Transfer retained.
4. only then test combinations if individual map deltas prove additive.

For each variant record:
- linked flash and padded firmware delta;
- which library/object symbols disappear;
- static RAM delta;
- browser Transfer;
- WebSocket binary upload/progress;
- Calibre workflow by direct IP;
- file cache invalidation after writes;
- server start/stop and repeated reconnect.

Do not permanently remove a convenience feature for a tiny saving merely because the source can be isolated.

### Current expectation

**Hypothesis requiring build measurement:** WebDAV is the strongest of these three size candidates because it has substantial dedicated request/path/XML/file-operation code. mDNS may recover a component-sized chunk if no other references retain it. UDP discovery is likely the smallest direct saving.

This ordering is an experiment priority only, not a measured size claim.


## 2026-09-18 — remaining source-only audit sweep

This checkpoint closes the six remaining source-only audit categories requested before returning to build/map measurement. Claims below are source-level unless explicitly labelled otherwise.

### 1. OPDS end-to-end reachability

**Confirmed source fact:** OPDS is already a substantial, user-reachable feature, not dormant parser code.

The current chain is:
- `OpdsServerStore` persists up to 8 servers in `/.crosspoint/opds.json`;
- server records contain name, URL, username and password; the store documents password obfuscation with a hardware-derived key on disk;
- Home checks `OPDS_STORE.hasServers()` and inserts the OPDS Browser into the visible menu when configured;
- settings activities add/edit/delete OPDS servers;
- `OpdsBookBrowserActivity` performs Wi-Fi selection, navigation history, search, feed pagination, cancellation and download progress;
- feeds stream through `HttpDownloader -> OpdsParserStream -> OpdsParser`, avoiding whole-feed XML buffering;
- parser results expose navigation/acquisition entries, search template, previous/next links and bounded/truncated feed state;
- HTTP Basic-style credentials are passed through the downloader for both feed fetches and book downloads;
- downloads use a `.part` temporary file, then replace/rename only after success and clear the book cache;
- download folder and filename-format preferences already live in `CrossPointSettings`.

**Roadmap correction:** 1.6.3 must not describe OPDS as a future greenfield feature. The correct task is **OPDS hardening/reconciliation**: identify missing protocol/auth/catalog cases, test real servers, improve UX only where needed, and measure existing flash/heap.

**Likely remaining investigation:** Atom edge cases, OpenSearch compatibility, redirects/auth variants, unsupported acquisition MIME types, malformed/large feeds, filename collisions, HTTPS server matrix, and post-download library visibility.

### 2. Inherited activities / visible reachability

**Confirmed source fact:** many apparently inherited activities are genuinely reachable from current Nooir UI:
- Clock & Weather, To-Do, Reading Statistics, all-books Bookmarks and Clippings are explicitly launched from Home's menu;
- OPDS is conditionally exposed when servers exist;
- File Transfer launches `CrossPointWebServerActivity`;
- Network mode selection exposes Join Network, Connect to Calibre and Create Hotspot;
- AP mode uses DNS captive-portal behavior, mDNS, QR generation and the same web server;
- Calibre mode uses the shared web server;
- reader/settings activities cover bookmarks, clippings, footnotes, dictionary, KOSync, OTA/SD update, font download, profiles, status bar and reader settings.

**Important source observation:** Bluetooth settings/state still exist in the Nooir tree and settings schema even though the pinned FreeInk release capability resolves BLE HID host off. This is a **UI/settings reachability check**, not evidence of NimBLE flash cost. Future map/build should distinguish retained settings/activity glue from the disabled FreeInk host implementation.

**Conclusion:** there is no obvious large orphan activity family from this pass that can simply be deleted. Continue to let linker/map evidence identify unusually expensive leaf activities rather than deleting by ancestry.

### 3. Image / decoder dependency audit

**Confirmed source fact:** Nooir intentionally has more than one JPEG path for different failure/performance cases.

The EPUB/framebuffer decoder factory retains:
- JPEG via `JpegToFramebufferConverter` using JPEGDEC;
- PNG via `PngToFramebufferConverter`.

JPEGDEC is used for the primary direct-to-framebuffer JPEG path and its source comments note an approximately 17 KB decoder object, allocated on demand. The converter contains coarse JPEG scaling, fixed-point resampling, dithering and CBZ cache integration.

The tree also contains the TJpgDec framebuffer converter and the FreeInk TJpgDec implementation/configuration. This matches the released fallback strategy for baseline JPEGs that JPEGDEC cannot safely handle. Therefore **JPEGDEC vs TJpgDec is not presumed duplication**.

Separate JPEG/PNG-to-BMP converters support generated/cache/thumbnail workflows; CBZ page caching and EPUB image rendering have different output/lifetime requirements.

**Conclusion:** do not collapse decoders based only on library count. Future map must rank JPEGDEC, TJpgDec and PNG/miniz-related objects separately. Any removal A/B must test:
- baseline and progressive JPEG;
- long-AC-Huffman fallback fixture;
- EPUB inline images;
- cover/thumbnail generation;
- CBZ Fit Width/Fit Page/Landscape/Zoom and cache;
- PNG transparency/grayscale cases;
- low-heap fail-soft behavior.

**Potential optimization question:** if two paths retain equivalent scaling/dither/cache helpers independently, inspect symbol-level duplication after map generation. Source inspection alone is insufficient.

### 4. Storage / cache / persistence ownership audit

**Confirmed persistent SD state includes at least:**
- `/.crosspoint/book-states.json` — status/progress/dates/reading aggregates;
- `/.crosspoint/recent.json` — bounded Recent presentation + reading fields;
- `/.crosspoint/reading-stats.json` — up to 730 daily aggregates;
- `/.crosspoint/opds.json` — OPDS servers;
- `/.crosspoint/dictionary-history.json` — bounded 16-entry lookup hint history;
- `/.crosspoint/profiles/` — settings-only profiles;
- settings-owned OPDS download folder/filename mode, BLE mappings, font/reader/UI options and other persisted configuration;
- per-book EPUB/CBZ/XTC caches and progress files under the established `/.crosspoint` cache architecture;
- stable-page cache, thumbnails/covers, dictionary indexes/history, weather/clock and metadata override stores elsewhere in the same ownership model.

**Confirmed safety properties:** Recent deliberately avoids heavy source parsing during shelf boot; reading stats are committed away from the page-turn path; OPDS downloads use temporary files; cache invalidation is called after web/DAV/OPDS writes; settings profiles deliberately exclude progress, stats, bookmarks, clippings, Wi-Fi credentials and hardware calibration.

**Architecture implication:** future SD language/hyphenation packs should live beside this ecosystem but must have their **own version/validation/ownership boundary**. They must not be swept by generic “Clear Reading Data” or per-book cache deletion.

Before any persistence format change, preserve `SECTION_FILE_VERSION=41` compatibility expectations and separately document whether the changed artifact is settings, user data, reconstructible cache or downloadable resource.

### 5. Build configuration / dependency inventory

**Confirmed direct external/FreeInk dependencies in the current PlatformIO configuration include:** FreeInk hardware/display/UI/storage/network libraries, ArduinoJson 7.4.2, QRCode 0.0.1, JPEGDEC pinned to commit `8628297...`, WebSockets 2.7.3 and Arduino-wolfSSL 5.7.2. Local libraries include expat, miniz and uzlib.

Current classification:
- **core/required:** BoardConfig, display/input/storage/power pieces, ArduinoJson;
- **network-shared:** SecureNet/wolfSSL, WebSockets;
- **feature-specific but currently reachable:** QRCode (File Transfer/AP UI), OPDS parser/expat, image decoder libraries, web/DAV pieces;
- **capability-stubbed in normal release:** BleKeyboardHost real NimBLE path, as established in the preceding audit;
- **compression/archive:** miniz/uzlib must be attributed to EPUB/CBZ/PNG/archive callers before changing;
- **simulator-only:** crosspoint-simulator dependency in simulator environments.

**Confirmed build hygiene already present:** selective compilation and explicit component trimming mean a dependency listed in `lib_deps` is not proof its full implementation is linked.

**Next map task:** produce a library/object contribution table and classify each retained object as core, shared, leaf-feature, fallback or unexpected. Do not optimize dependency declarations before proving linked cost.

### 6. Upstream refresh as of 2026-09-18

**CrossPoint:** latest relevant development commits still reinforce the existing 1.6.3 harvest list:
- `dc9c3eabc4...` — File Browser rowProvider/materialization fix;
- `43a3358204...` — button-only toolbar menu;
- `f4b4ff06cd...` — partial-cache space widths;
- `06b5d5b3ae...` — clear ligature views when releasing SD font caches.
A recent file-rename feature (`e0fb688bf...`) overlaps capability Nooir already exposes through its web file management and should not be blindly ported to device UI.

**CrossInk:** current main is largely post-1.5.1 maintenance; the major `9656361d...` 1.5.1 release remains the useful feature/fix harvest checkpoint already captured in the roadmap. Notable release notes continue to support investigation of EPUB tables, Arabic/Hebrew UI fitting, Quick Actions, build-size reduction, dictionary/clipping fixes and UTF-8 text-field correctness.

**InkPointX:** current main remains around the August 2026 v2.2.8 line. PDF/cover/focus-reading work remains reference material for later PDF investigation, not something to merge wholesale.

**CrossPDF:** latest repository commits remain August 2026; notable PDF-cache recovery and text-quality work remain relevant when the dedicated PDF prototype begins.

**CrossLink:** recent visible commits are README-only and the code line is comparatively stale. Continue treating it as Bluetooth behavior/reference evidence, especially because it worked on the user's X4, not as an upstream to merge wholesale.

### Audit-stop condition reached

The broad source-only archaeology is now sufficiently complete for 1.6.3 planning. Additional random source scanning is unlikely to rank flash savings reliably.

The next high-value work when build access is available is measurement:
1. reproduce frozen normal `gh_release`;
2. preserve ELF/map/size output;
3. rank hyphenation, fonts, i18n, compressed web assets, WebDAV/mDNS, decoder libraries, TLS/crypto and leaf activities by **linked bytes**;
4. run one-variable A/B builds;
5. record firmware image delta, static RAM, heap/largest block and physical behavior;
6. only then choose removals/externalization.

Source-only work should now resume only for a concrete roadmap feature, a newly discovered upstream change, or a question raised by the linker map.
