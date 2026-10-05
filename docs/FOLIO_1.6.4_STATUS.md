# Folio Nooir 1.6.4 Status

Current phase: planning / post-1.6.3.

## Completed baseline

- Folio Nooir 1.6.3 is released and closed.
- Reader-first BLE memory policy is shipped.
- Bluetooth Page Turner is shipped as Beta.
- Generic HID and Yiser J6 support are in the released baseline.
- BLE auto-off, reconnect lifecycle, Reader-aware stop/restart, CSS pressure reclaim, sleep/wake cleanup, To-Do redesign, library layouts, EPUB/CSS improvements, CBZ experimental support, stats, web UI, OTA, and simulators are part of the current baseline.
- The physical X4 test campaign showed stable Reader/BLE cycling, safe CSS reclaim, and healthy sleep/wake recovery.
- The known post-Wi-Fi heap recovery limitation is accepted as post-1.6.3 debt rather than a 1.6.3 release blocker.

## New community report to investigate

- Released 1.6.3 on X4: one user reports that after connecting to Wi-Fi, the device reboots after a few seconds, every time, preventing use of the web interface for setup.
- Root cause is unknown and must not be assumed to be the same as the known post-Wi-Fi BLE heap-recovery issue.
- Need reproduction and serial/reset-reason evidence, including heap/largest-block history and Wi-Fi/web-server/BLE state around the reboot.

## Not done yet

1. Investigate and fix the reported X4 Wi-Fi-connect reboot regression so a user can connect and keep the web interface usable.
2. Fix post-Wi-Fi heap/largest-block recovery so BLE can return without sleep/restart.
3. Make release builds deterministic and verify identical-source firmware hashes.
4. EPUB fidelity follow-up: small caps, common chapter/hgroup styling, verse/letter typography, and lightweight table readability improvements where cheap.
5. Add targeted EPUB regression/torture fixtures, including the Standard Ebooks *Through the Brazilian Wilderness* reference book.
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

Nooir remains a constrained e-ink book renderer. A feature is accepted only if it improves real reading without making ordinary page rendering slower, reducing safe heap margin materially, or adding uncontrolled background work/churn.
