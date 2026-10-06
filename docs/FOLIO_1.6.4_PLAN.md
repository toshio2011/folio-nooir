# Folio Nooir 1.6.4 Plan

Status: active post-1.6.3 work.

## Release state

Folio Nooir 1.6.3 is released and closed. Do not reopen 1.6.3 for normal feature work. All work below belongs to 1.6.4 or later unless a critical regression requires a separate hotfix.

Released 1.6.3 artifact:
- firmware.bin size: 5,788,880 bytes
- SHA-256: A843B52D976B547FA8E60ECC5DC544F826F7A7725AFEDE3FEE6245ACE06E3FA8

## 1.6.4 priority order

1. File Transfer/web runtime reboot regression.
2. Post-Wi-Fi BLE heap/largest-block recovery.
3. Deterministic release builds.
4. EPUB fidelity: small caps, chapter headings/hgroup, verse/letters, lightweight tables.
5. EPUB regression/torture coverage and parser/cache hardening.
6. Bluetooth controller/mapping polish after lifecycle stability.
7. CBZ performance work.
8. Physical X3 validation.
9. X4 Pro / X4 Classic investigation later.
10. Diagnostics cleanup after lifecycle fixes are proven.

## 1. File Transfer / Wi-Fi stability first

This is the first task for the next physical X4 session. Do not start EPUB fidelity, CBZ, or BLE feature expansion before the repeatable File Transfer reboot is understood.

### A. Released 1.6.3 File Transfer reboot

A community user testing released 1.6.3 on X4 reported that after joining Wi-Fi from File Transfer, the device reboots after a few seconds. The web interface never becomes usable. The current local symptom should be treated the same way: this is a general File Transfer startup/runtime regression, not a Settings-page-only problem.

### Current investigation findings

Comparison with working 1.6.2 narrows the regression surface:

- the core File Transfer activity/web-server architecture was already present in 1.6.2;
- the normal startup sequence is still essentially connect network -> mDNS -> `CrossPointWebServer` -> HTTP/WebDAV/WebSocket/UDP;
- the rich Nooir route set and web-management feature set were already present in 1.6.2, so route count alone is not the new regression delta;
- automatic Clock/Weather sync on successful Wi-Fi connection also existed in 1.6.2;
- the web Settings HTML is not the primary explanation because the whole browser session is unusable before individual pages can meaningfully load;
- 1.6.3 adds BLE/NimBLE to the normal release plus coexistence/lifecycle and memory-diagnostic work, changing the runtime/heap shape even when BLE is expected to stop for networking;
- `CrossPointWebServerActivity` is marked Bluetooth-resource-sensitive and ActivityManager asks BLE to stop before entering it, but hardware evidence is still needed to prove the BLE host has fully released its memory before Wi-Fi/server allocations begin;
- `WifiSelectionActivity` keeps the working 1.6.2 connection flow but adds explicit retain/deinit ownership and diagnostics around selection exit;
- File Transfer still performs `silentRestart()` on exit once Wi-Fi has been activated. A graceful server-start failure can therefore appear as a clean mysterious reboot, while panic/OOM/watchdog/brownout remain possible alternatives.

### Leading hypothesis

The most plausible regression class is currently:

> 1.6.3 reaches the old File Transfer/web-server startup with a different memory/runtime shape, and some stage now crosses a heap/largest-block/reset threshold.

This is not yet a diagnosis. Do not remove web features or lower BLE safety floors until serial evidence identifies the failing stage.

### Next physical X4 session — exact order

