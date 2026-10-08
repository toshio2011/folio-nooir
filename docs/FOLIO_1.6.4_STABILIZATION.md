# Folio Nooir 1.6.4 Stabilization Checkpoint

**Date:** 8 October 2026

This note preserves the current 1.6.4 reconstruction/stabilization state so the BLE and Web UI investigation can be resumed without relying on chat history.

## Recovery / repository checkpoint

- Canonical released 1.6.3 base: `3779e9944d829e02b6d1b0fab0a8482a47805446`.
- Clean reconstruction workspace used for 1.6.4: `C:\Users\fatiha\Documents\Codex\folio-nooir-1.6.4-reconstructed`.
- Reconstruction branch: `reconstruct/folio-nooir-1.6.4`.
- Pre-final-fix recovery checkpoint commit: `d9af683b6f9758c479fefe43aaecc0460c0b5803` (`Prepare Folio Nooir 1.6.4 release candidate`).
- That checkpoint was pushed to the local `origin` (`D:\fatiha\side`) and not yet to the hosted GitHub reconstruction branch.
- `sdkconfig.gh_release` was intentionally left local/untracked at that checkpoint.

The `d9af683` checkpoint is the safe recovery point before the final BLE renderer-coexistence and Web UI reset fixes.

## Hard engineering rules

- No feature removal to solve memory pressure.
- No Recent Books cap.
- Do not lower BLE safety floors.
- No duplicate `esp_wifi_deinit()`.
- No global/random `shrink_to_fit()` or blind purge.
- No polling loops or arbitrary delays for BLE re-admission.
- Reader performance/snappiness is a hard requirement.
- Reader has priority over BLE for genuinely unsafe/heavy work.
- BLE may temporarily disconnect for heavy work but must return automatically when memory is safe.
- Preserve all Web UI functionality.
- Preserve the bounded EPUB architecture: no full DOM, browser CSS engine, deep/unbounded selector engine, or retained full-document transformed copies.

## BLE safety floors

Cold BLE admission floors remain unchanged:

- free heap: **98,304 bytes (96 KiB)**
- largest contiguous block: **36,852 bytes (~36 KiB)**

Do not weaken these values merely to force reconnects.

## Critical acceptance rule: BLE OFF means BLE-less Nooir

When Bluetooth/BLE is disabled in Settings, Nooir must behave as though BLE support does not exist.

Required behavior when BLE is OFF:

- no NimBLE initialization;
- no scan/connect attempts;
- no reconnect attempts;
- no BLE cold-admission checks;
- no BLE wanted/pending work;
- no 98,304/36,852 admission floors affecting Reader decisions;
- no BLE-specific Reader teardown or render gating;
- no BLE memory reservation purely for coexistence;
- no BLE-specific lifecycle interference with Home, Recent Books, Library, Settings, Reader, Reader Settings, sleep, Wi-Fi, File Transfer, Web Settings, or Reading Stats.

If BLE is switched OFF while active:

1. disconnect cleanly;
2. stop NimBLE completely;
3. release BLE resources;
4. clear reconnect/pending intent appropriately;
5. continue as normal non-BLE Nooir.

Turning BLE back ON may enter the normal admission/connect lifecycle.

Existing user-facing BLE connection/configuration flows on Home/Settings/Reader-related UI should remain available when BLE is enabled. The coexistence work must not redesign or remove that UX.

## Reader BLE lifecycle — confirmed progress

### Old normal-page churn root cause

The previous unconditional post-input handoff in `src/main.cpp` effectively did:

`pollBle()` -> `bleHadActivityThisFrame()` -> `bleinput::stop()`

This ran before Reader classified the action, so every BLE page-turn could tear down NimBLE even when the page was already cached/lightweight.

That unconditional handoff and its dedicated pending flag were removed.

**Do not restore this path.**

### Physical confirmation: cached-page retention works

The latest physical log proves the normal-page fix is real:

- BLE can reconnect after Reader settles;
- BLE remains running and connected through a normal cached page render;
- there is no unconditional post-input stop on that cached page.

Therefore the target architecture remains:

- **cached/light page:** keep BLE resident;
- **heavy/new section/image/layout work:** BLE may stop temporarily;
- **after cleanup:** re-admit BLE only when the safety floors pass.

## Current BLE blocker 1: renderer grayscale scratch

With BLE resident, physical logs now show:

`OOM: grayscale strip scratch (8000 bytes); skipping AA this page`

Observed around the failure:

- BLE connected idle around 33 KiB free / 30 KiB largest block;
- cached Reader page renders while BLE remains connected;
- renderer cannot obtain its 8 KiB grayscale/AA scratch allocation;
- after the page, free heap is about 30 KiB but largest block can collapse to about 16 KiB;
- minimum free observed near 10 KiB.

