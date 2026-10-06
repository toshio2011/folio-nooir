# Folio Nooir 1.6.4 Plan

Status: active post-1.6.3 work.

## Release state

Folio Nooir 1.6.3 is released and closed. All normal work below belongs to 1.6.4 or later.

Released 1.6.3 artifact:
- firmware.bin size: 5,788,880 bytes
- SHA-256: A843B52D976B547FA8E60ECC5DC544F826F7A7725AFEDE3FEE6245ACE06E3FA8

## Simple order of work

### Now
1. Fix the 1.6.3 File Transfer reboot.
2. Fix post-Wi-Fi memory recovery so BLE can return safely without needing sleep/reboot where affordable.

### After network stability
3. Make release builds deterministic.
4. Improve EPUB rendering: small caps, chapter headings/hgroup, verse/letters, lightweight table polish.
5. Build/expand the EPUB regression set and do fixture-driven parser/cache hardening.

### Later in 1.6.4
6. Improve CBZ performance.
7. Broaden Bluetooth controller support/mapping UX.
8. Perform fuller physical X3 validation.
9. Keep using stock XTEINK/CrossLink only for targeted comparisons.

### Later beyond that
- X4 Pro / X4 Classic investigation;
- PDF / FB2;
- full system dark UI;
- advanced browser-grade EPUB layout features.

## 1. File Transfer reboot — first task

Released 1.6.3 can reboot a few seconds after joining Wi-Fi from File Transfer. The web interface never becomes practically usable. Treat this as a general File Transfer startup/runtime regression, not a Settings-page-only problem.

### What comparison with 1.6.2 tells us

- 1.6.2 and 1.6.3 use the same pioarduino ESP32 platform/core package, so this is not a framework-version upgrade regression.
- The core File Transfer/web-server architecture already worked in 1.6.2.
- The rich HTTP route set, WebDAV, WebSocket, UDP discovery, mDNS flow, and automatic Clock/Weather sync also existed in the working release.
- File Transfer still intentionally `silentRestart()`s after a network session, so a graceful startup failure can masquerade as a mysterious reboot.
- 1.6.3's major release-runtime delta is compiled-in NimBLE/BLE plus its coexistence lifecycle and associated memory budget.

### Strong source-backed lead: boot-time BLE memory reservation

Arduino-ESP32 releases reserved Bluetooth controller memory at boot when no Bluetooth library reports itself in use. Its own Bluetooth shim documents the returned amount as roughly 36 KB.

NimBLE-Arduino marks Bluetooth as in use before `app_main()`, so merely linking NimBLE prevents that boot-time release. Therefore:

- 1.6.2 `gh_release`, which does not link NimBLE, can give the unused controller reservation back to the general heap at boot;
- 1.6.3 `gh_release`, which links NimBLE so Bluetooth can be started later, intentionally keeps that memory available to Bluetooth for the whole boot.

This is true even if the user's Bluetooth setting is OFF and the BLE host has never been started.

Nooir's `bleinput::stop()` and FreeInk's `BleKeyboardHost::end()` still correctly tear down active NimBLE runtime state with `NimBLEDevice::deinit(true)`. That does not reverse the earlier boot decision to preserve reusable BT controller memory for future BLE starts.

This is now the leading explanation for why an otherwise familiar File Transfer path may have lost enough heap/largest-block headroom to fail in 1.6.3. It still needs physical A/B proof.

### Best experimental fix to test

File Transfer does not need Bluetooth and already ends with a reboot, so test a network-session-only irreversible BT memory release:

1. Wait until the user chooses Join Network or Create Hotspot.
2. Fully stop/deinit BLE.
3. Verify the Bluetooth controller is idle.
4. Release the BLE/controller memory for the remainder of that boot.
5. Log free heap + largest block before/after.
6. Start Wi-Fi and the web server normally.
7. On File Transfer exit, restart so Bluetooth capability is restored on the next boot.

Because ESP-IDF documents this memory release as irreversible until reboot, track a `btMemoryReleasedForNetwork` flag and force restart on any exit after the release, including partial/failed network startup.

Do not release this memory merely on entering File Transfer, because the user can still back out before selecting a network mode.

Test this on a separate branch before considering it for production.

### Parallel Codex jobs for the next work session

Use separate branches/worktrees to avoid overlapping edits.

**Job A — diagnostic build**
- reset reason + Nooir silent-restart flag;
- File Transfer enter;
- BLE stop begin/end/duration;
- BT controller state;
- heap/largest before and after BLE stop;
- Wi-Fi scan/connect begin/success;
- before/after Clock/Weather sync;
- WifiSelection exit/destroy;
- mDNS;
- WebServer allocation;
- route registration;
- WebDAV;
- HTTP begin;
- WebSocket;
- UDP;
- `SERVER_RUNNING`;
- File Transfer exit reason.

