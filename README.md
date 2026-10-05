<img width="241" height="402" alt="1" src="https://github.com/user-attachments/assets/4f2836ea-894f-4850-8af2-862581fbaeb4" />
<img width="241" height="402" alt="WhatsApp Image 2026-08-26 at 5 03 55 PM" src="https://github.com/user-attachments/assets/339b083c-b6cc-4243-a337-ed4b8486158a" />
<img width="241" height="402" alt="image" src="https://github.com/user-attachments/assets/5d700bb9-d750-4e31-9cd9-34ce5008da95" />
<img width="240" height="403" alt="2" src="https://github.com/user-attachments/assets/7fcc16d9-1e15-4c8c-ace4-ead43df7bb34" />
<img width="243" height="404" alt="3" src="https://github.com/user-attachments/assets/34d89a00-2bdc-4e59-bfd9-c7a7442c87da" />
<img width="244" height="416" alt="4" src="https://github.com/user-attachments/assets/2f819edf-f09a-4b37-8a51-136402d751ee" />
<img width="241" height="401" alt="5" src="https://github.com/user-attachments/assets/75ee65a0-ccb0-4862-a8a6-b61be1b58329" />
<img width="239" height="416" alt="6" src="https://github.com/user-attachments/assets/362576fa-eb2c-44d0-a519-60b02781dda6" />
<img width="240" height="406" alt="7" src="https://github.com/user-attachments/assets/aa0f3fd7-c5d0-42e1-8f37-2afa92d8e106" />
<img width="239" height="401" alt="8" src="https://github.com/user-attachments/assets/8ed8ec97-3cdb-4094-a130-11c32b3f0936" />
<img width="240" height="401" alt="9" src="https://github.com/user-attachments/assets/0896b309-f380-4667-90f4-36d7f6ef04c4" />
<img width="239" height="403" alt="10" src="https://github.com/user-attachments/assets/c075cd38-3501-4492-bb75-04e490f788fe" />
<img width="241" height="402" alt="WhatsApp Image 2026-08-26 at 5 03 55 PM (1)" src="https://github.com/user-attachments/assets/429968a5-8321-4471-8b40-c3b8e3ce408c" />
<img width="241" height="402" alt="image" src="https://github.com/user-attachments/assets/b9645814-b0fb-4025-a9a8-73d94870481a" />
<img width="241" height="402" alt="image" src="https://github.com/user-attachments/assets/a3816771-c2be-4b3d-b1f1-7f6e356b91d5" />

# Folio Nooir

Latest released version: **[v1.6.3](https://github.com/toshio2011/folio-nooir/releases/tag/1.6.3)**.

Folio Nooir is an experimental, bookshelf-focused custom firmware for XTEINK e-readers. It is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), keeping the strong CrossPoint reader/network foundation while adding a Folio-style bookshelf, richer reading tools, statistics, sleep screens, CBZ support, reader controls, and now memory-aware Bluetooth page-turner support.

The main goal remains simple:

> **Nooir should stay snappy. New features should not make reading slower just because they exist.**

## Hardware warning

> **Check your panel before flashing.** Folio Nooir is developed and physically tested primarily on the maintainer's older XTEINK X4.
>
> X3 and X4 share the same main code path. Newer X3 panels are probed before SPI starts for the UC8279d controller; confirmed results are cached, an explicit override is respected, and an inconclusive probe falls back to the original UC8253 path. X4 keeps the known SSD1677 path by default, with the newer X4 battery-latch handling retained.
>
> Older X3 hardware has community success, but X3 production revisions can differ. **X4 Pro / X4 Classic and other unvalidated hardware variants are not officially supported yet.**
>
> Always keep a known-good recovery image and know how to restore the original firmware before flashing custom firmware.

---

# What's new in 1.6.3

1.6.3 is a larger stability/performance release focused on **Reader memory management, Bluetooth page-turner support, sleep/Wi-Fi lifecycle work, To-Do polish, and keeping Nooir responsive under heavier books**.

## Bluetooth Page Turner — Beta

Bluetooth HID page-turner support is now included in the normal Nooir firmware.

Current support includes:

