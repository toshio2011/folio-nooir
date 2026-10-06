# Folio Nooir 1.6.4 Status

Current phase: first post-1.6.3 regression investigation.

## Release baseline

- Folio Nooir 1.6.3 is released and closed.
- Reader-first BLE memory policy is shipped and physically exercised on the old X4.
- Bluetooth Page Turner Beta, Generic HID, Yiser J6, BLE auto-off/reconnect, CSS pressure reclaim, sleep/wake cleanup, EPUB/CSS/image improvements, Arabic/RTL, library layouts, stats, To-Do, web UI, OTA, simulators, and experimental CBZ are the current baseline.
- 1.6.4 work starts with regressions/lifecycle cleanup before new rendering features.

## Current blocker: File Transfer reboot

Released 1.6.3 on X4 has a repeatable File Transfer regression. After joining Wi-Fi, the device reboots after a few seconds and the browser interface never becomes practically usable. This is not a Settings-page-only bug.

The same File Transfer/web-server architecture worked in 1.6.2. Comparison shows the core flow is still essentially:

`Wi-Fi -> mDNS -> CrossPointWebServer -> HTTP/WebDAV/WebSocket/UDP -> serve`

The rich route set, WebDAV, WebSocket, UDP discovery, automatic Clock/Weather sync, and the general web feature set already existed in 1.6.2. They can still create peak pressure, but they are not by themselves the new regression delta.

`CrossPointWebServerActivity::onExit()` still performs a `silentRestart()` after Wi-Fi was activated. Therefore a graceful startup failure can look like a mysterious clean reboot. A real panic/OOM/watchdog/brownout reset is also still possible and must be distinguished from the intentional restart.

## Strong new lead: BLE changes the boot-time memory budget even when Bluetooth is OFF

Source audit tonight found a concrete 1.6.2 -> 1.6.3 difference that can explain why an unchanged File Transfer path lost headroom.

Both releases use the same pioarduino ESP32 platform/core version, so this is not a framework-version regression.

In Arduino-ESP32 3.3.7, `initArduino()` releases reserved Bluetooth controller memory at boot when no Bluetooth library reports itself in use. The Arduino Bluetooth shim documents this as roughly 36 KB of memory returned to the heap.

NimBLE-Arduino includes Arduino's BT-memory marker header. Merely linking NimBLE therefore marks the Bluetooth library as in use before `app_main()`, preventing Arduino from performing that boot-time release.

That produces an important release difference:

- 1.6.2 `gh_release`: no NimBLE in the release environment -> Arduino can return the otherwise-unused BT controller reservation to general heap at boot.
- 1.6.3 `gh_release`: NimBLE is linked -> Arduino intentionally keeps that controller memory available so BLE can be initialized/reinitialized later.

This applies even when `SETTINGS.bluetoothEnabled == 0` and the user has never turned Bluetooth on.

Nooir's normal `bleinput::stop()` / `BleKeyboardHost::end()` is still correct for runtime teardown: it stops scan/connect work, deletes the client/task, and calls `NimBLEDevice::deinit(true)`. That returns dynamic NimBLE/controller runtime allocations. But it cannot undo the boot-time decision that kept the controller's reusable memory region reserved for future BLE use.

This is now the leading source-backed explanation for why the same old File Transfer startup can have substantially less headroom in 1.6.3.

It is still a hypothesis until a physical A/B test proves the File Transfer behavior and heap numbers line up.

## Promising experiment for File Transfer

File Transfer does not need Bluetooth, and the activity already intentionally reboots when the network session ends. That gives us a useful experiment:

1. User chooses Join Network or Create Hotspot.
2. Fully stop/deinit NimBLE.
3. Confirm the BT controller is idle.
4. Release the BLE/controller memory for the remainder of that boot.
5. Log free heap + largest block before/after the release.
6. Start Wi-Fi and the web server.
7. On leaving File Transfer, keep the existing silent restart so the next boot restores normal Bluetooth capability.

ESP-IDF documents controller/BT memory release as irreversible until reboot, so this must be scoped to the network session and guarded carefully.

Do **not** release BT memory as soon as File Transfer opens: the user can still back out of the mode-selection screen before Wi-Fi activates. Release it only after a network mode is chosen, and track a `btMemoryReleasedForNetwork` state so exiting after a partial/failed start still forces the reboot needed to restore BLE capability.

This should first be tested on a separate experimental branch, not merged directly into the release line.

## What tonight's audit deprioritized

