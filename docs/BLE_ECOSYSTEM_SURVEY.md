# XTEINK BLE / Input / Resource Ecosystem Survey

Status: **broad ecosystem reconnaissance; implementation reference, not implementation authorization**  
Survey date: 2026-09-20  
Companion deep-dive: [BLE_REMOTE_RESEARCH.md](BLE_REMOTE_RESEARCH.md)

## Why this exists

The first BLE research pass focused too narrowly on the obvious Bluetooth forks. The XTEINK ecosystem is now large enough that this is risky: a firmware that does not advertise "Bluetooth page turner" may still contain a better radio lifecycle, heap strategy, input abstraction, sleep policy, reconnect guard, fixed allocator, CPU policy, diagnostic harness, or transfer design that matters to a future Nooir BLE implementation.

This survey therefore uses the current ReadMe.club firmware catalog as a **discovery index**, then broadens beyond it with GitHub/web searches for XTEINK-specific BLE, HID, NimBLE, memory, power and radio work. The catalog reported **47 entries** at the time of this survey. One visible pair, `INX` / `Inx`, appears to be duplicate naming/listing rather than two independently proven codebases; catalog count is retained rather than silently "fixing" it.

**Screened does not mean line-by-line audited.** Every catalog entry is screened for BLE/input/resource relevance. Repositories with direct BLE or unusually strong memory/power evidence are then source-audited more deeply. Entries with no BLE evidence are still retained because negative/scope evidence and non-BLE resource ideas are useful.

## 1. Ecosystem-wide conclusions

### 1.1 There are several distinct BLE problem families

Do not compare all BLE firmware as if they solve the same problem.

1. **BLE HID central / remote input** — XTEINK connects to a page turner/keyboard. CrumBLE, CrossPoint Reader BLE lineages, the X3 BLE fork and SUMI are the strongest current examples.
2. **BLE peripheral / companion service** — phone connects to XTEINK. Flowe is important here; it has GATT-server, advertising, bonding/security, file/phone companion and explicit BLE shutdown/resume around memory-heavy operations.
3. **BLE scanner / radio utility** — biscuit and ShortBread expose BLE scanning/tooling. These are useful for radio arbitration and diagnostics even when not reader-HID implementations.
4. **BLE keyboard/peripheral experiments** — biscuit also contains BLE keyboard emulation; useful for understanding the opposite HID direction, descriptors and radio ownership, but not directly reusable as a HID host.
5. **No-BLE minimalist baselines** — Microreader/Lector and similar projects are useful controls for what memory/power/performance look like when radio complexity is intentionally absent.

### 1.2 The best future Nooir solution will probably be assembled from ideas, not copied from one fork

High-value patterns found across the ecosystem:

- **virtual/local input injection** from CrossPoint Reader Enhanced: BLE input enters the same input path as physical buttons instead of teaching each screen about BLE;
- **complete/bounded callback handoff** from CrumBLE/X3 BLE work: NimBLE callbacks should latch/copy fixed data and let the main loop do logging/UI/action work;
- **HID report monitor / learn mode** from Enhanced/X3 BLE lineages: crucial for unknown remotes;
- **central-only stack trimming + MTU 23** for HID-only operation from Enhanced/Foulad's in-tree `BleKeyboardHost` research;
- **free heap + largest contiguous block gates**, not free heap alone, from CrumBLE's field history;
- **crash/reconnect guard files and bounded workers** from the X3 BLE fork;
- **normal-CPU lock only around controller init/deinit/critical workers** from X3 BLE/Foulad findings;
- **radio auto-suspend and lower TX power** from Enhanced;
- **reader-only / screen-scoped / task-scoped BLE policies** from CrumBLE and Foulad as alternatives to always-resident BLE;
- **fast-to-slow advertising demotion** from Flowe's peripheral role: fast discovery/reconnect window, then low-duty advertising;
- **static callbacks and leak-aware BLE resume** from Flowe;
- **shutdown BLE before memory-heavy transfer, then restore it** from Flowe;
- **remove Wi-Fi entirely to buy BLE/plugin memory** from SUMI — too drastic for Nooir, but valuable as an upper-bound experiment;
- **preallocated memory arenas / bump allocation** from SUMI to resist BLE-era heap fragmentation;
- **Wi-Fi/BLE radio arbitration** from biscuit and several BLE forks;
- **dynamic CPU / automatic light-sleep / interrupt-driven input** from CrossDiTo as a power reference even though it is not primarily a BLE project;
- **allocate network buffers before the TLS low point** from CrossInk-Bookorbit: a general lesson for radio + constrained-heap transitions;
- **zero-copy / SD / mmap approaches** from eenk and other minimal projects as examples of moving pressure out of heap;
- **ESP-NOW as an alternate wireless design** from Duet: not a page-turner solution, but useful when comparing radio lifecycle and device-to-device transfer architecture.