- **Generic Bluetooth HID** page-turner controls.
- **Yiser J6 Ring** support, including its multi-report button behaviour.
- Bonded reconnect/lifecycle handling.
- A shared configurable **Toggle Bluetooth** action.
- **Turn off if no device connects** with **Never / 30 sec / 60 sec / 90 sec** choices; default is 60 seconds.
- Reader-aware stop/restart behaviour instead of keeping BLE alive at all costs.

### How Bluetooth works in Nooir

X3/X4 devices have limited RAM, and EPUB rendering can temporarily need a large amount of it. Nooir therefore treats Bluetooth as a **secondary service to reading**, not something that is allowed to destabilize the Reader.

In practice:

1. Bluetooth starts only when there is enough safe free memory and a large enough contiguous heap block.
2. Before memory-heavy Reader work such as rendering or chapter/Section transitions, Nooir may temporarily stop Bluetooth.
3. When the page is stable again, Nooir releases only disposable/rebuildable Reader memory and checks whether Bluetooth can safely return.
4. If memory is healthy, the controller reconnects automatically.
5. If memory is still tight, Bluetooth stays off rather than risking a crash, failed render, or unstable Reader.
6. The book remains usable even when Bluetooth cannot reconnect immediately.

So a controller may briefly disconnect during a heavy chapter or page transition. That is intentional.

Bluetooth OFF remains lightweight: the BLE host is not kept running when the feature is disabled.

### Memory-aware BLE recovery

1.6.3 adds bounded Reader-side recovery specifically to help BLE return safely:

- disposable SD-font/glyph caches can be released after a stable render;
- selected fallback UI glyph caches can be reclaimed when useful;
- empty EPUB CSS parser capacity can be released under pressure and rebuilt by the next Section;
- Bluetooth admission checks total free memory and contiguous memory before starting;
- the 36 KiB logical contiguous-memory reserve accounts for ESP-IDF light heap-poisoning reporting overhead;
- lifecycle checks are event-driven/coalesced instead of running a background retry loop.

Active page/Section/rendering state is not globally purged just to make Bluetooth fit.

### Known Bluetooth limitation

After some Wi-Fi sessions, ESP32 heap state may not immediately return to the same healthy level it had before Wi-Fi started. In that case Nooir may intentionally leave Bluetooth off because its memory-safety requirements are no longer met.

A **sleep/wake or restart restores the normal memory state**. Bluetooth/Wi-Fi coexistence and controller compatibility will continue to be improved in future releases.

## Reader / EPUB memory and stability

- Improved Reader lifecycle around rendering and Section transitions.
- Better cleanup of disposable Reader memory after a stable page render.
- Safer handling of large/complex chapters under memory pressure.
- Bounded shared `MemoryPressureReclaimer` instead of a background/global purge system.
- EPUB CSS retained-capacity cleanup after completed Sections when safe.
- Reader-first BLE handoff: heavy Reader work gets memory before Bluetooth.
- Improved Section/cache diagnostics and stability work used during 1.6.3 validation.
- Existing EPUB image/cache work remains included: image-cache warmup, lazy image extraction, invalid-cache cleanup, failure memoization, TJpgDec fallback, compact pixel cache, and reduced unnecessary SD writes.
- Existing Arabic/Bidi and Quran-oriented EPUB behaviour from the 1.6.x line remains preserved.

## Reading performance

- Reduced unnecessary Reader memory retention.
- Disposable font/glyph caches are released only when useful and rebuildable.
- BLE does not repeatedly retry while the Reader is under pressure.
- Simple books stay on the normal fast path; memory work is pressure-driven.
- Bluetooth-disabled users do not keep the BLE host resident.
- Existing large-library optimizations and cover-cache behaviour remain intact.

## To-Do improvements

The on-device To-Do screen has been redesigned while keeping the existing storage/web format compatible:

- clearer Open / Done summary;
- checkbox-style task presentation;
- clearer selected-row state;
- compact priority markers;
- improved empty state;
- Add / Edit / Delete / Reorder / Complete / Clear Completed preserved;
- improved bounded To-Do sleep card;
- Unchecked / Completed / Random / All sleep filters retained.

## Sleep / wake improvements

