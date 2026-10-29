# Folio Nooir 📚

**A bookshelf-first, reading-focused custom firmware for XTEINK X3/X4 devices.**

[**Download v1.6.6**](https://github.com/toshio2011/folio-nooir/releases/tag/1.6.6) · [Releases](https://github.com/toshio2011/folio-nooir/releases) · [Installation](#installation) · [Full features](#complete-feature-list) · [Simulator](#native-simulator)· [MihonNooir](https://github.com/toshio2011/mihon-nooir)

**Latest release: v1.6.6 — 29 October 2026.**

Folio Nooir is a personal, experimental fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). It retains its reader and wireless foundations while adding a Folio-style bookshelf, more reading tools, comics, statistics, sleep screens, and optional Bluetooth page turners.

> **Nooir should stay snappy. New features should not make reading slower just because they exist.**

## Screenshots

<img width="241" height="402" alt="1" src="https://github.com/user-attachments/assets/4f2836ea-894f-4850-8af2-862581fbaeb4" />
<img width="241" height="402" alt="WhatsApp Image 2026-08-26 at 5 03 55 PM" src="https://github.com/user-attachments/assets/339b083c-b6cc-4243-a337-ed4b8486158a" />
<img width="241" height="402" alt="image" src="https://github.com/user-attachments/assets/5d700bb9-d750-4e31-9cd9-34ce5008da95" />
<img width="240" height="403" alt="2" src="https://github.com/user-attachments/assets/7fcc16d9-1e15-4c8c-ace4-ead43df7bb34" />

<details>
<summary>More Folio Nooir screenshots</summary>

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

</details>

## ✨ What's new in v1.6.6

Another "just one more fix" that somehow became a big update. HAHAHA. 😆 This release is mostly about making Nooir's existing features work better without piling on heavy background work.

### 📚 Folio Lite, Home and shelf performance

- **Folio Lite** adds **3-cover Carousel, 5-cover Carousel, Spine, 2-row Spine and 3-row Spine**, with separate Recent/Finished preferences. **The original Main Folio Nooir theme is preserved.**
- Improved selected-book title/author/synopsis spacing, status placement and layout geometry.
- Faster targeted updates for unchanged Home shelves and selected books, with reusable SD-backed frame snapshots.
- Better behavior when Recent books are reordered, popups close, or you return from Settings or a book.
- Normal Home browsing favors fast refresh; periodic full ghost cleanup can be deferred to the actual sleep display boundary. Explicit orientation resync still does a full refresh.

### 📖 XTC / XTCH reading and covers

- **Streamed, bounded page rendering** for XTC (1-bit) and XTCH (2-bit grayscale).
- More reader controls: **page and percent navigation, bookmarks, auto-turn, reading direction, screenshots and Quick Actions**.
- **Page-based KOReader Sync** with a deliberate local/remote progress choice.
- XTCH grayscale cover/thumbnail processing uses bounded streaming buffers instead of allocating a full-size image page.
- Persistent, validated book metadata caching improves Library revisit behavior. Optional synopsis/series metadata is available when present in the source; ordinary XTC/XTCH files are still supported.
- More reliable manual metadata/cover refresh that preserves reading progress and bookmarks.

### 🔎 Library and book metadata

- Book-detail preparation waits for a **1.5-second stationary selection**, rather than decoding on each keypress.
- Better first-open cover/metadata preparation for **EPUB, CBZ, XTC and XTCH**.
- Missing or incomplete BMP thumbnails are detected and safely regenerated; already valid covers are reused.
- **Retrieve All Book Details** now uses streaming work queues and clearer cache-hit, completion and failure outcomes.
- Thumbnail/metadata repair avoids resetting book progress, bookmarks and clippings.

### 🕹️ Bluetooth page turner — still experimental

- More reliable BLE activity transitions, remote navigation and input-state cleanup.
- Reader work takes priority when a page or EPUB section needs scarce RAM; BLE may stop temporarily and return after safe memory recovery.
- To avoid endless reconnect/disconnect cycles, **three memory-survival stops within the same EPUB** suspend automatic BLE readmission until the book is exited.
- Existing BLE memory admission floors and protections are preserved. Bluetooth OFF does not keep a host running.

**Known behavior:** some unusually heavy EPUB pages still trigger Bluetooth shutdown during font preparation. The Reader remains usable; this is a safety measure, not a promise of uninterrupted BLE.

### ⚙️ Settings, Reader and general polish

- Better grouped Settings, keyboard navigation, and a read-only Device Info screen.
- Avoid redundant writes for unchanged settings, while retaining verified temporary writes and backup/recovery for changed data.
- EPUB section builds can pause under memory pressure, keep completed pages cached on SD, release temporary parser/CSS/build memory and resume later.
- CBZ cache recovery, sleep-cover safety, font/render memory handling and Reader-exit persistence diagnostics have been refined.

