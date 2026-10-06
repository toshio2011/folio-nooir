# Folio Nooir 1.6.4 Status

Current phase: first post-1.6.3 regression investigation.

## Completed baseline

- Folio Nooir 1.6.3 is released and closed.
- Reader-first BLE memory policy is shipped.
- Bluetooth Page Turner is shipped as Beta.
- Generic HID and Yiser J6 support are in the released baseline.
- BLE auto-off, reconnect lifecycle, Reader-aware stop/restart, CSS pressure reclaim, sleep/wake cleanup, To-Do redesign, library layouts, EPUB/CSS improvements, CBZ experimental support, stats, web UI, OTA, and simulators are part of the current baseline.
- The physical X4 test campaign showed stable Reader/BLE cycling, safe CSS reclaim, and healthy sleep/wake recovery.
- The known post-Wi-Fi heap recovery limitation is accepted as post-1.6.3 debt rather than a 1.6.3 release blocker.

## Immediate blocker: File Transfer reboot

Released 1.6.3 on X4 has a repeatable File Transfer failure: after connecting to Wi-Fi, the device can reboot after a few seconds and the web interface never becomes usable. This was reported by a community user and is also the current local symptom to investigate.

### What we know now

- This is not yet proven to be the same problem as the known post-Wi-Fi BLE recovery issue.
- The basic File Transfer/web-server flow and automatic Clock/Weather sync after connection already existed in 1.6.2.
- BLE/NimBLE in the normal release is new to the 1.6.3 line and changes overall runtime memory shape, although File Transfer is marked resource-sensitive and BLE should stop before network work.
- The current Nooir web server starts many more management endpoints than the lean CrossLink reference: roughly 37 explicit Nooir HTTP route registrations versus roughly 14 in CrossLink, before WebDAV/WebSocket/UDP. Startup heap/largest-block pressure is therefore a serious suspect.
- File Transfer currently calls `silentRestart()` on exit after Wi-Fi has been activated. If web-server startup fails and calls `onGoHome()`, this existing cleanup behavior can make a server-start failure look like a mysterious clean reboot.
- A real panic/OOM/watchdog reset is still possible; reset-reason evidence is required.

### External reference audit

**Stock XTEINK X4**
- Official documentation uses a device-created Hotspot for local browser transfer (`E-Paper` Wi-Fi, browser at `192.168.3.3`).
- This gives us a useful control test: AP/Hotspot vs Join Network can separate common web-server failures from STA-specific failures.
- Stock source was not found publicly. Use stock behavior/update notes as comparison evidence, not reusable source code.
- Interesting future comparison points include stock Bluetooth stability, EPUB punctuation/word-spacing/line-break handling, JPG refresh performance, index rebuild, and book-vs-chapter progress display.

**CrossLink**
- MIT-licensed and suitable as an implementation reference with attribution/license preservation where code is reused.
- Current shutdown path cleanly stops web server -> mDNS/DNS -> disconnect -> `WiFi.mode(WIFI_OFF)` and returns without a forced reboot.
- Its leaner web server is useful as a startup-memory baseline.
- Its repository also contains regression EPUB fixtures for tables, images, and kerning/ligatures that may be useful later for Nooir tests.

## Next physical X4 session — exact order

1. Test released 1.6.3 **File Transfer -> Create Hotspot** and leave it running.
2. Test released 1.6.3 **File Transfer -> Join Network -> connect -> leave it alone**.
3. Interpret the split:
   - Hotspot stable + STA reboot: focus on STA/auto-sync/mDNS/transition path.
   - both reboot: focus on common web-server startup/memory path.
4. If practical, repeat on 1.6.2 with the same X4/network to establish a regression boundary.
5. Build one bounded diagnostic firmware with INFO checkpoints at:
   - File Transfer enter;
   - Wi-Fi connected;
   - before/after auto sync;
   - before/after mDNS;
   - before web-server construction;
   - after WebServer allocation;
   - after route registration;
   - after WebDAV;
   - after HTTP begin;
   - after WebSocket;
   - after UDP;
   - SERVER_RUNNING;
   - File Transfer exit reason.
6. Log boot/reset reason to distinguish intentional silent restart vs panic/assert/watchdog/brownout/other reset.
7. Reproduce once with serial connected and fix only the stage proven to fail.

Do not remove useful web settings, weaken BLE safety floors, or add retry/restart loops merely to hide the failure.

## Second network task: post-Wi-Fi BLE recovery

Physical X4 cancel-path evidence still shows post-Wi-Fi free heap/largest block below the pre-Wi-Fi baseline, so BLE correctly remains off until sleep/wake restores the heap.

After File Transfer is stable, run repeated Wi-Fi cycles without reboot to distinguish:
- one-time networking residency;
- cumulative leak;
- one-time residency plus smaller fragmentation/leak.

Then improve recovery without lowering the existing BLE floors.

## Not done yet

1. Diagnose and fix the X4 File Transfer reboot so both useful transfer/setup paths are stable.
2. Fix post-Wi-Fi heap/largest-block recovery so BLE can return without sleep/restart where safely affordable.
3. Make release builds deterministic and verify identical-source firmware hashes.
4. EPUB fidelity follow-up: small caps, common chapter/hgroup styling, verse/letter typography, and lightweight table readability improvements where cheap.
5. Add targeted EPUB regression/torture fixtures, including the Standard Ebooks *Through the Brazilian Wilderness* reference book and useful CrossLink fixtures where appropriate.
6. Continue evidence-driven parser/cache hardening only where real fixtures justify it.
7. Expand Bluetooth controller support and mapping UX after lifecycle work is stable.
8. Improve CBZ high-resolution performance/startup/prefetch without harming Reader memory.
9. Perform fuller physical X3 validation.
10. Investigate X4 Pro / X4 Classic later; not officially supported yet.
11. Trim/gate forensic diagnostics after 1.6.4 lifecycle work is proven.

## Deferred beyond 1.6.4 unless priorities change

- full browser-grade EPUB CSS
- floats / true floated drop caps
- publisher @font-face loading
- complex table-grid engine
- PDF
- FB2
- full system-wide dark UI

## Guardrail

Nooir remains a constrained e-ink book renderer. A feature is accepted only if it improves real reading or fixes a real lifecycle problem without making ordinary page rendering slower, reducing safe heap margin materially, or adding uncontrolled background work/churn.