- Bluetooth is stopped before deep sleep.
- Wi-Fi is stopped before deep sleep.
- active Reader Section work is finalized/cancelled safely;
- temporary Reader resources are released before sleep;
- sleep-image decoding keeps a safe fallback path;
- transparent overlay sleep images remain supported;
- sleep/wake restores a clean memory state for normal Reader/BLE operation.

## Wi-Fi / clock / location cleanup

- One-shot Clock/Location sync paths stop SNTP when finished.
- Wi-Fi teardown and memory diagnostics were tightened.
- Bluetooth Reader reclaim is suppressed while Wi-Fi mode is active.
- Web-session Wi-Fi behaviour remains unchanged where the live session must stay available.
- Further post-Wi-Fi heap recovery work is planned for a future release.

---

# Features

## Folio Nooir bookshelf and library

- Folio Nooir boot logo and bookshelf-focused visual design.
- Three main bookshelf views: **Library**, **Recent**, and **Finished**.
- Library works as a folder/file browser and loads metadata lazily as books are highlighted.
- Fast case-insensitive Library filename search, with optional **Search All Folders** recursive mode.
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading time, and session count.
- Compact **4 × 2 cover grid** with percentage/progress ribbons.
- **3 Covers**, **4 × 2 Grid**, **3-Cover Carousel**, **5-Cover Carousel**, and optional **Spine** shelf layouts where supported.
- Recent and Finished keep independent layout preferences.
- Standalone Carousel theme with its own persisted 3/5-cover setting.
- Carousel uses a dominant selected cover, mirrored/perspective side covers, looping navigation, and avoids duplicate books in small collections.
- Optional **Spine Shelf** for Recent/Finished with bounded deterministic book spines, light grayscale variation, binding details, UTF-8-safe titles, Arabic/Bidi text where renderable, pagination, and matching selection rectangles.
- Featured-cover sizing follows the active shelf geometry.
- Right-aligned battery icon/percentage in Library, Recent, and Finished when battery display is enabled.
- Cover cache warm-up, cache reuse, invalid/blank-BMP recovery, and featured-cover invalidation fixes.
- **Retrieve All Book Details** with streaming progress, resumable missing-thumbnail retrieval, selected-book priority, valid-cache skipping, and **Stop for now** for large libraries.
- Long-press actions for Open, status changes, reset progress, cache refresh, full synopsis, book statistics, and removing a book from the list without deleting the file.
- Automatic movement to Finished at 100% progress.
- Bookmark, clipping, and highlight managers available from home/book actions.
- Context-aware shelf buttons and direct Library/Recent/Finished navigation.
- Library menu access to Clock & Weather, To-Do, Reading Summary, Reading Calendar, bookmarks, clippings, highlights, and metadata retrieval.

## Themes, interface, controls, and settings

- Classic, Lyra, Lyra 3-Cover, Rounded Raff, Folio Nooir, and standalone Carousel themes.
- Persisted display, typography, orientation, refresh, battery, and input settings.
- UI scale controls for menus/reader controls while bookshelf geometry stays fixed.
- Reader Dark Mode.
- Full system-wide dark mode is not currently claimed; dark mode applies to supported reading/UI paths.
- Existing status-bar controls and localization/device configuration from CrossPoint.
- Configurable short/long power actions.
- Separate Reader front-button remapping.
- Side-button layout and long-press actions.
- Reader Options shortcuts.
- Four configurable Reader Quick Action slots, with actions depending on context.
- Optional touch-reader controls on hardware providing the supported touch interface.
- **Settings Profiles** for saving, applying, and deleting named device-setting snapshots without copying reading data.
- **Clear Reading Data** clears Recent entries, Book State records, reading statistics, and Folio shelf snapshot while preserving covers/thumbnails, metadata/book cache files, bookmarks, clippings, and highlights.
- Per-book **Clear Reading Cache** removes generated Reader cache while preserving saved reading position.

## EPUB, XTC/XTCH, TXT and Markdown Reader

CrossPoint's Reader foundation is retained and extended.