1. Start with released 1.6.3.
2. Enter File Transfer and record free heap + largest block before network work.
3. Verify BLE stop is not merely requested but actually complete before Wi-Fi starts.
4. Reproduce **Join Network -> connect -> leave it alone** with serial attached.
5. If useful, test **Create Hotspot** as a control path to separate STA-specific work from common server startup.
6. If practical, repeat the same File Transfer flow on 1.6.2 on the same X4/network and compare memory/reset behavior at matching stages.
7. Use one bounded diagnostic build with INFO checkpoints at:
   - `FILEXFER enter`;
   - BLE stop requested;
   - BLE fully stopped / post-stop heap;
   - Wi-Fi scan begin/end;
   - Wi-Fi connect begin/success;
   - before/after automatic Clock/Weather sync;
   - WifiSelection child exit/destroy;
   - before/after mDNS;
   - before `CrossPointWebServer` construction;
   - after Arduino `WebServer` allocation;
   - after route registration;
   - after WebDAV registration;
   - after HTTP begin;
   - after WebSocket construction/begin;
   - after UDP discovery begin;
   - `SERVER_RUNNING`;
   - File Transfer exit reason before any `silentRestart()`.
8. Record boot/reset reason so the next boot can distinguish intentional silent restart, panic/assert, watchdog, brownout, or other reset.
9. Fix only the proven failing stage and physically retest normal joined-network File Transfer.

### Candidate fixes after evidence

Depending on the log:

- ensure BLE and other disposable state are fully reclaimed before network startup;
- reduce a proven peak allocation or defer only the specific optional server component causing the cliff;
- fix any lifecycle race between WifiSelection destruction and server startup;
- adopt cleaner CrossLink-style teardown ordering where it directly helps;
- reconsider forced `silentRestart()` only after normal teardown is proven safe;
- preserve the full web UI whenever possible.

Do not add restart loops, retry storms, global purges, or floor bypasses.

### B. Post-Wi-Fi BLE heap recovery

This is the second network task after File Transfer itself is stable.

Physical X4 evidence from the cancel/deinit path showed approximately:

- after BLE stop / before Wi-Fi: ~96.5 KiB free;
- while Wi-Fi connection work was active: ~48.9 KiB free at the captured low point;
- after `WIFI_OFF` / selection teardown: ~84-85 KiB free;
- BLE correctly remained below admission floors;
- sleep/wake restored the healthy baseline.

Do not call the retained footprint a leak until repeated-session testing distinguishes one-time network residency from cumulative loss.

Repeated-cycle interpretation after File Transfer is fixed:

- ~96 -> ~84 -> ~84 -> ~84 KiB: likely one-time networking residency;
- ~96 -> ~84 -> ~72 -> ~60 KiB: cumulative leak;
- large first drop + small later drops: residency plus smaller leak/fragmentation.

Goal:
- identify retained allocations/fragmentation;
- recover enough healthy post-Wi-Fi heap/largest block for BLE to return when safely affordable;
- preserve Reader priority and existing BLE floors.

## 2. Reference-firmware audit: stock XTEINK and CrossLink

Use other firmware as evidence/design references, not as a feature shopping list.

### Stock XTEINK X4

Public source code was not found during the audit. Official documentation and public firmware/update behavior can still be used for comparison.

Useful references:
- Hotspot-first local browser transfer at `192.168.3.3`;
- Bluetooth scanning/connection stability behavior;
- EPUB punctuation, English word spacing, paragraph indentation, and line-break behavior;
- JPG refresh/display performance;
- book-vs-chapter progress display;
- explicit index rebuild workflow;
- external-font performance tradeoffs.

### CrossLink

CrossLink is MIT-licensed and can be used as an implementation reference with attribution/license preservation where code is reused.

High-value comparisons:
- File Transfer startup/shutdown ordering and memory behavior;
- clean stop order: web server -> mDNS/DNS -> disconnect -> `WiFi.mode(WIFI_OFF)`;
- leaner web-server footprint as a baseline, but not as proof that Nooir must remove routes;
- table/image/kerning EPUB fixtures;
- small parser/layout fixes only when a real Nooir fixture proves value.

Do not import CrossLink's different partition/install model into Nooir.

## 3. EPUB fidelity — surgical improvements only

Keep Nooir a fast book renderer, not a browser engine. Every addition must preserve X3/X4 responsiveness, bounded memory use, cache correctness, and readable fallback.

