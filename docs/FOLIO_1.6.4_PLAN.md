# Folio Nooir 1.6.4 Plan

Status: post-1.6.3 planning baseline.

## Release state

Folio Nooir 1.6.3 is released and closed. Do not reopen 1.6.3 for normal feature work. All remaining work below belongs to 1.6.4 or later unless a critical regression requires a separate hotfix.

Released 1.6.3 artifact:
- firmware.bin size: 5,788,880 bytes
- SHA-256: A843B52D976B547FA8E60ECC5DC544F826F7A7725AFEDE3FEE6245ACE06E3FA8

## 1.6.4 priorities

### 1. Wi-Fi stability and teardown

Highest-priority lifecycle/regression area from 1.6.3.

#### A. Community-reported X4 Wi-Fi reboot regression

A user testing the released 1.6.3 firmware on an X4 reported a repeatable setup failure: after connecting the device to a Wi-Fi network, the device reboots after a few seconds. The reboot reportedly happens every time they connect, preventing them from keeping the device online long enough to finish setup through Nooir's web interface.

Treat this as a real community report but not yet a root-cause diagnosis. Do not assume it is the same bug as the post-Wi-Fi BLE heap-recovery issue until logs or reproduction prove that.

Reproduction to investigate:
1. flash/load released 1.6.3 on X4;
2. connect to a Wi-Fi network;
3. remain connected for several seconds / attempt to use the web interface;
4. observe whether the device reboots;
5. capture serial/reset reason, heap/largest-block history, Wi-Fi/web-server lifecycle, and whether BLE is enabled or connected.

1.6.4 goal:
- reproduce on physical X4 if possible;
- obtain serial logs/reset reason from an affected setup;
- determine whether this is OOM, watchdog, assert/panic, Wi-Fi/web-server lifecycle, BLE/Wi-Fi interaction, or another path;
- make normal connect-and-use-web-UI setup stable;
- preserve the Reader-first memory policy rather than masking the fault with retries or lower safety margins.

#### B. Wi-Fi -> BLE heap recovery

Observed on physical X4: after some Wi-Fi selection/connect/cancel/deinit paths, total free heap and largest allocatable block remain below the healthy pre-Wi-Fi state. BLE then correctly stays off because the 1.6.3 admission floors are not met. Sleep/wake restores the healthy baseline.

Goal for 1.6.4:
- identify retained Wi-Fi/network allocations or fragmentation after teardown;
- restore a healthy post-Wi-Fi heap without requiring sleep/restart;
- keep Reader priority and existing BLE safety floors;
- no retry storms, floor bypasses, global purges, or background churn.

### 2. EPUB fidelity: surgical improvements only

Keep Nooir a fast book renderer, not a browser engine. Every addition must preserve X3/X4 responsiveness, bounded memory use, cache correctness, and readable fallback.

Use the Standard Ebooks edition of Theodore Roosevelt's *Through the Brazilian Wilderness* as one real-world regression/reference EPUB. It exercises small caps, structured chapter headings, verse/letter styling, many images, and complex appendix tables.

Candidate work, in priority order:

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

### 3. Deterministic release builds

Make generated HTML/gzip assets reproducible by using deterministic gzip timestamps (for example `gzip.compress(..., mtime=0)` where appropriate), then verify two clean `gh_release` builds from identical sources produce identical firmware hashes.

### 4. Bluetooth Page Turner follow-up

Bluetooth remains Beta.

Potential 1.6.4 work after Wi-Fi recovery:
- broader controller compatibility;
- easier mapping/setup;
- consider a bounded raw-HID learner using fixed storage only;
- improve BLE/Wi-Fi lifecycle polish and user feedback.

Do not weaken the 1.6.3 Reader-first memory policy.

### 5. EPUB/CSS hardening backlog

Only evidence-driven fixes:
- malformed XHTML/entity edge cases;
- empty inline/span spacing regressions;
- KOReader oversized-response handling where relevant;
- targeted parser/cache fixes that apply cleanly;
- torture EPUB fixtures for small caps, headings, verse, tables, malformed markup, huge images, RTL/LTR mixtures, and cache interruption.

Avoid broad renderer rewrites.

### 6. CBZ / manga

Still experimental. Candidate work:
- high-resolution page performance;
- first-page/startup latency;
- safer/smarter prefetch;
- optional web-assisted preprocessing;
- quality improvements that do not increase Reader memory pressure.

### 7. Hardware validation

- X4 old model remains primary physical target.
- X3 remains compatibility floor; formal physical validation is still needed.
- X4 Pro / X4 Classic remain investigation items, not officially supported targets yet.

### 8. Diagnostics cleanup

Retain the useful allocator/BLE diagnostics while 1.6.4 lifecycle work is active. Trim or gate forensic logging later only after the replacement behavior is proven on hardware.

## Deferred / not implemented

- PDF reading
- FB2 reading
- full system-wide dark UI
- full browser-grade EPUB CSS
- official X4 Pro / X4 Classic support

## Acceptance rule

A 1.6.4 change is worth keeping only when it improves a real book or lifecycle failure without making ordinary reading slower, reducing safe heap margin materially, increasing fragmentation/churn, or making cache/recovery behavior less predictable.

Reader/page rendering remains the highest priority. Optional services must yield under pressure.