- EPUB, XTC/XTCH, TXT, Markdown, and file-browser workflows.
- EPUB chapter navigation, footnotes, bookmarks, go-to-percent, auto page turn, screenshots, orientation control, and custom fonts.
- Improved EPUB CSS handling, HTML tables/cells, images, metadata, and memory safety.
- Optional paragraph indents.
- Improved lists/tables and `<hr>` separators.
- Lightweight strikethrough/redaction handling.
- Reader Guide Dots.
- Large images fitted to the display where possible instead of producing blank/failed image blocks.
- EPUB image-cache warmup and lazy extraction.
- TJpgDec fallback and compact image/pixel cache paths.
- Invalid-cache cleanup and failed-image memoization.
- Reduced Recent/reader SD writes where possible.
- XTC/XTCH cover/page rendering improvements and streaming-oriented page handling to avoid unnecessarily retaining large image planes in RAM.
- Reader font sizes in points.
- Point-based margin controls.
- Fine line-spacing controls.
- Per-book Reader settings.
- Reader Dark Mode.
- Arabic/Bidi rendering support retained from the 1.6.x line.
- Optional **Stable Pages** mode using a compact per-book `stable_pages.bin` map.
- Stable Pages can import compatible CrossInk `META-INF/x-locations.json` data.
- Stable-page preparation is streamed, bounded, cancellable, reusable, and releases temporary memory after completion.

**PDF and FB2 Reader support are not implemented yet.** Repository notes may discuss feasibility, but they are not shipped Reader formats.

## Bluetooth Page Turner — Beta

- Generic Bluetooth HID controller support.
- Yiser J6 Ring preset/multi-report decoder.
- Bonded reconnect.
- Memory-aware Reader handoff.
- Event-driven lifecycle reevaluation.
- Shared **Toggle Bluetooth** action available through supported configurable inputs.
- Configurable no-device auto-off: Never / 30 / 60 / 90 seconds.
- Reader rendering remains higher priority than BLE.
- BLE stays off when memory safety checks fail instead of forcing a risky start.
- Further controller support and Bluetooth/Wi-Fi coexistence improvements are planned.

## Dictionary and text tools

- Offline **StarDict** dictionary support.
- Multiple dictionary folders.
- Preferred dictionary with fast-path reuse.
- Fallback to another prepared dictionary when the preferred source has no match.
- Dictionary history.
- Definition source indicator and preferred/fallback state.
- Lazy Sources picker with up to six successful matching dictionaries.
- Invalid/missing/no-match folders are skipped.
- Dictionary-specific font and font-size controls separate from reading typography.
- **Prepare Dictionary Indexes** screen for one-dictionary-at-a-time indexing.
- Percentage progress, Back-to-cancel, saved Paused state, and resumable checkpoints.
- Downloadable SD-card fonts through **Font Manager**.
- Manual SD-card and web-upload font installation remain supported; see [SD-card font setup](docs/sd-card-fonts.md).
- Continuous word-range selection with held-button navigation.
- Save text clippings/highlights while reading.
- Saved highlights can use black, dark-gray, light-gray, or white backgrounds.
- Bookmark, clipping, and highlight lists support viewing, editing, deletion, and jumping back to the saved book/location where supported.

### Dictionary folder layout

```text
/dictionaries/<folder>/<stem>.idx
/dictionaries/<folder>/<stem>.dict
```

`.dict.dz` is also supported. The hidden `/.dictionaries/<folder>/` root is accepted. Nooir creates rebuildable `.qidx` sidecar indexes; the original dictionary source files are not modified.

