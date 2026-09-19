# BLE Remote Input Research

Status: **research dossier / future implementation reference**  
Date captured: 2026-09-19  
Scope: XTEINK X3/X4-class resource-constrained firmware; Nooir implementation is **not authorized by this document**.

## 1. Goal

Future Nooir Bluetooth should be designed as **Bluetooth Remote Input**, not as a hard-coded two-button page turner.

The target architecture is:

```text
BLE/HID transport
    -> complete relevant HID report capture
    -> HID report/descriptor decoding
    -> bounded sequence/gesture recognition
    -> normalized trigger
    -> configurable Nooir mapping
    -> existing Nooir input/action dispatch
```

The remote must be understood before its input is mapped. A remote that appears to "swipe" may not emit the same HID traffic as a simple click remote. It may present keyboard usages, Consumer Control usages, mouse buttons, relative pointer movement, wheel/scroll, vendor reports, or a multi-report press/move/release sequence. Do not collapse the first non-zero byte directly into Next/Previous Page until the complete relevant report or bounded sequence has been classified.

The second goal is to make BLE coexist safely with Nooir's tight X3/X4 memory and battery budgets. Compatibility alone is insufficient: flash, heap, largest contiguous allocation, fragmentation, task stacks, Wi-Fi interaction, sleep/wake, reconnect behavior, and power cost are first-class design inputs.

## 2. Evidence gathered so far

### 2.1 Platform / Espressif facts

The ESP32-C3 supports Bluetooth LE but not Classic Bluetooth. ESP-NimBLE is the Apache NimBLE-derived host available in ESP-IDF and is the natural stack to evaluate for a BLE-only central/HID host.

Espressif documentation shows that BLE memory is material enough to treat as a subsystem rather than a free background feature. Their general Bluetooth FAQ gives historical/example-level figures on the order of tens of kilobytes for BLE controller and GATT client operation; exact Nooir numbers must be measured with Nooir's actual Arduino/ESP-IDF/NimBLE versions and configuration.

Espressif exposes many NimBLE compile-time knobs that can reduce RAM/flash when the application only needs a narrow role. Relevant categories include:
- maximum connections;
- enabled GAP roles (central/observer/peripheral/broadcaster);
- maximum bonds and CCCDs;
- security features;
- BLE 5/extended features;
- whitelist size;
- maximum GATT procedures;
- MSYS block counts/sizes;
- ACL/event buffers;
- reconnect/reattempt features;
- host task stack size;
- privacy/vendor features where not required.

**Nooir implication:** start from the minimum feature set for one bonded HID peripheral and one central/observer role needed for discovery/connection. Do not copy a generic NimBLE configuration.

Espressif power guidance confirms the main BLE power levers:
- scan interval and scan window;
- passive vs active scan;
- connection interval;
- connection latency;
- supervision timeout;
- RF transmit power;
- automatic/light sleep and dynamic frequency behavior.

Continuous scanning (window == interval) is highest-duty scanning. Longer connection intervals reduce wakeups but increase latency. These must be tuned for page-turn responsiveness rather than throughput.

### 2.2 HID is broader than keyboard buttons

Espressif HID examples and HID report maps cover keyboard, mouse and Consumer Control roles. Independent HID host work also demonstrates composite devices and vendor/custom reports.

**Nooir implication:** a generic remote decoder should not assume an 8-byte boot-keyboard packet. During discovery, obtain/inspect the HID Report Map where practical, subscribe to relevant Input Report characteristics, preserve report ID/context, and classify reports by descriptor rather than only scanning bytes for known page-turn keycodes.

A fallback heuristic may still be useful for malformed/simple remotes, but descriptor-aware decoding should be the target.

### 2.3 CrumBLE: strongest public XTEINK BLE implementation found so far

Audited public CrumBLE source at commit `3c212bb5c4897f2d3d8ffd2ef9927d9299ad8627`.

CrumBLE is currently the most useful public case study because it documents real XTEINK failures and iterations rather than only advertising BLE support.