### 1.3 A swipe-like remote strengthens the case for descriptor-aware capture

Existing BLE page-turner forks tend to optimize for known keyboard/consumer-style keycodes and profiles. The X3 BLE fork and CrumBLE inspect report maps only heuristically (keyboard vs Consumer Usage Page) and then search short reports for known codes. That is good compatibility engineering for known remotes, but it is not enough evidence that mouse/pointer/wheel/vendor multi-report gesture remotes are captured faithfully.

Nooir should preserve the broader target from BLE_REMOTE_RESEARCH.md:

```text
report characteristic + report ID + descriptor context
    -> decoded keyboard / consumer / pointer / wheel / vendor input
    -> bounded sequence recognizer
    -> normalized trigger
    -> mapping
    -> Nooir action
```

Unknown remote support should be based on observed reports/sequences, not device-name branching alone.

## 2. Deep source findings worth preserving

### 2.1 CrossPoint Reader Enhanced — especially valuable

Repository: `Rballesteros/crosspoint-reader-enhanced`.

The fork documents a vendored `NimBLE-Arduino-enhanced` because ordinary configuration alone did not solve ESP32-C3 init/reconnect problems. Important ideas:
- central-only role;
- minimum ATT MTU for HID;
- controller-memory reserve can be compile-time selectable;
- if controller memory is released at boot, later BLE enable may require reboot;
- virtual-button injection gives BLE the same behavior surface as physical input;
- key learning includes Confirm/Back, not just page forward/back;
- in-app HID report monitor;
- active scans stop when leaving Bluetooth UI;
- radio can auto-suspend after idle when disconnected;
- full BLE teardown before deep sleep;
- lower TX power than default;
- Wi-Fi/OTA paths pre-disable BLE;
- heap-aware fallbacks in unrelated reader subsystems when BLE has reduced available memory.

**Nooir study:** compare its virtual-input seam against Nooir's current InputManager/ButtonNavigator/action dispatch. The goal is semantic reuse without blindly copying its BLE manager.

### 2.2 X3 BLE page-turner fork — reconnect/lifecycle laboratory

Repository: `hannah-nula/crosspoint-x3-ble-page-turner`; audited documentation and manager source.

Notable behavior:
- NimBLE-Arduino 2.5.0;
- finite Pair New Remote worker rather than an open-ended scanner;
- Free2/Free3/GameBrick/mini keyboard/IINE/Kobo name/profile hints;
- learned mappings;
- saved address **and address type**;
- broad HID report-characteristic discovery;
- virtual-button injection;
- persisted reconnect diagnostics;
- reader sleep/wake marker;
- Free3 remote sleep/off/on recovery;
- normal-CPU lock while scan/connect/subscribe worker is active;
- reconnect work explicitly excluded from "recent user activity" so it does not prevent device auto-sleep;
- crash-loop guard after panic/watchdog/CPU-lockup;
- manual recovery remains available when automatic recovery is guarded;
- hardware validation on an X3 + Free3 lineage, including repeated reader sleep/wake and remote off/on cycles.