Additional compatible dictionaries are available from [CrossInk's dictionary downloads](https://inky.crossink.dev/#downloads).

## CBZ / Manga — experimental

Normal `.cbz` files can be copied directly to the SD card and opened without conversion.

- Direct CBZ reader.
- `ComicInfo.xml` metadata where available.
- Cover/thumbnail caching independent from temporary page caches.
- Bounded archive indexing/extraction for low-memory devices.
- Library / Recent / Finished integration.
- Reading progress and reading statistics.
- Per-book page bookmarks.
- Page Picker.
- **Fit Width** mode.
- **Fit Page** mode.
- **Landscape** mode with scrolling.
- **Zoom** mode with directional panning and held-button page changes.
- **Reset View**.
- LTR and manga-style RTL navigation.
- Cache replay and responsive/read-ahead preparation where safe.
- SD-backed temporary/page caches instead of retaining a full high-resolution decoded page in RAM.
- Web transfer supports optional progressive-CBZ-JPEG normalization when useful; ordinary CBZ transfer remains supported without preprocessing.

Very large/high-resolution comic pages can still be slow on first decode. Fit Width remains the recommended starting mode for normal manga reading.

## Reading statistics and achievements

- Persistent per-book reading time.
- Session count.
- Progress and status.
- Start/finish dates.
- Persistent page-turn counts.
- Daily page counts and pages-per-minute pace.
- Current and best consecutive reading-day streaks.
- **Statistics** screen with Overview, Calendar, Books, and Achievements tabs.
- Today and seven-day activity views.
- Books started/finished and retained reading totals.
- Average-session information.
- Calendar month navigation and selected-day details.
- Monochrome daily reading intensity.
- Books statistics view with cached covers, synopsis, dates, reading time, sessions, pages, progress, and status.
- Daily history retained for up to 730 days.
- Twenty derived achievements with earned/locked state and progress.
- Per-book Statistics from long-press actions.
- Home/Library **Reading Summary**.
- Reading Calendar.
- Finished / Reading / On Hold / New state tracking.
- Reading Stats and Minimal Stats sleep modes.
- Web statistics dashboard and JSON export.

## Web interface and wireless tools

When the device is connected to the same network, the built-in web interface provides:

- Folio Nooir-styled dashboard.
- Bookshelf with covers and progress.
- File browsing.
- Upload/download.
- Rename/move/delete.
- Folder creation.
- Image preview.
- Book metadata editing for title, author, synopsis, status, progress, start date, and finish date without rewriting the original book file.
- Cover management.
- Reading statistics dashboard at `/stats`.
- JSON statistics export with daily pages, streaks, pace, and totals.
- Reset-reading-data action.
- Clock/weather card with editable coordinates, Celsius/Fahrenheit, last-sync state, cached conditions, and one-shot Sync Now.
- Device Clock & Weather page with cached time/date/weather and one-shot refresh.
- To-Do page at `/todo` with quick add/edit/complete/reorder/delete/clear-completed.
- Transfer-page EPUB optimization.
- Optional progressive-CBZ-JPEG normalization.
- Existing CrossPoint web settings, Wi-Fi, OPDS, font, and typography pages.
- Browser-based transfer plus **Calibre wireless transfer**.
- Station and hotspot modes.

## To-Do List

- Persistent storage at `/.crosspoint/todo.json`.
- Add, Edit, Delete, Reorder, Complete, Priority, and Clear Completed actions.
- 1.6.3 device UI with Open/Done summary, checkbox presentation, priority markers, clearer selected state, and improved empty state.
- Matching web To-Do page.
- Sleep-screen To-Do card.
- Sleep filters: Unchecked / Completed / Random / All.

## Sleep and display

Available sleep/display modes include:

- Dark
- Light
- Blank
- Custom
- Cover
- Quick Resume
- Page Overlay
- Cover + Overlay
- Reading Stats
- Minimal Stats
- Clipping + Cover
- To-Do List
- Reading Calendar
- Reading Summary

Additional behaviour:

- Custom PNG/BMP sleep images.
- Random sleep images from `/.sleep/`.
- Transparent PNG Page Overlay preserving the last Reader page underneath.
- Cover + Overlay using the current/recent book cover plus transparent artwork.
- Reading Stats/Minimal Stats bounded cached-cover layouts.
- Clipping + Cover quote cards.
- Reading Calendar/Reading Summary use existing bounded statistics data without network/library scans during sleep.
- Quick Resume and **Resume Reader on Wake** are separate controls.
- Ghosting mitigation and clean refresh behaviour around Reader/sleep transitions.
- Conservative X3/X4 display-driver detection.

### Custom sleep images

Choose **Custom** and use either:

- `/sleep.png` or `/sleep.bmp` for one fixed image; or
- multiple `.png` / `.bmp` files inside `/.sleep/` for randomized sleep images.

If both root files exist, `/sleep.bmp` takes priority.

For Page Overlay / Cover + Overlay, transparent PNG artwork can be placed in `/.sleep/` or `/sleep/`. A fixed `/sleep-overlay.png` (or `overlay.png`) is also supported as a fallback.

## KOReader progress sync

Nooir supports KOReader-compatible progress sync.

Default public server:

```text
https://sync.koreader.rocks:443
```

- Authenticate from **Settings → System → KOReader Sync**.
- Reader Menu → Sync Progress supports **Apply Remote** and **Upload Local**.
- Editable Sync Device Name, defaulting to `Folio Nooir X4`.
- Filename matching is the portable default and requires matching filenames.
- Binary matching uses KOReader-compatible partial-MD5 identity and requires identical book files.
- CrossPoint servers can retain richer CrossPoint position data; generic KOReader servers receive standard KOReader fields.
- Portable XPath/percentage mapping is attempted before richer page/paragraph fallback mapping.
- Nooir ↔ KOReader interoperability has been physically confirmed.

## Network, sync, OPDS, and updates

- Wi-Fi setup.
- OPDS browsing.
- Browser transfer.
- Calibre wireless transfer.
- KOReader Sync.
- Clock & Weather one-shot sync.
- Location/coordinate configuration for weather.
- GitHub-release OTA support.
- SD-card firmware update.
- Recovery tools inherited from the CrossPoint foundation.

### Over-the-air updates

Nooir checks:

```text
https://github.com/toshio2011/folio-nooir/releases/latest
```

A compatible GitHub release should contain an asset named exactly:

```text
firmware.bin
```

## Native simulator

PlatformIO profiles are available for:

- `simulator_x4`
- `simulator_x3`

They exercise the shared Nooir UI, bookshelf, Carousel, Library, EPUB rendering, and navigation without flashing a physical device. The X3 profile also includes simulated tilt testing.

There is no official X4 Pro simulator. WSL is the supported simulator workflow where SDL2 is available. See [docs/simulator.md](docs/simulator.md).

## Supported / retained CrossPoint workflows

Folio Nooir is a feature/interface layer on top of CrossPoint, not a replacement of its foundation. Retained workflows include:

- EPUB / XTC / XTCH / TXT / Markdown reading.
- File browser and image preview.
- EPUB chapter navigation, footnotes, bookmarks, go-to-percent, auto page turn, screenshots, orientation, and fonts.
- Wi-Fi, web transfer, hotspot/station mode, Calibre wireless, OPDS, KOReader Sync, OTA.
- Sleep/battery/status screens and SD-card update/recovery paths.
- Existing themes/settings/input infrastructure.
- X3 tilt-page-turn path where supported.

---

# Installation

1. Download the latest `firmware.bin` from the repository's [Releases](https://github.com/toshio2011/folio-nooir/releases) page.
2. Keep a copy of your currently working firmware/recovery image.
3. Open the CrossPoint web flasher and select the custom firmware option.
4. Choose the Folio Nooir `firmware.bin` and flash only hardware you can recover if necessary.

Custom firmware is provided without warranty. Flash at your own risk.

## SD-card update

The SD-card firmware picker accepts a file named exactly:

```text
firmware.bin
```

in the SD-card root. Reading position/book data are stored separately from the firmware image.

---

# Building

Folio Nooir uses PlatformIO.

Development build:

```powershell
python scripts/build_html.py
.\.venv\Scripts\pio.exe run -e default
```

Development firmware:

```text
.pio/build/default/firmware.bin
```

Release build:

```powershell
python scripts/build_html.py
.\.venv\Scripts\pio.exe run -e gh_release
```

Release firmware:

```text
.pio/build/gh_release/firmware.bin
```

---

# Current known limitations / future work

- **Bluetooth Page Turner remains Beta.** More controllers and cleaner mapping/setup are planned.
- Post-Wi-Fi memory recovery can temporarily prevent Bluetooth from returning until sleep/wake or restart.
- CBZ is still experimental and high-resolution pages can be slow.
- PDF/FB2 Reader support is not shipped.
- X4 Pro / X4 Classic are not officially supported yet.
- More EPUB/CSS compatibility, CBZ performance, Bluetooth/Wi-Fi coexistence, and X3/X4 physical validation are planned.

---

# Credits and license

Folio Nooir is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), with display and reader foundations from the CrossPoint contributors.

It uses the [FreeInk SDK](https://github.com/toshio2011/freeink-sdk) for device and Reader support, and acknowledges the open-source [CrossInk](https://github.com/uxjulia/CrossInk) project as a reference for compatible XTEINK display, sleep-screen, input, and Reader improvements.

Licensed under the MIT License.
