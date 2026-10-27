# BLE teardown heap diagnostic

`ble-host-teardown-heap.patch` is a diagnostic-only change against the
`freeink-sdk` gitlink in this checkout. It adds `BLEMEM` snapshots around scan
stop, link close, client deletion, connection-worker deletion, and NimBLE
deinitialization/retry. Each line includes whether NimBLE still reports itself
initialized and the deinit return value where applicable.

The patch does not change BLE admission floors, retry policy, memory ownership,
or teardown order. Apply it at the repository root before building the
`gh_release` environment:

```powershell
git apply diagnostics/ble/ble-host-teardown-heap.patch
pio run -e gh_release
```

The `BLEMEM stage=host_end_*` lines are emitted because
`gh_release` already enables `FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS`. Preserve
the patch and candidate together; the app descriptor's short source commit
does not by itself identify an un-applied SDK patch.

On X4, capture a baseline through this sequence without changing the saved
Bluetooth preference:

1. Cold boot with Bluetooth enabled; record startup/Home and connect stages.
2. Stop BLE through a reader transition, then capture every `host_end_*`
   stage and the `HEAPSHAPE` immediately after stop.
3. Repeat one BLE Off/On cycle from Settings to compare teardown and re-init.
4. Open XTC/XTCH and the affected EPUB with BLE off; record the existing
   streaming and PNG allocation diagnostics.
5. Repeat after BLE On/Off, then exit the reader and record total/largest heap.

Look for the first teardown stage where total free bytes or largest block
changes. If `deinit_post` still reports `initialized=1` or `result=0`, retain
the preceding lines; that is a failed stack teardown, not merely fragmentation.
If deinit succeeds but the largest block stays low, use the paired before/after
allocations and task state to isolate still-live owners before changing code.

This is not proof of X4/X3 safety and is not a release or OTA candidate.