**Nooir study:** the reconnect state machine and guard design are more valuable than its byte-scanning HID decoder.

### 2.3 CrumBLE — memory/teardown failure history

Repository: `imshentastic/CrumBLE`; audited at `3c212bb5...` in the first dossier.

Carry forward:
- check free heap **and max/largest alloc** before controller init;
- NimBLE pool/controller trims can materially change start cost;
- teardown/destructor behavior can crash on an active real remote;
- same-boot controller memory release can be one-way depending on integration;
- retry must be rate-limited and bounded;
- image-heavy reader + BLE exposed heap failures in unrelated font/render paths;
- dictionary lookup now auto-disables BLE to free heap and reconnects afterward;
- reader-only BLE is a viable policy when always-resident memory is unsafe.

**Nooir study:** reproduce the failure classes in tests; do not inherit its current numeric gates without measuring Nooir.

### 2.4 SUMI — radically different memory budget strategy

Repository: `ezuroski/SUMI`.

SUMI explicitly removes Wi-Fi because it reports Wi-Fi consuming roughly 100 KB and fragmenting heap in its build. It uses BLE for keyboard/page-turn input and file transfer. Its README documents:
- an 82 KB preallocated memory arena split across reusable regions;
- bump allocation for temporary layout work;
- streaming parsers;
- one content provider at a time;
- cache-to-flash/SD strategy;
- BLE-only connectivity.

**Nooir study:** do **not** remove Wi-Fi, but benchmark the principle: reserve reusable large working buffers before BLE can fragment heap, time-share buffers between mutually exclusive jobs, and prefer deterministic scratch regions over repeated large malloc/free churn.

### 2.5 Flowe — BLE peripheral, advertising and radio lifecycle

Repository: `andrewjiang/flowe-os`, previously audited at `b1561925...`.

Flowe is not a HID page-turner reference; it is valuable because it operates XTEINK as a BLE peripheral/phone companion.

Source findings:
- UI first, radio second;
- bonded/security-aware GATT server;
- function-static callbacks to avoid leaking user callback allocations across begin/deinit cycles;
- comments explicitly track small NimBLE init/deinit leaks;
- fast advertising 30–60 ms for a 60 s grace window, then 400–500 ms slow advertising;
- BLE shutdown before transfer, with memory probes before/after;
- Wi-Fi teardown/reclamation before radio transitions;
- task stack probes;
- connection/security work deferred/managed across host/main task boundaries.

**Nooir study:** even if Nooir initially ships only HID-central, Flowe provides strong patterns for power-aware discovery windows, radio state transitions, leak detection and measurement instrumentation.

### 2.6 Foulad/Midad BLE research — valuable corrections and thin-stack ideas

Repository: `sfoulad/midad-by-foulad`, historical/current BLE planning docs at audited commit `ef3b455c...`.

Important findings/ideas:
- recognizes central HID and peripheral companion as separate roles/tracks;
- in-tree FreeInk `BleKeyboardHost` already provides HID-central, bonding and page-key abstractions;
- minimum MTU 23 for HID is deliberate to avoid larger GATT buffers;
- init/deinit must occur at normal CPU frequency;
- recommends fixed-size/stack buffers and avoiding `std::string`/`std::vector` churn in BLE hot paths;
- later hardware findings moved BLE from an always-background idea to screen-scoped lifecycle in that project;
- explicit "paused/low memory" thinking is useful even if Nooir uses a different UI;
- separates starting heap gates by workload/state rather than pretending one number fits all.

**Caution:** some values in the historical planning doc are borrowed/estimated and explicitly marked as needing local measurement. Treat architecture lessons as stronger evidence than numeric thresholds.

### 2.7 biscuit / ShortBread — radio arbitration and diagnostic breadth