Observed architecture:
- NimBLE central/HID host.
- HID service `0x1812`, Report `0x2A4D`, HID Information, Report Map and Protocol Mode are explicitly handled.
- asynchronous scanning;
- pairing/bond persistence;
- saved bonded address/type;
- connection callbacks run on the NimBLE host task and defer/latch work rather than doing unsafe/heavy logging there;
- report-map hints distinguish at least keyboard vs Consumer Usage Page;
- known remote profiles and generic fallback extraction coexist;
- connection parameters in the audited version use 15–30 ms interval, latency 0, 6 s supervision timeout, with a 10 s connect timeout;
- Wi-Fi is explicitly shut down before BLE enable in this implementation;
- CPU is temporarily held at normal speed around NimBLE/controller initialization because low-frequency initialization had freeze/watchdog problems in field investigation.

Important memory history in the source:
- Earlier builds required a much larger free-heap gate before NimBLE init.
- Later pool/controller trimming substantially reduced the reported initialization cost.
- The audited comments describe tuning connection count, controller activities, MSYS/ACL/event pools, NimBLE host task stack and ATT prepared-write capacity.
- Both **free heap and largest contiguous allocation** are checked before BLE initialization.
- Source history documents controller-init failures when max contiguous allocation was too small even when total free heap looked acceptable.
- Source history also documents crashes while explicitly deleting/destroying a client connected to a real remote; the implementation reverted to a proven disconnect + deinit path.
- Controller memory release can be one-way for the current boot in the used Arduino/ESP-IDF integration, requiring restart before BLE can be initialized again.
- Retry logic is rate-limited and eventually gives up rather than hammering initialization indefinitely.
- The changelog records image-heavy reader + Bluetooth heap pressure and a case where system-wide glyph fallback had to be reverted because the combination pushed memory over the edge.
- CrumBLE deliberately disables BLE outside the book/reader to relieve parser heap pressure.

**Lesson for Nooir:** do not assume BLE can remain resident everywhere merely because pairing works. Nooir Phase A may give us a different budget, so CrumBLE's reader-only policy is evidence to benchmark, not a rule to copy.

**Lesson for teardown:** enable/disable/re-enable must be a dedicated stress test. BLE cleanup is not a trivial destructor problem on this stack/device.

### 2.4 CrossInk / CrossPoint / CrossLink status in this pass

CrossInk's current public `platformio.ini` explicitly ignores a library named `BLE` in the normal build and contains substantial non-BLE RAM tuning. Public code search in this pass did not surface an active CrossInk BLE host implementation. Treat CrossInk as a valuable memory/input/UI reference, not as verified BLE source until an actual BLE branch/commit is located.

CrossPoint's public README still mentions Bluetooth page-turner work, and its current build configuration contains NimBLE-related SDK configuration, but this pass did not locate a complete active public page-turner implementation on the searched branch. A feature branch/PR may exist and should be audited by exact ref when identified. Do not infer runtime architecture merely from NimBLE Kconfig entries.

The public DaisonChun CrossLink repository did not expose BLE/NimBLE symbols through GitHub code search in this pass. Its public `platformio.ini` also did not reveal a BLE dependency. The user's physical experience establishes that a CrossLink lineage worked with Bluetooth on X4, but **the exact public-source implementation has not yet been proven**. The Chinese X3/CrossLink lineage supplied separately remains a high-priority source target because its UI/remote behavior may differ from the public GitHub tree.

These are explicit evidence gaps, not negative conclusions.

## 3. Remote input taxonomy to support during research

Capture and classify at least:

| Class | Data to retain | Possible remote behavior |
| --- | --- | --- |
| Keyboard | report ID, modifiers, usages, down/up, rollover | arrows, PageUp/PageDown, Enter, letters |
| Consumer Control | report ID, usage, down/up | volume, media next/previous, play/pause |
| Mouse/pointer | buttons, relative X/Y, wheel/pan, down/up | click remote, swipe-emulating ring |
| Composite HID | report ID + descriptor-defined type | keyboard + mouse in one remote |
| Vendor/custom | report ID + raw bytes + descriptor | proprietary page-turn gesture |
| Repeated/held | timing, repeat cadence, release | long press / rapid page turn |
| Multi-report sequence | bounded time series/state | press -> movement -> release swipe-like behavior |

Never store an unbounded event history. Gesture recognition should be a small streaming state machine with:
- start state;
- accumulated/peak displacement where applicable;
- direction;
- button state;
- elapsed time;
- release/timeout;
- small report-count cap.

Retain raw diagnostic samples only in diagnostic builds or a fixed-size ring buffer.

## 4. Descriptor-aware HID discovery plan

