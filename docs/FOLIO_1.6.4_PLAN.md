# Folio Nooir 1.6.4 Plan

Status: post-1.6.3 planning baseline.

## Release state

Folio Nooir 1.6.3 is released and closed. Do not reopen 1.6.3 for normal feature work. All remaining work below belongs to 1.6.4 or later unless a critical regression requires a separate hotfix.

Released 1.6.3 artifact:
- firmware.bin size: 5,788,880 bytes
- SHA-256: A843B52D976B547FA8E60ECC5DC544F826F7A7725AFEDE3FEE6245ACE06E3FA8

## 1.6.4 priorities

### 1. File Transfer / Wi-Fi stability first

This is the first task for the next physical X4 session. Do not start EPUB fidelity, CBZ, or BLE feature expansion before the repeatable File Transfer reboot is understood.

#### A. Community-reported and locally observed X4 File Transfer reboot

A user testing released 1.6.3 on an X4 reported a repeatable setup failure: after connecting the device to Wi-Fi from File Transfer, the device reboots after a few seconds. The reboot reportedly happens every time, preventing use of Nooir's web interface. The same general File Transfer reboot is also reproducible locally.

Treat this as a real 1.6.4 regression investigation. Do not assume it is the same bug as the post-Wi-Fi BLE heap-recovery issue.

##### Current investigation findings

Comparison against 1.6.2 narrows the likely regression surface:

- the basic File Transfer activity / web-server flow already existed in 1.6.2;
- automatic Clock/Weather sync after Wi-Fi connects also existed in 1.6.2, so it is not by itself a new 1.6.3 behavior;
- 1.6.3's normal `gh_release` now includes BLE/NimBLE and its coexistence lifecycle, changing the firmware/runtime memory shape even when BLE is stopped for networking;
- File Transfer is correctly marked Bluetooth-resource-sensitive in the 1.6.3 line, so BLE should yield before the network activity rather than intentionally run alongside it;
- the current Nooir web server is much richer than the lean reference server: it registers roughly 37 explicit HTTP routes before adding WebDAV, WebSocket, UDP discovery, and the surrounding mDNS/network state. CrossLink's current server registers roughly 14 explicit routes plus WebDAV/WebSocket/UDP. This makes web-server startup heap/largest-block pressure a serious suspect that must be measured rather than guessed.

A particularly important behavior can hide the original failure: `CrossPointWebServerActivity::onExit()` performs a `silentRestart()` whenever Wi-Fi had been activated. If `startWebServer()` fails, the activity calls `onGoHome()`, which exits File Transfer and can therefore produce a clean intentional reboot. The visible sequence "Wi-Fi connects, waits a few seconds, then reboots" may therefore mean:

1. Wi-Fi connection succeeds;
2. mDNS / web-server startup fails or returns not-running;
3. File Transfer exits to Home;
4. the existing heap-defrag `silentRestart()` runs.

That is a leading hypothesis, not a diagnosis. A panic/OOM/watchdog reset before graceful exit remains possible.

##### Useful reference behavior

**Stock XTEINK X4**

The official X4 user guide documents local browser transfer using device Hotspot mode: the X4 creates an `E-Paper` Wi-Fi network and serves an upload page at `192.168.3.3`. This is useful as a behavioral reference because it gives us a simple AP/local-transfer control path that does not require joining the user's LAN.

**CrossLink**

CrossLink is an MIT-licensed open-source reference descended from CrossPoint. Its File Transfer shutdown path currently:

1. stops the web server first;
2. ends mDNS;
3. stops/deletes the AP DNS server if present;
4. briefly allows pending network traffic to flush;
5. gracefully disconnects STA or AP;
6. calls `WiFi.mode(WIFI_OFF)`;
7. returns normally without forcing a restart.

Do not copy CrossLink wholesale. Use this as a comparison point for teardown order, startup footprint, and test cases. Any reused code must retain required MIT attribution/license notices.

##### First test tomorrow / next physical X4 session

Before changing behavior, split the failure into common-vs-STA-specific paths:

1. On released 1.6.3, enter **File Transfer -> Create Hotspot** and leave it running.
   - If Hotspot is stable while Join Network reboots, focus first on STA connection, auto-sync, mDNS, or STA-specific transition state.
   - If Hotspot also reboots, focus first on the common web-server startup/memory path.