`yattsu/biscuit` advertises Wi-Fi and BLE tools, including BLE scan and BLE HID keyboard emulation, and its architecture states Wi-Fi/BLE radio use is arbitrated by a `RadioManager`. ShortBread inherits a lean subset and includes a BLE scanner.

**Nooir study:** inspect `RadioManager` before designing a Nooir Wi-Fi/BLE ownership mechanism. A single owner/state machine is preferable to every activity independently starting/stopping radios.

### 2.8 CrossDiTo — power architecture even without page-turner focus

CrossDiTo reports dynamic CPU operation, automatic light sleep, interrupt/deadline-driven input, lower-power e-ink waits, retained next-page work and less Home rebuilding.

**Nooir study:** BLE battery cost must be evaluated inside the whole-device power policy. A radio that is efficient while the CPU is needlessly awake is still a bad result.

### 2.9 CrossInk-Bookorbit — constrained network allocation ordering

Release notes document allocating a transfer buffer **before** the TLS handshake rather than at the post-handshake heap low point.

**Nooir study:** order of allocation is part of memory design. For BLE/Wi-Fi handoff, allocate critical deterministic buffers before entering a known fragmented/low-heap phase where appropriate.

### 2.10 Microreader / Lector — intentional no-BLE controls

Microreader explicitly says no Wi-Fi/Bluetooth/sync. Lector explicitly says GPIO-only, no BLE input.

**Nooir study:** use minimalist firmware as conceptual controls: what responsiveness, boot/sleep simplicity and heap behavior are available when radios are absent? This helps quantify the cost of BLE rather than treating the BLE build in isolation.

## 3. Catalog-wide screening — all 47 ReadMe.club entries

Legend:
- **BLE-direct**: direct BLE implementation or explicit BLE feature worth source audit.
- **Resource/power**: not necessarily HID, but relevant architecture.
- **Baseline/inherited**: likely shares upstream behavior; no distinct BLE evidence found in this screening.
- **Format/UI/app**: useful elsewhere, not a primary BLE source.
- **Gap**: public evidence insufficient; retain as a follow-up target.

