# Folio Nooir 1.6.4 Status

Current phase: first post-1.6.3 regression investigation.

## Release baseline

- Folio Nooir 1.6.3 is released and closed.
- Reader-first BLE memory policy is shipped.
- Bluetooth Page Turner is shipped as Beta.
- Generic HID and Yiser J6 support are in the released baseline.
- BLE auto-off, reconnect lifecycle, Reader-aware stop/restart, CSS pressure reclaim, sleep/wake cleanup, To-Do redesign, library layouts, EPUB/CSS improvements, CBZ experimental support, stats, web UI, OTA, and simulators are part of the current baseline.
- Physical old-X4 testing showed stable Reader/BLE cycling, safe CSS reclaim, and healthy sleep/wake recovery.
- The known post-Wi-Fi BLE recovery limitation is accepted as post-1.6.3 debt rather than a 1.6.3 release blocker.

## Immediate blocker: File Transfer / web runtime reboot

Released 1.6.3 on X4 has a repeatable File Transfer regression. After joining Wi-Fi, the device reboots after a few seconds and the web interface never becomes usable. This is not limited to the web Settings page: nothing useful can be done in the browser before the device reboots.

### What we know now

- Treat this as a general File Transfer startup/runtime regression, not a Settings-endpoint-only bug.
- The same general File Transfer/web-server architecture worked in 1.6.2.
- Comparison against 1.6.2 shows the core `CrossPointWebServerActivity` startup flow is essentially the same: connect network -> mDNS -> construct/start `CrossPointWebServer` -> HTTP/WebDAV/WebSocket/UDP -> serve requests.
- The rich Nooir route set, WebDAV, WebSocket, UDP discovery, and the general web-management feature set were already present in 1.6.2. They remain useful pressure points to measure, but they are not by themselves the new regression delta.
- The web Settings HTML is also not the primary explanation because the device reboots before any web page becomes practically usable.
- Wi-Fi connection logic is largely inherited from the working 1.6.2 path. The 1.6.3 line mainly adds BLE coexistence/lifecycle work, network-memory diagnostics, and explicit retain/deinit ownership around `WifiSelectionActivity`.
- File Transfer is marked `bluetoothResourceSensitive()`, and ActivityManager is intended to stop BLE before entering it. We must verify on hardware that the BLE host has actually released its memory before Wi-Fi/server allocations begin.
- `CrossPointWebServerActivity::onExit()` still performs `silentRestart()` when Wi-Fi had been activated. Therefore a graceful server-start failure can look like a mysterious reboot. A real panic/OOM/watchdog/brownout reset is also still possible.

### Current leading hypothesis

The most plausible regression class is not "the web server feature changed" but "1.6.3 reaches the same old web-server startup with a different runtime/heap shape". BLE/NimBLE integration and other 1.6.3 runtime changes may leave less free heap or a smaller largest allocatable block even after BLE is asked to stop.

This remains a hypothesis until serial/reset-reason evidence proves the failing stage.

## Next physical X4 session — exact order

1. Start from released 1.6.3 and enter File Transfer.
2. Verify BLE is actually stopped/released before Wi-Fi and record free heap + largest block at File Transfer entry.
3. Reproduce Join Network -> connect -> leave it alone with serial attached.
4. If useful, also test Create Hotspot as a control path, but do not treat Hotspot as the main bug if the normal joined-network path already reproduces reliably.
5. If practical, repeat the same File Transfer flow on 1.6.2 on the same X4/network to establish a clean memory/reset baseline.
6. Use one bounded diagnostic build with INFO checkpoints at:
   - File Transfer enter;
   - BLE stop requested / BLE fully stopped;
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
7. Log boot/reset reason so the next boot distinguishes:
   - intentional Nooir silent restart;
   - panic/assert;
   - watchdog;
   - brownout;
   - other reset reason.
8. Fix only the stage the log proves is failing.

Do not remove useful web features, weaken BLE safety floors, or add restart/retry loops merely to hide the failure.

## Second network task: post-Wi-Fi BLE recovery

Physical X4 cancel-path evidence still shows post-Wi-Fi free heap/largest block below the pre-Wi-Fi baseline, so BLE correctly remains off until sleep/wake restores the heap.