For a candidate remote:
1. record advertised name/address type and relevant advertisement/service UUIDs;
2. connect with security/bonding as required;
3. enumerate HID service and Input Report characteristics;
4. read HID Report Map;
5. parse report IDs, Usage Pages, Usages, report sizes/counts and Input fields;
6. subscribe to every relevant Input Report, not merely the first characteristic;
7. associate each notification with report ID/characteristic and decoded type;
8. record press, release, repeat and timing;
9. exercise every physical control and gesture on the remote;
10. power the remote off/on, let it idle/sleep, move out of range and reconnect;
11. repeat after XTEINK sleep/wake/reboot.

A remote compatibility record should contain:
- vendor/model/marketing name;
- address type;
- advertised services;
- HID report-map hash or compact signature;
- report IDs/types;
- each physical control -> observed report/sequence;
- bonding/security requirements;
- reconnect behavior;
- known quirks;
- mapping recommendations.

Avoid device-name-only special cases where descriptor/report behavior can identify the class generically.

## 5. Proposed BLE Input Inspector

Development/diagnostic only unless production cost proves negligible.

Example output:

```text
BLE remote: Example Ring
HID map: keyboard + mouse

t=0000 report=3 mouse buttons=1 x=0 y=0 wheel=0
t=0018 report=3 mouse buttons=1 x=14 y=1 wheel=0
t=0034 report=3 mouse buttons=1 x=21 y=0 wheel=0
t=0052 report=3 mouse buttons=1 x=17 y=-1 wheel=0
t=0070 report=3 mouse buttons=0 x=0 y=0 wheel=0
sequence: pointer-drag-right
normalized trigger: SwipeRight
```

Inspector requirements:
- never log/allocate heavily from the NimBLE callback;
- callback copies/latches into a fixed-size structure/queue;
- main loop formats diagnostics;
- fixed maximum report length and queue depth;
- dropped-report counter;
- report/sequence counters;
- optional descriptor dump/hash;
- free heap / largest block snapshot around connect/disconnect in diagnostic builds.

This tool is valuable even if it never ships: it turns unsupported-remotes reports into evidence instead of guesses.

## 6. Normalized trigger layer

Transport-specific data should end before Nooir action dispatch.

Candidate normalized triggers:
- Key/Consumer usage press/release;
- PointerButton1/2/...;
- WheelUp/WheelDown;
- SwipeLeft/Right/Up/Down when a bounded sequence is confidently recognized;
- Hold(trigger, threshold);
- Repeat(trigger);
- device connected/disconnected events for UI status only.

Mapping targets should reuse Nooir's existing action/input infrastructure where possible:
- Next/Previous Page;
- Up/Down/Left/Right;
- Confirm;
- Back;
- Reader Menu;
- Home;
- Dictionary/Lookup;
- Toggle Bookmark;
- Clip Text;
- orientation;
- other existing actions only when the current activity supports them.

Do not make the BLE layer call EPUB/CBZ code directly.

When a remote's pointer sequence is recognized as a swipe, default mapping can still dispatch the semantic Nooir action rather than synthesizing a fake touchscreen path. Actual touch-event synthesis should be a separate, evidence-driven capability only if an activity genuinely requires touch semantics.

## 7. Memory research matrix

Every BLE prototype must record **total free heap and largest free block**. Total heap alone is insufficient.

Measure:
1. clean boot, BLE not compiled (control build);
2. BLE compiled but disabled;
3. BLE enabled before scan;
4. active scan;
5. scan complete/results retained;
6. pairing/security;
7. connected + subscribed;
8. connected idle;
9. repeated input;
10. remote disconnect;
11. reconnect;
12. BLE disabled/deinitialized;
13. second enable in same boot;
14. 10/50/100 connect-disconnect cycles where practical;
15. reader with ordinary EPUB;
16. image-heavy EPUB;
17. Arabic/Quran EPUB;
18. CBZ;
19. XTC/XTCH;
20. Dictionary open/lookup;
21. Book Info/menu/overlay;
22. Wi-Fi transition before/after BLE;
23. sleep/wake.

Record:
- linked flash delta;
- firmware.bin delta;
- static RAM delta;
- free heap;
- largest free block;
- minimum observed heap;
- task stack high-water marks;
- allocation failure/fragmentation;
- event latency and lost/duplicate reports.

### Configuration A/B candidates