| # | Catalog entry | BLE/resource relevance from this survey | What to keep studying |
|---:|---|---|---|
| 1 | Official Xteink | BLE-direct but closed-source/opaque | Physical behavior baseline: pairing UX, supported remotes, sleep/wake, battery, swipe-like remote behavior; black-box captures only |
| 2 | CrossPoint | Resource/baseline | Track upstream NimBLE/FreeInk changes and PRs; radio/power/input infrastructure |
| 3 | CrossInk | Resource/baseline | RAM tuning, input model, FreeInk evolution; normal build previously ignored BLE library |
| 4 | biscuit. | BLE-direct / radio utility | RadioManager, BLE scan, HID-peripheral descriptors, Wi-Fi/BLE arbitration, diagnostics |
| 5 | Papyrix | Resource/format | Memory-light reader, theme/font loading, base inherited by SUMI |
| 6 | INX | UI/resource | Input abstraction, UI task/render patterns; no distinct HID-central evidence established |
| 7 | Inx | Catalog duplicate/alias candidate | Confirm whether this is duplicate listing before treating as independent evidence |
| 8 | CPR-vCodex | Resource/stats | Memory cost of large feature surface; input/action abstractions |
| 9 | CrossPet | Gap / lineage | Original public project is not enough to assume BLE; separate BLE fork lineage exists and should be audited by exact repo/ref |
| 10 | Microreader | No-BLE control | Minimal radio-free performance/memory baseline; one-time conversion strategy |
| 11 | SUMI | BLE-direct | BLE keyboard + page turner + transfer, Wi-Fi removal, arenas/bump allocator, plugin coexistence |
| 12 | TernOS | Alternative architecture | Rust/converted-content approach; resource ownership and sleep model; no HID evidence established |
| 13 | InkPoint X | Resource/format | On-device PDF/FB2 prep, cache architecture, broad feature pressure; no distinct BLE HID evidence established |
| 14 | CrumBLE | BLE-direct | Highest-priority memory/teardown/reconnect case study |
| 15 | AALU | UI/resource | Quick settings/library/stats under 380 KB; no distinct BLE evidence established |
| 16 | YACP | Gap | Screen source for BLE/radio/input terms before implementation; no distinct evidence established in broad search |
| 17 | vCodex Steroids | Resource | Large feature surface under C3 constraints; no distinct BLE evidence established |
| 18 | CrossPoint Apps | Resource/apps | Offline-first caching and feature modularity; no distinct BLE evidence established |
| 19 | CrossPlay | Alternative wireless/input | Device-to-device/game/app architecture; X4 Pro touch/input work; determine exact radio transport by ref before borrowing |
| 20 | Flowe | BLE-direct peripheral | GATT server, advertising power policy, BLE shutdown/resume, leak/task probes |
| 21 | CrossInk-Bookorbit | Resource/network | Buffer allocation ordering, transfer/TLS low-heap handling |
| 22 | CrossPoint Reader BLE | BLE-direct | Pair/learn/virtual-input lineage; compare exact repo/ref with Enhanced/X3 fork/CrumBLE |
| 23 | eenk | Resource architecture | mmap/flash-backed large data, minimal updater partition; useful memory-pressure techniques |
| 24 | Matcha Reader | Format/input | Vertical text/dictionary/manga; complex reader workload to include in BLE coexistence tests if concepts port |
| 25 | Foulad eInk | BLE-direct/research | FreeInk BleKeyboardHost, state-scoped BLE, normal-frequency init/deinit, fixed-buffer guidance |
| 26 | Snapix | Performance/resource | IRAM hot paths, failure blacklisting, SD recovery, O2/LTO tradeoffs |
| 27 | Duet | Alternative wireless | ESP-NOW/serverless sync; compare radio lifecycle/power and serialization with BLE/Wi-Fi |
| 28 | Casper | BLE-direct candidate | Public community evidence says pre-release BT page-turn support exists; exact source/ref needs dedicated audit |
| 29 | AvesO3 | Network/app | AO3 workflow/cache pressure; no distinct BLE evidence established |
| 30 | ShortBread | BLE-direct scanner | Lean biscuit/CrossInk blend; BLE scanner and inherited radio-management ideas |
| 31 | Folio Nooir | Target | Establish measured baseline; no BLE implementation yet |
| 32 | nous | Gap/resource | Inspect memory/input/radio differences; no distinct BLE evidence established |
| 33 | CrossNotes | Network/input | Annotation/keyboard workflow; hotspot server; no distinct BLE evidence established |
| 34 | SEEK Reader | Resource/dictionary | Memory-safe dictionary patterns; CrumBLE ports dictionary behavior and suspends BLE during lookup |
| 35 | Crossink Pokemon | App/input | Input loop and feature coexistence; no distinct BLE evidence established |
| 36 | CrossPlant | App/power | Long-lived/pet-style background state may reveal wake/persistence discipline; no distinct BLE evidence established |
| 37 | CrossPDF | Resource stress | PDF reflow is a strong worst-case heap workload for future BLE coexistence testing |
| 38 | Ruby | Gap | No distinct BLE/resource evidence established in broad search; retain source-screening task |
| 39 | SraightPoint Reader | Gap | No distinct BLE evidence established; verify repo identity and input changes |
| 40 | CrossDiTo | Power-direct | Dynamic CPU, auto light sleep, interrupt/deadline input, e-ink wait power |
| 41 | CrossLingua | Resource/reader | Bilingual/dictionary workload; aggressive SD caching; useful stress workload |
| 42 | Minuta | Gap | No distinct BLE evidence established; source-screen radio/power/input |
| 43 | CrossWordle | App/input | Small app input/state patterns; not a primary BLE source |
| 44 | ChinesePoint | High-priority gap | X4 Pro/touch lineage may be especially relevant to touch/swipe semantics; source/ref must be established |
| 45 | Headwater Edition | Network/resource | Task-scoped network update/download; no distinct BLE evidence established |
| 46 | Lector (DX34 successor) | No-BLE control | Explicit GPIO-only/no BLE; lean reading baseline |
| 47 | CrossPoint ++ | Resource/network | Sync/network fixes and current CrossPoint derivative; no distinct BLE evidence established in broad screen |