Use the Standard Ebooks edition of Theodore Roosevelt's *Through the Brazilian Wilderness* as a permanent real-world regression/reference EPUB.

### A. Small caps

Investigate `font-variant: small-caps`.
- prefer bounded, allocation-light text-run handling;
- no publisher font requirement;
- measure layout/page-turn cost before keeping it.

### B. Chapter heading / `hgroup` fidelity

Improve common heading structures such as `hgroup` plus chapter subtitle text.
- investigate narrowly bounded child (`>`) and adjacent sibling (`+`) selector support only where real books need it;
- avoid a general selector engine.

### C. Verse / letters / block typography

Improve where the current BlockStyle model can support it cheaply:
- hanging indents;
- margins;
- alignment;
- spacing;
- signatures/salutations;
- verse presentation.

### D. Tables

Keep the current bounded flowing-row renderer as the safe baseline.
- consider lightweight `colspan` awareness;
- improve readability/headers where cheap;
- do not build a browser-style grid engine.

### Explicit EPUB scope limits

Not planned for 1.6.4:
- full browser CSS;
- CSS floats;
- true floated drop caps;
- general pseudo-elements;
- full descendant/child/sibling selector engine;
- publisher `@font-face`;
- complex rowspan/colspan geometry;
- flexbox/grid/multi-column layout.

## 4. EPUB regression/torture set

Create/retain fixtures for:
- small caps;
- chapter headings/hgroup;
- verse and letters;
- simple/complex tables;
- huge images;
- malformed XHTML/entities;
- empty inline/span spacing;
- unknown XML tags;
- RTL/LTR mixtures;
- cache interruption/recovery;
- low-memory fallback;
- punctuation/word spacing/line-break behavior inspired by stock XTEINK notes;
- JPG-heavy pages;
- useful CrossLink table/image/kerning fixtures where provenance/licensing is clear.

## 5. Parser/cache/network hardening

Evidence-driven only:
- unknown XML-tag regression behavior;
- empty CSS span / inline-spacing behavior;
- KOReader oversized-response handling where relevant;
- tiny parser/cache fixes that apply cleanly;
- no broad renderer rewrite.

## 6. Deterministic release builds

Make generated HTML/gzip assets reproducible.
- use deterministic gzip timestamps (`mtime=0` where appropriate);
- run two clean `gh_release` builds from identical sources;
- require identical firmware hashes.

## 7. Bluetooth Page Turner follow-up

Bluetooth remains Beta.

After network lifecycle stability:
- broaden controller compatibility;
- improve mapping/setup UX;
- consider a bounded raw-HID learner using fixed storage only;
- improve BLE/Wi-Fi lifecycle feedback;
- do not weaken Reader-first memory policy.

## 8. CBZ / manga

Still experimental.

Candidate work:
- high-resolution page performance;
- first-page/startup latency;
- safer/smarter prefetch;
- optional web-assisted preprocessing;
- image quality/performance balance;
- never steal memory needed for EPUB/Reader stability.

## 9. Hardware validation

- old X4 remains primary physical target;
- X3 remains compatibility floor; full physical regression validation is still needed;
- X4 Pro / X4 Classic remain investigation items, not officially supported targets.

## 10. Diagnostics cleanup

Keep allocator/BLE/network forensic diagnostics while 1.6.4 lifecycle work is active. Trim or gate them only after replacement behavior is proven physically.

## Deferred / not implemented

- PDF reading
- FB2 reading
- full system-wide dark UI
- full browser-grade EPUB CSS
- official X4 Pro / X4 Classic support

## Acceptance rule

A 1.6.4 change is worth keeping only when it fixes a real lifecycle failure or improves a real book without making ordinary reading slower, materially reducing safe heap margin, increasing fragmentation/churn, or making recovery/cache behavior less predictable.

Reader/page rendering remains highest priority. Optional services must yield under pressure.