Read the [v1.6.6 release notes](https://github.com/toshio2011/folio-nooir/releases/tag/1.6.6) or the detailed [1.6.6 development checkpoint](https://github.com/toshio2011/folio-nooir/blob/codex/nooir-x4-integration/docs/1.6.6-RESUME.md).

**Testing:** the release candidate passed **580 C++ host tests and 135 Python source contracts** and was exercised on the maintainer's original unlocked X4. This does not guarantee every device, book, BLE controller, bulk-library workflow or OTA path; some physical validation is still pending.

## ⚠️ Hardware compatibility and flashing safety

> **Back up your current working firmware and reading data.** Keep a known-good recovery method before flashing.
>
> - **Older/original XTEINK X4:** primary physical development and test device.
> - **XTEINK X3:** shared firmware path, with community success, but panel revisions differ and not all have been physically verified.
> - **X4 Pro, X4 Classic and S3:** **not officially supported.** Do not assume compatibility from the model name.
> - Bluetooth and CBZ remain experimental, and extremely complex EPUBs can still require additional memory recovery.

The shared X3/X4 display path includes UC8279d detection with UC8253 fallback for newer X3 panels and the familiar SSD1677 X4 path. These protections do not certify untested hardware revisions.

---

# Complete feature list

## Folio Nooir bookshelf and library

- Folio Nooir boot logo and bookshelf-focused visual design.
- Three main bookshelf views: **Library**, **Recent**, and **Finished**.
- Library works as a folder/file browser and loads metadata lazily as books are highlighted.
- Fast case-insensitive Library filename search, with optional **Search All Folders** recursive mode.
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading time, and session count.
- Compact **4 × 2 cover grid** with percentage/progress ribbons.
- **Main Folio Nooir:** familiar 4 × 2 grid plus available cover, Carousel and Spine layouts.
- **Folio Lite:** 3-cover and 5-cover Carousel, Spine, 2-row Spine, and 3-row Spine while keeping the original Main theme intact.
- Recent and Finished keep independent layout preferences, including all five Lite layouts.
- Standalone Carousel theme with its own persisted 3/5-cover setting.
- Carousel uses a dominant selected cover, mirrored/perspective side covers, looping navigation, and avoids duplicate books in small collections.
- Optional **Spine Shelf** for Recent/Finished with bounded deterministic book spines, light grayscale variation, binding details, UTF-8-safe titles, Arabic/Bidi text where renderable, pagination, and matching selection rectangles.
- Featured-cover sizing follows the active shelf geometry.
- Right-aligned battery icon/percentage in Library, Recent, and Finished when battery display is enabled.
- Cover cache warm-up, cache reuse, invalid/blank-BMP recovery, and featured-cover invalidation fixes.
- **Retrieve All Book Details** with streaming metadata/thumbnail processing, cache reuse, outcome/failure counters, selected-book priority and **Stop for now** for large libraries.
- 1.5-second stationary-selection metadata preparation for incomplete books, plus safe first-open EPUB/CBZ/XTC/XTCH cover repair.
- Persistent source-checked XTC/XTCH metadata records (title, author, optional synopsis/series and cover state), independent of Recent.
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
- Optional SD-card interface font families, selected independently from Reader
  typography.
- Organized Settings with collapsible groups, keyboard-navigation improvements, and read-only **Device Info**.
- Change-aware Settings saves, plus validated temporary files, backup and recovery for damaged or incomplete settings.
- **Settings Profiles** for saving, applying, and deleting named device-setting snapshots without copying reading data.
- **Clear Reading Data** clears Recent entries, Book State records, reading statistics, and Folio shelf snapshot while preserving covers/thumbnails, metadata/book cache files, bookmarks, clippings, and highlights.
- Per-book **Clear Reading Cache** removes generated Reader cache while preserving saved reading position.

## EPUB, XTC/XTCH, TXT and Markdown Reader

CrossPoint's Reader foundation is retained and extended.

- EPUB, XTC/XTCH, TXT, Markdown, and file-browser workflows.
- EPUB chapter and table-of-contents navigation, in-section anchors, footnotes,
  bookmarks, go-to-percent, auto page turn, screenshots, orientation control,
  and custom fonts.
- Improved EPUB CSS handling, HTML tables/cells, images, metadata, and memory safety.
- Reader text settings include paragraph alignment, embedded EPUB style control,
  optional language-aware hyphenation, and Focus Reading, which emphasizes the
  opening part of regular body words.
- Optional paragraph indents.
- Improved lists/tables and `<hr>` separators.
- Lightweight strikethrough/redaction handling.
- Reader Guide Dots.
- Large images fitted to the display where possible instead of producing blank/failed image blocks.
- EPUB image-cache warmup and lazy extraction.
- TJpgDec fallback and compact image/pixel cache paths.
- Invalid-cache cleanup and failed-image memoization.
- Reduced Recent/reader SD writes where possible.
- XTC/XTCH cover/page rendering improvements and bounded streaming to avoid unnecessarily retaining large image planes in RAM.
- EPUB completed section layouts are cached on SD; memory-heavy section builds can suspend, release temporary parser/CSS state and resume.
- Individual cached EPUB pages are loaded for rendering; previous Page objects are not retained as an in-RAM page collection.
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

## XTC / XTCH reader (1-bit / 2-bit)

These are device-friendly comic/image formats that Nooir can read directly.

- XTC uses 1-bit pages; XTCH supports two-plane 2-bit grayscale.
- Bounded streaming and render memory checks, including skipping optional XTCH snapshot buffers when RAM is too tight.
- Page/percent and supported chapter navigation, bookmarks, auto-turn, LTR/RTL direction, fit controls, screenshots and format-specific Reader Quick Actions.
- Cached covers/thumbnails, including streamed XTCH grayscale reduction and bitmap validity checks.
- Persistent validated metadata caching and optional embedded series/synopsis, without changing compatibility with ordinary XTC/XTCH files.
- Manual cache refresh keeps reading progress and bookmarks.
- **KOReader Sync** uses one-based page progress and an explicit local/remote choice, not automatic background sync.

XTC/XTCH rendering does not use the EPUB font-prewarm pipeline.

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
- Configurable remote directional/button mapping with press, repeat, release and held-state handling, plus Yiser J6 input decoding.
- In heavy EPUBs, a third actual memory-survival stop latches automatic BLE return until the book exits; XTC/XTCH readers do not inherit this EPUB-specific latch.
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
- WebDAV-compatible file access, HTTP upload/download, and WebSocket upload
  progress alongside the browser file manager.
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
- Fast Home waveform for ordinary navigation, with deferred full ghost cleanup at the actual final sleep display boundary when due.
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
- Nooir ↔ KOReader interoperability is established for the existing EPUB workflow.
- **XTC/XTCH in v1.6.6:** content-based identity and one-based page progress, with manual local/remote choice and atomic progress saving. Further real-device compatibility testing is recommended.

## Network, sync, OPDS, and updates

- Wi-Fi setup.
- OPDS browsing.
- Browser transfer.
- Calibre wireless transfer.
- KOReader Sync.
- Clock & Weather one-shot sync with cached time/date/weather and NTP lifecycle
  handling.
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

1. Download [v1.6.6 `firmware.bin`](https://github.com/toshio2011/folio-nooir/releases/download/1.6.6/firmware.bin), or choose another version from the [Releases](https://github.com/toshio2011/folio-nooir/releases) page.
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

Folio Nooir uses PlatformIO. **The current repository default branch is `codex/folio-nooir`; the v1.6.6 firmware source is on the [`1.6.6` release tag](https://github.com/toshio2011/folio-nooir/tree/1.6.6) and [`codex/nooir-x4-integration` branch](https://github.com/toshio2011/folio-nooir/tree/codex/nooir-x4-integration).** Check out the tag if you want to build the exact released source:

```bash
git fetch --tags
git checkout 1.6.6
git submodule update --init --recursive
```

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

- **Bluetooth Page Turner remains Beta.** Heavy EPUB font prewarming may temporarily stop BLE. The three-survival-stop latch leaves BLE off until book exit when memory pressure keeps recurring. Wider controller and hardware validation remains necessary.
- Post-Wi-Fi BLE recovery remains under development and can be deferred while
  either memory safety floor is unmet.
- CBZ is still experimental and high-resolution pages can be slow.
- PDF/FB2 Reader support is not shipped.
- X4 Pro / X4 Classic are not officially supported yet.
- Occasional 1–2 px Folio Lite header residue remains a deferred visual polish item.
- Complete mixed-format Retrieve All, some reboot/sleep and OTA scenarios, and broader X3/X4 hardware revisions still need physical validation.
- More EPUB/CSS compatibility, CBZ performance, Bluetooth/Wi-Fi coexistence, and X3/X4 physical validation are planned.

---

# Credits and license

Folio Nooir is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), with display and reader foundations from the CrossPoint contributors.

It uses the [FreeInk SDK](https://github.com/toshio2011/freeink-sdk) for device and Reader support, and acknowledges the open-source [CrossInk](https://github.com/uxjulia/CrossInk) project as a reference for compatible XTEINK display, sleep-screen, input, and Reader improvements.

Licensed under the MIT License.
