---
id: C087
kind: claim
status: holds
created: 2026-07-29
tags: perf
---

## Claim

The two diagnostics fixes bought +12.6% throughput AND more game coverage per run — the port now reaches a seventh overlay within the gate's 40 seconds.

## Evidence

Same 40s gate, same machine: frames 16508 -> 17178 -> 18586 across the lucent channel_enabled fix (6.06% -> 0.33% of CPU) and the inlined per-store watch hooks (cw_check 3.14% + wwatch_check 1.79% removed from the profile). Bytes from disc 11147264 -> 13178880, CD completions 56 -> 63, distinct overlays identified 6 -> 7, zero recomp misses and zero native/substrate divergences throughout. Frame counts vary run to run so a single pair would be weak, but the overlay and byte counts are step changes in what the run actually exercises, not noise.

## What would falsify it

if a later run at the same build shows 16-17k frames, the gain is within run-to-run variance and this overstates it