After File Transfer is stable, run repeated Wi-Fi cycles without reboot to distinguish:
- one-time networking residency;
- cumulative leak;
- one-time residency plus smaller fragmentation/leak.

Then improve recovery without lowering the existing BLE floors.

## 1.6.4 work board

### P0 — must solve first

1. **File Transfer reboot regression**
   - identify exact reset reason and failing startup stage;
   - compare 1.6.2 vs 1.6.3 memory at the same checkpoints;
   - verify BLE really yields all releasable memory before network startup;
   - fix the proven cause and physically retest normal joined-network File Transfer.

2. **Post-Wi-Fi BLE recovery**
   - determine one-time network residency vs real leak/fragmentation;
   - improve teardown/recovery so BLE can return without sleep/restart when safely affordable;
   - keep Reader priority and the existing BLE floors.

### P1 — release engineering and EPUB fidelity

3. **Deterministic `gh_release` builds**
   - make generated gzip/HTML assets deterministic (`mtime=0` where appropriate);
   - verify two clean identical-source builds produce the same firmware hash.

4. **EPUB small caps**
   - investigate `font-variant: small-caps`;
   - keep only a bounded, allocation-light implementation.

5. **Chapter heading / `hgroup` fidelity**
   - improve common book heading structures;
   - investigate narrowly bounded child (`>`) and adjacent sibling (`+`) selector support only where real EPUBs benefit.

6. **Verse / letters / block typography**
   - improve hanging indents, margins, spacing, alignment, signatures/salutations where the current block model can support them cheaply.

7. **Tables**
   - retain the safe flowing-row fallback;
   - consider small readability improvements and lightweight `colspan` awareness;
   - no browser-style table engine.

8. **EPUB regression/torture set**
   - keep Standard Ebooks *Through the Brazilian Wilderness* as a real-world reference;
   - add fixtures for small caps, chapter headings, verse, letters, tables, huge images, malformed XHTML/entities, RTL/LTR mixtures, low-memory/cache interruption;
   - reuse useful CrossLink table/image/kerning fixtures where licensing/provenance is clear;
   - add stock-XTEINK-inspired checks for punctuation/word spacing/line breaks and JPG-heavy pages.

### P2 — hardening and feature polish

9. **Parser/cache hardening**
   - unknown XML-tag regression behavior;
   - empty CSS span / inline-spacing behavior;
   - KOReader oversized-response handling where relevant;
   - small parser/cache fixes only when fixtures prove value.

10. **Bluetooth follow-up**
    - more controller compatibility;
    - easier mapping/setup;
    - bounded raw-HID learner only if it can use fixed storage and stay lightweight;
    - BLE/Wi-Fi lifecycle polish and clearer feedback.

11. **CBZ / manga**
    - high-resolution page performance;
    - first-page/startup latency;
    - safer/smarter prefetch;
    - optional web-assisted preprocessing;
    - preserve EPUB/Reader memory priority.

12. **Physical X3 validation**
    - simulator/shared code already exists;
    - community evidence exists;
    - full physical regression pass is still outstanding.

13. **Diagnostics cleanup**
    - retain allocator/BLE/network diagnostics while lifecycle work is active;
    - later trim or gate forensic logging once replacement behavior is proven.

### P3 — investigate later

14. **X4 Pro / X4 Classic**
    - investigate hardware/firmware differences;
    - not officially supported yet.

15. **Reference-firmware audit**
    - continue using stock XTEINK and CrossLink as behavioral/implementation references where they expose a real Nooir weakness;
    - do not accumulate features simply because another firmware has them.

## Explicitly deferred / not implemented

- PDF reading
- FB2 reading
- full system-wide dark UI
- full browser-grade EPUB CSS
- CSS floats
- true floated drop caps
- general pseudo-elements
- full descendant/child/sibling selector engine
- publisher `@font-face`
- complex browser table-grid/rowspan/colspan geometry
- flexbox/grid/multi-column layout
- official X4 Pro / X4 Classic support

## Guardrail

Nooir remains a constrained e-ink book renderer. A 1.6.4 change is accepted only if it fixes a real lifecycle failure or improves a real book without making ordinary page rendering slower, reducing safe heap margin materially, increasing fragmentation/churn, or adding uncontrolled background work.

Reader/page rendering remains highest priority. Optional services yield under pressure.