**Job B — File-Transfer-only BT-memory-release experiment**
- implement the guarded release described above;
- measure actual reclaimed heap/largest block on X4;
- ensure BLE cannot be rearmed before the required restart.

**Job C — BLE-off 1.6.3 A/B control**
- build the same 1.6.3 source with release BLE/NimBLE compiled out;
- compare boot/home heap, largest block, and File Transfer behavior;
- diagnostic only: do not treat this as a proposal to remove Bluetooth.

**Optional Job D — deterministic builds**
- make generated gzip/HTML output deterministic (`mtime=0` where appropriate);
- build twice from identical sources and require identical firmware hashes.

Do not start EPUB feature implementation in parallel with the blocker investigation; keep physical testing focused.

### Other suspects tonight's audit moved down the list

- broad Settings-page/API bug;
- Arduino/core version mismatch;
- auto Clock/Weather being a brand-new behavior;
- obvious BLE re-arm during active File Transfer;
- custom interface-font loading on default settings.

Keep these available if the physical logs contradict the leading memory-budget hypothesis.

## 2. Post-Wi-Fi BLE recovery

Separate from the reboot bug.

Physical X4 evidence already showed roughly:
- ~96.5 KiB free after BLE stop / before Wi-Fi;
- ~48.9 KiB at a captured Wi-Fi low point;
- ~84-85 KiB after Wi-Fi OFF / selection teardown;
- BLE correctly stays below its admission floors;
- sleep/wake restores the healthy baseline.

After File Transfer is fixed, run repeated Wi-Fi cycles without reboot:
- ~96 -> ~84 -> ~84 -> ~84 KiB suggests one-time network residency;
- ~96 -> ~84 -> ~72 -> ~60 KiB suggests a recurring leak;
- large first drop + tiny later drops suggests residency plus smaller fragmentation/leak.

Goal: recover enough healthy heap/largest block for BLE to return when safely affordable without weakening the existing Reader-first BLE floors.

## 3. Deterministic release builds

- use deterministic gzip timestamps where appropriate;
- make two clean `gh_release` builds from identical source;
- require identical firmware hashes.

## 4. EPUB fidelity — surgical only

Use Standard Ebooks *Through the Brazilian Wilderness* as a permanent real-world reference.

### Small caps
Investigate `font-variant: small-caps` with bounded, allocation-light handling.

### Chapter headings / hgroup
Improve common book heading structures. Consider narrowly bounded child (`>`) and adjacent-sibling (`+`) selector support only where fixtures prove value.

### Verse / letters
Improve hanging indents, margins, spacing, alignment, signatures/salutations where the current block model supports them cheaply.

### Tables
Keep the safe flowing-row fallback. Consider only small readability improvements or lightweight `colspan` awareness.

Not planned: full browser CSS, floats, true floated drop caps, publisher `@font-face`, complex table grid geometry, flexbox/grid/multi-column layout.

## 5. EPUB regression / hardening

Keep/add fixtures for:
- small caps;
- chapter headings/hgroup;
- verse/letters;
- tables;
- huge images/JPG-heavy pages;
- malformed XHTML/entities;
- empty inline/span spacing;
- unknown XML tags;
- RTL/LTR mixtures;
- cache interruption/low-memory fallback;
- stock-XTEINK-inspired punctuation/word-spacing/line-break cases;
- useful CrossLink table/image/kerning fixtures where provenance/licensing is clear.

Carry only fixture-proven parser/cache fixes, including KOReader oversized-response handling where relevant. No broad renderer rewrite.

## 6. CBZ / manga

Still experimental. Later 1.6.4 candidates:
- high-resolution page performance;
- faster first page/startup;
- safer/smarter prefetch;
- optional web-assisted preprocessing;
- quality/performance balance without stealing EPUB/Reader memory.

## 7. Bluetooth follow-up

Bluetooth remains Beta. After lifecycle stability:
- more controller compatibility;
- easier mapping/setup;
- possibly a bounded fixed-storage raw-HID learner;
- better BLE/Wi-Fi lifecycle feedback.

## 8. Hardware validation

- old X4 remains primary physical target;
- X3 remains compatibility floor; full physical regression validation still needed;
- X4 Pro / X4 Classic remain later investigation targets.

## 9. Reference firmware

Broad audits are already done. Do not repeat them.

Use only targeted references:
- stock XTEINK: behavioral reference, including hotspot/browser transfer and rendering behavior;
- CrossLink: MIT-licensed implementation reference for specific lifecycle/server/parser questions.

## 10. Diagnostics cleanup

Keep useful allocator/BLE/network diagnostics while 1.6.4 lifecycle work is active. Trim/gate them only after replacement behavior is physically proven.

## Acceptance rule

A 1.6.4 change is worth keeping only when it fixes a real lifecycle failure or improves a real book without making ordinary reading slower, materially reducing safe heap margin, increasing fragmentation/churn, or making recovery/cache behavior less predictable.

Reader/page rendering remains highest priority. Optional services must yield under pressure.