## 4. Beyond the 47-entry catalog

Do not freeze discovery to ReadMe.club. Additional relevant repositories/lineages already surfaced:

- `Rballesteros/crosspoint-reader-enhanced` — major BLE source; not the same thing as assuming the catalog's CrossPoint Reader BLE entry.
- `hannah-nula/crosspoint-x3-ble-page-turner` — hardware-validated X3 reconnect/pairing work.
- `mirkokiefer/shelf` — GitHub search surfaced XTEINK builds containing NimBLE artifacts; needs source-level confirmation because generated build artifacts are not enough evidence of intended BLE behavior.
- CrossPet BLE forks/branches separate from the original CrossPet repository.
- FreeInk SDK `BleKeyboardHost` lineage embedded in downstream firmware trees.
- ESP32/XTEINK launcher projects with NimBLE bond compatibility handling across firmware switches — relevant to bond persistence and NVS compatibility, even when not an e-reader firmware.
- future new forks: rerun GitHub topic/code search before implementation.

## 5. New research questions created by the ecosystem scan

### 5.1 Can Nooir avoid remote-specific profiles for most devices?

Enhanced/X3/CrumBLE prove profiles + learn mode work. Nooir should test whether a small descriptor-aware parser can classify enough standard keyboard/consumer/mouse reports that profiles become exceptions rather than the primary architecture.

### 5.2 Can the same normalized input seam serve buttons, BLE and touch?

Enhanced's virtual-button injection is strong for button-like remotes. Nooir's future swipe-style remote support may require a richer normalized trigger than "virtual physical button." Design a seam that can still collapse to ButtonNavigator for ordinary controls but retain pointer/gesture semantics when useful.

### 5.3 What BLE residency policy is best for Nooir?

Benchmark at least:
- always initialized/connected;
- reader-only;
- screen/task-scoped;
- connected reader but temporarily suspended for dictionary/PDF/CBZ/large chapter work;
- quick-connect/on-demand.

CrumBLE, Foulad and Enhanced each point to different valid policies because their feature/memory budgets differ.

### 5.4 Should Nooir reserve controller memory at boot?

Enhanced documents the Arduino-ESP32 controller-memory reserve/release tradeoff. A no-BLE boot may recover valuable RAM, but same-boot later enable may then require restart. Test:
- reserve always;
- reserve only in BLE build;
- boot-mode/restart transition when user enables BLE;
- whether a controlled restart is acceptable UX in exchange for more RAM while BLE is disabled.

### 5.5 Can we reduce radio duty without making page turns feel slow?

Borrow the *shape* of Flowe's fast->slow discovery policy and Enhanced's idle suspend, but apply it to HID-central:
- fast scan immediately after explicit connect/wake/link loss;
- sparse/backoff recovery later;
- connected interval tuned for human button latency;
- no perpetual aggressive scanning when the remote is off.

### 5.6 What does a swipe remote really emit?

Still hardware-blocked. The catalog scan found many button/keyboard-oriented implementations but no verified source proving the exact swipe-like remote behavior the user described. This makes the BLE Input Inspector a higher priority, not a lower one.

## 6. Expanded future Nooir test matrix

In addition to BLE_REMOTE_RESEARCH.md, include:

**Input compatibility**
- keyboard arrow/PageUp/PageDown remote;
- Consumer Control remote;
- Free2/Free3 family;
- GameBrick/IINE style;
- full BLE keyboard;
- mouse/pointer-style clicker;
- swipe/ring remote;
- composite keyboard+mouse remote;
- unknown HID with learn mode;
- malformed/unexpected report.

**Workload coexistence**
- ordinary EPUB;
- image-heavy EPUB;
- Arabic/RTL EPUB;
- dictionary lookup;
- CBZ high-resolution page;
- XTC/XTCH;
- future PDF reflow;
- OPDS/TLS;
- KOReader sync;
- web transfer;
- sleep-screen render;
- home library with hundreds of cached books.

**Lifecycle**
- cold boot with BLE off;
- cold boot with BLE enabled;
- enable after heavy reader use;
- disable while connected;
- remote off/on;
- remote sleep/wake;
- out-of-range/return;
- reader sleep/wake;
- repeated 100-cycle connect/disconnect;
- Wi-Fi request while BLE connected;
- dictionary/PDF task suspends BLE then reconnects;
- low free heap;
- low largest block with acceptable total heap;
- panic/watchdog previous boot;
- bond corruption/address-type change.

**Power**
- no-BLE control;
- BLE stack reserved but off;
- initialized disconnected;
- scanning fast;
- scanning sparse;
- connected idle;
- normal page-turn cadence;
- reconnect storm prevention;
- sleep current after BLE teardown.

## 7. Priority source-audit queue

Before Nooir BLE implementation, re-audit in this order:

1. CrumBLE current HEAD + BLE history.
2. CrossPoint Reader Enhanced + its vendored NimBLE fork.
3. X3 BLE page-turner current HEAD and validation docs.
4. FreeInk `BleKeyboardHost` current implementation/config.
5. SUMI BLE host + file-transfer source and arena implementation.
6. Flowe BLE service + radio/transfer lifecycle.
7. Foulad current BLE recovery/status docs.
8. biscuit `RadioManager` and BLE scanner/HID peripheral.
9. Casper exact BT pre-release implementation.
10. CrossPet BLE exact fork/ref.
11. ChinesePoint / Chinese CrossLink source lineages.
12. official XTEINK black-box behavior using physical captures.
13. rerun ecosystem search for new firmware after this survey date.

The remaining catalog entries stay in the matrix as secondary sources for memory, power, input, network and workload ideas.

## 8. What we should *not* conclude yet

- Do not assume NimBLE-Arduino-enhanced is automatically better for Nooir; compare its fixes with the exact future framework version first.
- Do not copy CrumBLE/Foulad heap thresholds as Nooir constants.
- Do not assume Wi-Fi + BLE is impossible at the ESP32-C3 hardware level; several projects serialize them for practical memory/stability reasons. Test Nooir's actual stack.
- Do not assume a remote called "Free2"/"Free3" always has one report layout.
- Do not assume all page-turners are keyboard/consumer HID.
- Do not assume swipe-looking behavior means the remote sends a formal HID "swipe"; it may be pointer deltas, wheel data, key sequences or vendor reports.
- Do not keep BLE callbacks doing UI/render/SD/logging work.
- Do not make BLE-specific code understand EPUB/CBZ/PDF.
- Do not add a heavyweight runtime mapping engine before measuring flash/RAM.
- Do not sacrifice Nooir's Phase-A headroom merely because another firmware runs BLE successfully with a different feature set.

## 9. Updated guiding principle

> Study the whole ecosystem for mechanisms, not just firmware that advertises the same feature. Capture remote behavior faithfully; normalize input once; reuse Nooir actions; serialize expensive radios/workloads when that wins; and treat memory, fragmentation, reconnect behavior and battery as part of BLE correctness.

The target is not "Nooir can connect to a page turner." The target is a BLE subsystem that remains reliable when the book, dictionary, images, network, sleep cycle and weird remote all become inconvenient at the same time.
