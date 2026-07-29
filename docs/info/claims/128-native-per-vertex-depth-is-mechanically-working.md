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