This means the latest BLE retention fix exposed a real renderer coexistence problem: ordinary cached-page rendering must not depend on obtaining a fresh 8 KiB contiguous allocation from a heap already fragmented by resident NimBLE.

### Preferred solution direction

Investigate the exact scratch allocation and related render-time temporaries.

Preferred order:

1. **Reusable/preallocated bounded renderer scratch** allocated at a controlled lifecycle point while BLE is not competing for the heap.
   - Possible owner: renderer or Reader session.
   - Cached pages reuse the existing scratch instead of allocating 8 KiB on demand.
   - Keep the persistent cost bounded and justified.
2. If reusable scratch is not safe, use a **render preflight** that only stops BLE when the required scratch/headroom is unavailable.

Do **not** regress to stopping BLE on every page.

## Current BLE blocker 2: largest-block recovery

After a heavy section transition BLE correctly stops, but total free heap can recover without the largest contiguous block recovering enough for BLE.

Physical examples:

- total free ~98–102 KiB while largest remains ~34,804 bytes;
- after worse fragmentation, total free ~98 KiB while largest remains ~25,588 bytes;
- immediately after some BLE stops the largest block has been around ~21 KiB.

Because the largest-block floor is 36,852 bytes, cold admission correctly remains blocked.

This is a heap-shape/allocation-order problem, not simply a total-free problem.

Investigate live/rebuildable allocations surviving around:

- NimBLE stop;
- section destruction/build;
- renderer scratch/AA buffers;
- font and glyph caches;
- CSS retained storage;
- image decoder buffers;
- Reader exit;
- Recent Books / Folio Library entry;
- sleep cleanup.

Only claim a leak if allocation ownership proves it. Current evidence proves fragmentation/topology, not one specific permanent Reader leak.

Allowed targeted fixes include releasing/reordering/reusing a proven rebuildable allocation or allocating it before NimBLE. Avoid global purges.

## Heavy Reader paths that may stop BLE

The centralized lifecycle may still stop BLE for genuinely heavy/resource-sensitive work, including:

- initial unstable Reader open;
- section build/cache miss;
- new-section transition;
- partial rebuild;
- first-use image decode/cache miss;
- loading UI / quality-recovery / full-refresh work;
- queued work while renderer is already occupied;
- Reader exit/settings-triggered reflow;
- Wi-Fi / Web UI / sleep / OTA / other resource-sensitive activities.

The final implementation should keep these narrowly classified. A stable cached page is not automatically heavy.

## BLE wanted / pending re-admission

Preserve the existing intent model:

- if user wants BLE enabled, keep BLE intent true;
- heavy work may stop the host without losing user intent;
- failed floor admission must not clear pending intent;
- re-evaluate only on real lifecycle cleanup events;
- no timer/polling reconnect loop;
- clear pending only after BLE actually starts successfully.

Meaningful re-evaluation points can include render completion, section cleanup, Reader exit, Transfer exit, Settings exit, and other existing lifecycle boundaries.

## P0 Web UI / File Transfer fixes already present

Preserve the existing fixes:

- ownership-safe `closeFileIfOpen()` for WebSocket/HTTP/font uploads and server cleanup;
- fix for the startup `HalFile::close()` assertion on a default-constructed file;
- streamed `/api/stats` instead of full JSON plus a second serialized copy;
- stats scan reduction;
- Settings descriptor-copy reduction;
- upload buffers only while active;
- library JSON temporary reuse;
- thumbnail client/file cleanup;
- WebSocket upload cleanup;
- full HTTP/WS/UDP/WebDAV/upload/watchdog teardown before Wi-Fi shutdown;
- BLE re-evaluation after Transfer cleanup.

Web UI opening had previously been physically confirmed after the HalFile startup fix.

## Current Web UI blocker: Settings / Reading Stats reset the device

The latest user-visible symptom looked like Wi-Fi disconnecting when opening Web Settings or Reading Stats.

The serial log shows these are **full CPU resets**, not an ordinary Wi-Fi drop.

Two patterns are currently known.

### Pattern A — degraded post-Reader heap

Observed before/during Transfer/WebServer:

- Wi-Fi success around 46 KiB free / 25.6 KiB largest;
- CrossPointWebServer around 27.9 KiB free / 20.5 KiB largest;
- minimum free about 4.8 KiB;
- repeated missing `thumb_226.bmp` lookups;
- then core dump/reboot.

### Pattern B — fresh boot

After a fresh reboot:

- BLE is stopped for NetworkModeSelection;
- Wi-Fi connects successfully;
- selection handoff/destruction completes;
- device resets shortly afterward, before the usual periodic CrossPointWebServer memory log.

This suggests there may be a separate startup/lifetime/callback/task problem in addition to low-memory pressure.

### Panic identity to decode

Latest physical log reports:

- `ELF file SHA256: b27d8dfff`
- MEPC approximately `0x40389af6`
- RA approximately `0x40389ae6`
- MTVAL approximately `0x4038e3b0`