2. Reproduce **File Transfer -> Join Network -> connect -> leave it alone**.
3. If practical, repeat both tests on 1.6.2 on the same X4/network to establish a clean regression boundary.

##### Diagnostic build to make next

Add INFO-level, bounded checkpoints around the actual File Transfer startup chain:

- `FILEXFER enter` — free heap + largest block;
- Wi-Fi connected;
- before / after automatic Clock/Weather sync;
- before / after mDNS;
- before `CrossPointWebServer` construction;
- after Arduino `WebServer` allocation;
- after route registration;
- after WebDAV registration;
- after HTTP server begin;
- after WebSocket construction/begin;
- after UDP discovery begin;
- `SERVER_RUNNING` — free heap + largest block;
- File Transfer exit reason before any `silentRestart()`.

Also record the boot/reset reason so a test can distinguish:

- intentional Nooir silent restart;
- panic/assert;
- watchdog;
- brownout;
- other reset reason.

Keep diagnostics bounded and release-safe. Do not add background polling just to collect this data.

##### Fix strategy after evidence

Only fix the stage that the log proves is failing. Candidate directions, depending on evidence:

- reduce peak web-server startup allocations;
- split or lazily initialize optional web-management route groups if route registration is the pressure point;
- ensure disposable pre-network/UI/BLE state is actually released before server construction;
- copy/adapt the clean CrossLink shutdown ordering if teardown contributes;
- reconsider the forced `silentRestart()` only after normal teardown is proven to recover safely;
- preserve the full web Settings workflow if it can be made safe rather than deleting useful web functionality to hide the fault.

Do not lower BLE safety floors or hide the failure behind restart/retry loops.

#### B. Wi-Fi -> BLE heap recovery

This remains a separate second network task after File Transfer itself is stable.

Observed on physical X4: after some Wi-Fi selection/connect/cancel/deinit paths, total free heap and largest allocatable block remain below the healthy pre-Wi-Fi state. BLE then correctly stays off because the 1.6.3 admission floors are not met. Sleep/wake restores the healthy baseline.

The existing physical cancel-path sample showed roughly:

- after BLE stop / before Wi-Fi: ~96.5 KiB free;
- while Wi-Fi connection work was active: ~48.9 KiB free at the captured low point;
- after `WIFI_OFF` / selection teardown: ~84-85 KiB free;
- BLE remained below its admission floors.

Arduino-ESP32's Wi-Fi driver does perform `esp_wifi_stop()` / `esp_wifi_deinit()` when Wi-Fi is turned off, but the higher-level Arduino/ESP networking infrastructure is initialized globally and may retain a one-time runtime footprint. Do not call this a leak until repeated-session testing distinguishes one-time residency from cumulative loss.

After File Transfer is fixed, run a repeated Wi-Fi cycle test without sleep/reboot:

- ~96 -> ~84 -> ~84 -> ~84 KiB suggests one-time networking residency;
- ~96 -> ~84 -> ~72 -> ~60 KiB suggests a recurring leak;
- a large first drop plus small later drops suggests resident infrastructure plus a smaller leak/fragmentation issue.

Goal for 1.6.4:
- identify retained Wi-Fi/network allocations or fragmentation after teardown;
- restore enough healthy post-Wi-Fi heap/largest block for BLE to return where safely affordable;
- keep Reader priority and existing BLE safety floors;
- no retry storms, floor bypasses, global purges, or background churn.

### 2. Reference-firmware audit: stock XTEINK and CrossLink

Use other firmware as evidence and design references, not as reasons to accumulate features.

#### Stock XTEINK X4

The manufacturer firmware source was not found publicly during this audit. Public official documentation and public firmware images can still be used for behavioral comparison, but do not treat closed binary code as reusable source.

Useful stock behaviors/ideas to benchmark:

- Hotspot-first local browser transfer at `192.168.3.3`;
- Bluetooth scanning/connection stability improvements documented in stock updates;
- EPUB punctuation, English word-spacing, paragraph-indentation, and punctuation-line-break handling documented in stock updates;
- JPG refresh/display performance;
- book-vs-chapter progress display;
- explicit index rebuild workflow;
- external-font performance tradeoffs.

Nooir already covers many stock conveniences in richer forms (button remapping, dark mode, auto page turn, custom sleep images, cache refresh, Bluetooth page turners, custom fonts, etc.). Only pursue stock-inspired work where it exposes a real Nooir weakness.

