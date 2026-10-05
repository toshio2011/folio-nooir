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

## Not done yet

1. Fix post-Wi-Fi heap/largest-block recovery so BLE can return without sleep/restart.
2. Make release builds deterministic and verify identical-source firmware hashes.
3. EPUB fidelity follow-up: small caps, common chapter/hgroup styling, verse/letter typography, and lightweight table readability improvements where cheap.
4. Add targeted EPUB regression/torture fixtures, including the Standard Ebooks *Through the Brazilian Wilderness* reference book.
5. Continue evidence-driven parser/cache hardening only where real fixtures justify it.
6. Expand Bluetooth controller support and mapping UX after lifecycle work is stable.
7. Improve CBZ high-resolution performance/startup/prefetch without harming Reader memory.
8. Perform fuller physical X3 validation.
9. Investigate X4 Pro / X4 Classic later; not officially supported yet.
10. Trim/gate forensic diagnostics after 1.6.4 lifecycle work is proven.

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
