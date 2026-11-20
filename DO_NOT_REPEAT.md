# Do Not Repeat — Folio Nooir 1.6.7

Updated 2026-11-20. Read the current `HANDOFF.md` and the cumulative audit before starting work.

- Preserve `_04` BIN/ELF; never rebuild over them. Hashes and provenance are in `docs/milestones/FOLIO_NOOIR_1.6.7_X4_BLE_MILESTONE_2026-11-20.md`.
- The owner reports physical repeated EPUB BLE recovery on the original X4. Do not repeat the `_03` pre-fix test or describe it as `_04`. The saved raw COM3 file with three runtime floor faults is the pre-fix evidence; the raw post-fix _04 capture is not present in the workspace.
- Do not use `b846e155` as source-commit proof. The `_04` build uses content manifest provenance, not Git commit provenance.
- `_02` automatic idle admission, `_03` phase-aware budgets, and `_04` proactive suspension plus post-render cleanup rearming are cumulative; do not restart manual-arm or 12-byte threshold investigations.
- Preserve idle EPUB thresholds (104,448 B free / 36,852 B largest), post-start floors (32 KiB / 16 KiB), strict simultaneous-render threshold (108,544 B / 59,392 B), existing NimBLE pools, worker ownership and confirmed teardown. No speculative threshold or pool changes.
- Continuous EPUB BLE remains unproven as an always-available mode; the demonstrated approach is adaptive disconnect/render/reconnect. Do not claim 20 consecutive turns in one session until measured on the final image.
- Keep no-device Interrupt-WDT and partial NimBLE-init rollback open; do not claim the lifecycle is release-safe from source tests alone.
- Public 1.6.7 refinement branch changes after official 1.6.6 are documentation-only. The isolated _04 cumulative firmware source is the integration input; do not blindly merge old experiments.
- FreeInk `958720659ea289ae325e83db20049d0ea844800d` is the reproducible base; the four-file Nooir patch is maintained in this repository and applied by the pre-build hook. Do not switch to a different SDK branch or silently discard the patch.
- Do not integrate the rejected EPUB cache probe, status bars, Classic/Pro, S3/Web Emulator, or MihonNooir into the original-X4 branch.
- The `NooirSdSpiDiagnostics.h` memory-policy test import failure is a known missing-header coverage issue in both baseline and candidate. Do not report the entire suite as green. CMake/native tests were not run in the last isolated review because CMake was unavailable.
- Keep EB4D, `_02`, `_03`, `_04`, SD/profile data and stable 1.6.6 artifacts untouched. No flash, tag, merge to main or public release without separate approval.