- **ESP32/Arduino core upgrade:** not the cause; 1.6.2 and 1.6.3 use the same platform package.
- **Settings web page:** not the general cause; the whole browser session dies.
- **Auto Clock/Weather sync:** existed in 1.6.2; current changes are mainly diagnostics and one-shot NTP cleanup support for other callers.
- **Obvious BLE re-arm during File Transfer:** low suspicion. File Transfer is Bluetooth-resource-sensitive, BLE eligibility requires Wi-Fi OFF, and ActivityManager also asks BLE to stop before entering resource-sensitive activities.
- **Custom interface font loading:** lower priority unless the failing unit actually has a custom UI font selected; the default interface font setting is empty.
- **Broad CrossLink/XTEINK audit:** already done. Only return to those references for targeted questions.

## Tomorrow: parallel Codex jobs

Use separate branches/worktrees so the jobs do not edit the same files.

### Job A — diagnostic build

No behavior fix yet. Add bounded INFO checkpoints for:
- boot reset reason + Nooir silent-restart flag;
- File Transfer enter;
- BLE stop begin/end/duration;
- BT controller state;
- free heap + largest block before/after BLE stop;
- Wi-Fi scan/connect begin/success;
- before/after auto Clock/Weather sync;
- WifiSelection exit/destroy;
- mDNS;
- WebServer allocation;
- route registration;
- WebDAV;
- HTTP begin;
- WebSocket;
- UDP;
- `SERVER_RUNNING`;
- File Transfer exit reason before restart.

### Job B — File-Transfer-only BT-memory-release experiment

Separate branch:
- release BLE/controller memory only after the user commits to a network mode;
- verify controller idle first;
- log reclaimed free heap/largest block;
- prevent BLE from being rearmed in that session;
- force the existing restart on exit whenever BT memory was irreversibly released;
- preserve normal BLE behavior after reboot.

Start conservatively with the smallest correct release API and measure it. Do not assume generic ESP-IDF documentation numbers equal the exact Nooir/X4 gain.

### Job C — BLE-off 1.6.3 A/B control

Diagnostic-only build from the 1.6.3 release source with the release BLE/NimBLE capability compiled out, while otherwise keeping the source as close as possible.

Compare:
- boot/home heap;
- largest block;
- File Transfer startup;
- physical reboot behavior.

If that build restores 1.6.2-like File Transfer stability, it strongly confirms that compiled-in Bluetooth memory budget is the regression boundary. This is not a proposal to remove Bluetooth from Nooir.

### Optional Job D — deterministic builds

Independent safe work:
- make generated gzip/HTML assets deterministic (`mtime=0` where appropriate);
- build twice from identical sources;
- require identical firmware hashes.

Do not start EPUB feature implementation in parallel with the blocker fix yet; keep physical testing focused.

## Second network task: Wi-Fi -> BLE recovery

After File Transfer itself is stable, return to the separate known issue where Wi-Fi teardown leaves total free heap/largest block below the pre-Wi-Fi baseline and BLE therefore correctly stays off.

Run repeated Wi-Fi cycles without sleep/reboot to distinguish:
- one-time networking residency;
- cumulative leak;
- one-time residency plus smaller fragmentation/leak.

Do not lower BLE safety floors.

## After the network work

1. Deterministic release builds.
2. EPUB small caps.
3. Better chapter heading / `hgroup` fidelity.
4. Better verse/letter typography.
5. Lightweight table readability improvements only.
6. Permanent EPUB regression/torture set, including Standard Ebooks *Through the Brazilian Wilderness*.
7. Evidence-driven parser/cache hardening.
8. CBZ high-resolution/startup/prefetch improvements.
9. More Bluetooth controllers and easier mapping; bounded learner only if lightweight.
10. Fuller physical X3 validation.
11. X4 Pro / X4 Classic investigation later.
12. Trim/gate forensic diagnostics after the lifecycle work is proven.

## Already-audited references

Stock XTEINK and CrossLink were already reviewed broadly for 1.6.4 planning. Do not repeat the broad audit.

Use them only when a specific question comes up:
- stock XTEINK: behavioral reference, including simple hotspot/browser transfer and rendering behavior;
- CrossLink: MIT-licensed implementation reference for targeted lifecycle/server/parser comparisons.

## Deferred

- PDF
- FB2
- full system-wide dark UI
- browser-grade EPUB CSS
- CSS floats / true floated drop caps
- publisher `@font-face`
- complex browser table engine
- official X4 Pro / X4 Classic support

## Guardrail

Reader/page rendering remains Nooir's highest priority. Keep fixes bounded, measurable, and reversible. Optional services must yield under memory pressure; do not hide failures with retry storms, safety-floor bypasses, or broad purges.