Decode these **only against the exact matching ELF/map**. Do not reuse historical symbolication from another candidate. If the matching ELF cannot be located, state that symbolication is unproven.

## Web UI investigation scope

Trace startup/lifetime across:

- NetworkModeSelection;
- Wi-Fi initialization;
- handoff into CrossPointWebServer;
- server creation/start;
- HTTP handlers;
- WebSocket handlers;
- WebDAV;
- mDNS/UDP;
- upload state;
- asynchronous callbacks;
- activity/task lifetime and stack usage;
- watchdog ownership.

Investigate null/invalid state, use-after-free, task stack corruption/overflow, callback lifetime, HalFile ownership, watchdog, and OOM. Do not assume every reset is simple heap exhaustion.

### Web Settings route

Verify Web Settings does not unnecessarily:

- deep-copy the full descriptor vector;
- build duplicate large JSON strings/documents;
- scan the library;
- request/generate thumbnails;
- retain upload buffers;
- load unrelated covers/fonts.

Use bounded/streamed output where appropriate while preserving all settings functionality.

### Reading Stats route

Verify the existing streaming fix remains intact:

- stream one book/stat row at a time;
- do not construct a full stats JSON plus serialized duplicate;
- avoid repeated Recent scans;
- avoid unnecessary thumbnail/cover work;
- avoid large retained metadata copies.

### Thumbnail pressure

Repeated missing `thumb_226.bmp` lookups occur during WebServer activity. Determine whether initial library rendering or duplicate browser requests are repeatedly driving expensive fallback/cache work. Keep cover functionality, but make failed/missing thumbnail handling bounded and promptly release files/clients.

## P1 EPUB work already present

Preserve the bounded P1 work:

- ASCII/Latin render-time small caps;
- `hgroup`;
- bounded `>` / `+` selector support;
- verse hanging indent;
- letter/signature alignment;
- separate table captions;
- bounded colspan advancement;
- figure/figcaption;
- full-page image isolation;
- CSS cache v14.

Simple EPUBs must keep the fast path. No DOM/general browser engine/deep selectors.

## P2 carryovers already included in the 1.6.4 candidate

Only these two P2 items were intentionally included:

1. **Empty CSS span / inline spacing**
   - empty inline elements queue transient `padding-left` geometry onto the next real token;
   - line breaking, hyphenation, RTL reorder and positioning account for it;
   - no synthetic DOM token or cache-format change.
2. **KOReader oversized `getProgress()` response handling**
   - bounded 8 KiB streaming response buffer;
   - reject oversized known-length or streaming responses;
   - reject incomplete/aborted bodies;
   - parse the bounded buffer directly without an extra `getString()` copy.

Do not pull broader P2 parser/settings/memory-policy work into the stabilization round unless it directly fixes a proven blocker.

## Last built 1.6.4 candidate before the latest blocker findings

The candidate built successfully before the latest physical test:

- version: `1.6.4`
- `firmware.bin`: 5,803,184 bytes
- SHA256: `7F9586900C9D4E4A5DA065E9E7E25AA155B9886795F1F8BB57B9E46D0D2EC2CE`
- app usage: 5,789,035 / 6,553,600 bytes (88.3%)
- static RAM: 60,996 / 327,680 bytes (18.6%)
- NimBLE-Arduino 2.5.1 and BleKeyboardHost 1.0.0 linked.

This build is **not release-ready** because the physical test exposed the renderer/BLE and Web UI reset blockers above.

## Required next sequence

1. Trace/fix renderer scratch coexistence without restoring every-page BLE teardown.
2. Trace/fix largest-block recovery using evidence-based allocation ownership/order.
3. Verify BLE OFF is a true BLE-less Nooir path.
4. Decode the Web UI reset against the exact `b27d8dfff` ELF/map.
5. Fix the WebServer startup / Settings / Reading Stats reset path.
6. Run `git diff --check` and focused tests/static checks.
7. Only when source is coherent, build **one** new `gh_release` candidate.
8. Physically test:
   - cached page turns keep BLE connected;
   - no grayscale scratch OOM;
   - heavy section work may stop BLE but it returns automatically when floors pass;
   - no progressive largest-block collapse;
   - BLE OFF behaves like BLE-less Nooir;
   - File Transfer/Web Settings/Reading Stats stay connected and do not reset;
   - Transfer exit cleans up and BLE returns when enabled/safe.
9. If all blockers pass, make a final checkpoint, push the hosted reconstruction state, and release 1.6.4 quickly rather than expanding scope.

## Explicit release gate

Do **not** release 1.6.4 until both are physically green:

- Reader BLE coexistence / recovery;
- Web UI Settings / Reading Stats stability.

Everything after that belongs in a later release unless it is required to fix a release blocker.
