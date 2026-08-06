---
id: C128
kind: claim
status: holds
created: 2026-07-29
tags: gpu,depth,coverage
---

## Claim

Native per-vertex depth is MECHANICALLY WORKING but covers only 2.5% of primitives: 23281 of 921709 across a full run, with 698 of 826 prim-bearing frames getting none at all.

## Evidence

Aggregated the per-frame ndepth summary over a whole 130s run (scratch/logs/depth9.log): 1572 sampled frames, 826 with prims; total real-depth(3D)=23281 vs OT-band(2D)=898428. Per-frame: 47 frames at 100%, 81 partial, 698 at 0%. The 100% frames (e.g. 41340, 210/210 prims) are real — the chain records, propagates and resolves with miss=0 — so this is a COVERAGE limit, not a correctness one.

## What would falsify it

the ratio moving materially in either direction after more renderers are tapped or owned; also if a scene is found where prims resolve depth but the resulting occlusion is visibly wrong, which would make this a correctness problem rather than coverage

## INSTRUMENT DEPENDENCY NOTED 2026-08-06 — `PSXPORT_DEBUG=ndepth` is now I041, DISTRUSTED

Status left `holds`; nothing measured contradicts this claim, and its "1572 sampled frames" phrasing
is already the honest form. The dependency is recorded so the link is not lost: each ndepth line is a
ONE-FRAME snapshot (counters reset every present at `gpu_native.cpp:1466` while the report is gated
`s_frame % 60 == 0` at `:1439`) and the line never prints that window. So `2.5%` is 2.5% ACROSS 1572
SAMPLED FRAMES, not across the run's ~N frames, and the two denominators are not the same number.

The spider1 failure that got I041 registered — every sample phase-locked onto a non-drawing field —
is NOT known to happen here, and C145's observed alternation across consecutive samples is evidence
that it does not. See I041. Unreconciled and worth settling before re-deriving these figures: 1572
samples does not divide cleanly out of a 130 s run at a 60-frame period.