Test, don't assume:
- one connection only;
- only central + observer roles required;
- minimum bonds needed (probably 1–2);
- minimum CCCDs;
- minimum GATT procedures;
- reduced controller activity count;
- reduced MSYS pools;
- reduced ACL/event buffers;
- trimmed host task stack based on measured high-water mark;
- no unused peripheral/broadcaster features;
- no unused BLE 5/extended/privacy/vendor features;
- bounded prepared-write capacity;
- security only to the level required for common HID bonding;
- dynamic vs static allocation options available in the exact framework version.

Do not cargo-cult CrumBLE's numeric values: Nooir may use a different framework/library version and has a different memory layout.

## 8. Wi-Fi coexistence research

CrumBLE explicitly turns Wi-Fi off before enabling BLE. ESP32-C3 shares a 2.4 GHz radio/coexistence environment, but the exact Nooir framework may technically support coexistence.

Test three questions separately:
1. **Can** the chosen framework keep Wi-Fi + BLE initialized?
2. Is it **memory-safe** for Nooir's workload?
3. Is it **useful** enough to justify the RAM/power/latency cost?

Likely Nooir operations needing policy:
- KOReader Sync;
- OPDS;
- OTA/update;
- web transfer;
- Clock/Weather;
- Font Manager;
- BLE remote during reading.

A simple serialized policy (temporarily disconnect/suspend BLE for network work, then restore the bonded remote) may be safer than simultaneous Wi-Fi+BLE, but this is not decided until measured.

## 9. Battery and power research matrix

BLE is low-energy only when its duty cycle and wake behavior are sensible.

Measure physical-device current/battery behavior where possible for:
- BLE off;
- BLE enabled but disconnected;
- passive scan;
- active scan;
- continuous/high-duty scan;
- connected idle;
- one page turn every ~10 s;
- rapid remote input;
- remote out of range;
- repeated reconnect/backoff;
- remote powered off;
- XTEINK awake but idle;
- reader active;
- device sleep/wake.

Variables to A/B:
- scan interval/window;
- active vs passive scan;
- scan timeout;
- reconnect cadence and exponential/backoff behavior;
- connection interval;
- latency where the central/peripheral negotiation permits it;
- supervision timeout;
- TX power;
- CPU frequency lock only around operations that actually require it;
- reader-only vs globally resident BLE;
- always-connected vs on-demand/quick-connect;
- behavior when remote sleeps.

Target UX: a page-turn remote should feel immediate, but there is no reason to optimize for bulk BLE throughput. Prefer the slowest radio/CPU duty cycle that still gives reliable, subjectively instant page turns.

## 10. Reliability / lifecycle tests

Required scenarios:
- first pair;
- cancel scan;
- no devices;
- wrong/non-HID BLE device;
- bond succeeds;
- bond fails;
- remote requires encryption before report discovery;
- remote powers off while connected;
- remote wakes later;
- out-of-range then return;
- XTEINK sleep/wake;
- XTEINK reboot;
- forget bond;
- re-pair same device;
- pair a replacement remote;
- notification storm/repeat;
- malformed/oversized report;
- composite device with multiple report IDs;
- disconnect during callback/input;
- exit reader while event arrives;
- Wi-Fi requested while connected;
- low heap before BLE enable;
- fragmented heap with acceptable total free heap;
- BLE deinit and same-boot reinit;
- 100+ page turns;
- long reading session.

No BLE callback should directly perform heavy rendering, SD I/O, logging, allocations, activity replacement or reader parsing.

## 11. Performance targets / decision gates

Do not set final numeric limits until the Phase A baseline is measured, but require:
- no material regression with BLE compiled but disabled;
- safe app-slot cushion remains;
- BLE enable refuses/fails cleanly when contiguous memory is insufficient;
- no parser/image/dictionary regression with connected-idle BLE under the supported policy;
- no unbounded reconnect loop;
- no unbounded scan;
- no duplicate page turns from one intended control action;
- remote reconnect behavior is understandable and recoverable;
- disabling BLE actually returns the expected reclaimable memory where the stack permits it;
- sleep/wake does not strand stale client state;
- battery impact is measured and documented.

If always-resident BLE violates these gates, prefer reader-only/on-demand operation rather than weakening EPUB/image/font safety.

## 12. Firmware comparison template

For each firmware/ref, capture:

```text
Firmware / commit:
Target device:
Framework / Arduino / ESP-IDF:
BLE library + version:
Role(s):
HID discovery:
Report-map parsing:
Supported report classes:
Known remote profiles:
Generic fallback:
Pair/bond persistence:
Reconnect policy:
Scan policy:
Connection parameters:
Wi-Fi coexistence policy:
Sleep/wake policy:
CPU/power locks:
BLE teardown:
Flash delta:
Static RAM delta:
Enable/scan/connect heap:
Largest-block behavior:
Task stacks:
Battery evidence:
Known crashes/failures:
Ideas worth adapting:
Ideas to avoid:
Evidence gaps:
```

## 13. Current comparison snapshot

| Firmware | Verified in this research | Important lesson | Gap |
| --- | --- | --- | --- |
| CrumBLE @ `3c212bb5` | Public NimBLE HID manager, pairing/reconnect/settings/history | Richest XTEINK evidence for memory gates, teardown hazards, reader-only BLE and HID quirks | Need full measured build deltas and physical battery measurements |
| CrossInk current public main | RAM-tuned build; normal config ignores `BLE` library | Useful memory baseline/architecture | No active BLE implementation located in this pass |
| CrossPoint current public branch | NimBLE-related Kconfig exists; README references page turner | Track upstream BLE work by exact branch/PR | Complete runtime implementation not located in this pass |
| DaisonChun CrossLink public master | Public project/build inspected | User reports a CrossLink lineage worked physically on X4 | BLE source not located by public code search; exact lineage may differ |
| Chinese X3/CrossLink lineage | Known research target | Potentially important for swipe-like remote behavior | Source retrieval/audit still unresolved |

## 14. Research still possible before hardware capture

Continue without touching Nooir firmware:
- identify CrossPoint BLE feature branch/PR and audit exact source;
- inspect CrumBLE's current FreeInk `BleKeyboardHost`/replacement `BleHid` implementation and its sdkconfig/nimconfig trims in full;
- mine CrumBLE changelog/commit history for measured before/after heap values and failure fixes;
- locate CrossLink BLE lineage or confirm it is in a different/private/Chinese tree;
- retrieve/audit the Chinese X3 source if access becomes possible;
- search additional XTEINK forks for NimBLE/HID host implementations;
- compare HID descriptor parsers suitable for a tiny central;
- establish the exact Arduino/ESP-IDF/NimBLE versions Nooir would inherit and list available Kconfig knobs;
- design a host-side parser/state-machine test suite using recorded synthetic HID reports;
- prepare a physical test sheet for several remote types.

Hardware is eventually required for:
- exact HID captures from click and swipe-style remotes;
- real pairing/reconnect quirks;
- current/battery measurements;
- RF/range behavior;
- physical X4 latency/stability;
- physical X3 validation if an X3 is available.

## 15. Recommended implementation sequence when BLE is finally authorized

1. Re-run upstream research so this dossier is not stale.
2. Freeze a measured Nooir no-BLE baseline after Phase A.
3. Create a separate BLE experimental build/profile.
4. Bring up minimal NimBLE central with aggressive compile-time trimming.
5. Add BLE Input Inspector before hard-coded remote mappings.
6. Capture real remotes and build descriptor-aware decoders.
7. Add bounded sequence recognizer for swipe-like/pointer remotes.
8. Normalize triggers into Nooir's existing input/action layer.
9. Add bonding/reconnect and lifecycle state machine.
10. Measure and tune memory.
11. Measure and tune battery/power.
12. Test Wi-Fi serialization/coexistence.
13. Stress teardown/re-enable/sleep/wake.
14. Only then add user-facing mapping/settings UI.
15. Decide from evidence whether production policy is always-on, reader-only, or on-demand.
16. Physical X4 regression; X3 simulator/shared-path regression; never claim physical X3 BLE without hardware evidence.

## 16. Design principle

> Capture the remote faithfully first. Map it second. Keep the BLE layer ignorant of book formats. Measure memory and battery as part of correctness.

A successful Nooir BLE implementation is not the one that merely turns a page. It is the one that can understand a broad class of remotes, map them predictably to existing Nooir actions, reconnect cleanly, release resources safely, and remain boringly reliable through a long reading session.

## Ecosystem-wide companion survey

A broader screen of the current XTEINK firmware ecosystem (47 catalog entries plus additional BLE lineages) is preserved in [`BLE_ECOSYSTEM_SURVEY.md`](BLE_ECOSYSTEM_SURVEY.md). It covers direct BLE implementations as well as memory, power, radio arbitration, input and workload ideas from firmware that does not advertise page-turner support.