#### CrossLink

High-value comparisons for 1.6.4:

- File Transfer startup/shutdown ordering and memory behavior;
- its much leaner web-route set as a memory baseline;
- table/kerning/image regression EPUB fixtures already present in its repository;
- status-bar and typography behavior only where Nooir does not already have an equal/better implementation;
- any small parser/layout fixes that can be proven with fixtures and carried over cleanly.

Do not import CrossLink's different partition-table/install model into Nooir.

### 3. EPUB fidelity: surgical improvements only

Keep Nooir a fast book renderer, not a browser engine. Every addition must preserve X3/X4 responsiveness, bounded memory use, cache correctness, and readable fallback.

Use the Standard Ebooks edition of Theodore Roosevelt's *Through the Brazilian Wilderness* as one real-world regression/reference EPUB. It exercises small caps, structured chapter headings, verse/letter styling, many images, and complex appendix tables.

Candidate work, in priority order after the network regression is stable:

1. **Small caps**
   - investigate `font-variant: small-caps` support;
   - prefer a bounded, allocation-light representation;
   - do not require publisher font loading;
   - measure text-run splitting/layout cost before keeping it.

2. **Chapter heading / hgroup fidelity**
   - improve common book heading structures such as `hgroup` plus sibling heading text;
   - investigate narrowly bounded child (`>`) and adjacent-sibling (`+`) selector support where it materially improves real EPUBs;
   - avoid a general-purpose selector engine.

3. **Verse / letters / block typography**
   - improve hanging indents, margins, alignment, and spacing where these map cleanly onto the existing BlockStyle model;
   - preserve current text-first fallback.

4. **Tables**
   - keep the current bounded flowing-row renderer as the safe baseline;
   - consider lightweight `colspan` awareness or readability improvements only if cheap;
   - do not build a browser-style table grid engine for 1.6.4.

Explicitly out of scope for now:
- full CSS/browser compatibility;
- CSS floats;
- true floated drop-cap layout;
- general pseudo-element support;
- full descendant/child/sibling selector engine;
- publisher `@font-face` loading;
- complex table geometry, rowspan/colspan grid layout;
- flexbox/grid/multi-column rendering.

### 4. Deterministic release builds

Make generated HTML/gzip assets reproducible by using deterministic gzip timestamps (for example `gzip.compress(..., mtime=0)` where appropriate), then verify two clean `gh_release` builds from identical sources produce identical firmware hashes.

### 5. Bluetooth Page Turner follow-up

Bluetooth remains Beta.

Potential 1.6.4 work after Wi-Fi recovery:
- broader controller compatibility;
- easier mapping/setup;
- consider a bounded raw-HID learner using fixed storage only;
- improve BLE/Wi-Fi lifecycle polish and user feedback.

Do not weaken the 1.6.3 Reader-first memory policy.

### 6. EPUB/CSS hardening backlog

Only evidence-driven fixes:
- malformed XHTML/entity edge cases;
- empty inline/span spacing regressions;
- KOReader oversized-response handling where relevant;
- targeted parser/cache fixes that apply cleanly;
- torture EPUB fixtures for small caps, headings, verse, tables, malformed markup, huge images, RTL/LTR mixtures, and cache interruption.

Avoid broad renderer rewrites.

### 7. CBZ / manga

Still experimental. Candidate work:
- high-resolution page performance;
- first-page/startup latency;
- safer/smarter prefetch;
- optional web-assisted preprocessing;
- quality improvements that do not increase Reader memory pressure.

### 8. Hardware validation

- X4 old model remains primary physical target.
- X3 remains compatibility floor; formal physical validation is still needed.
- X4 Pro / X4 Classic remain investigation items, not officially supported targets yet.

### 9. Diagnostics cleanup

Retain the useful allocator/BLE/network diagnostics while 1.6.4 lifecycle work is active. Trim or gate forensic logging later only after the replacement behavior is proven on hardware.

## Deferred / not implemented

- PDF reading
- FB2 reading
- full system-wide dark UI
- full browser-grade EPUB CSS
- official X4 Pro / X4 Classic support

## Acceptance rule

A 1.6.4 change is worth keeping only when it improves a real book or lifecycle failure without making ordinary reading slower, reducing safe heap margin materially, increasing fragmentation/churn, or making cache/recovery behavior less predictable.

Reader/page rendering remains the highest priority. Optional services must yield under pressure.
