---
id: C202
kind: claim
status: holds
created: 2026-08-17
tags: render,ownership,ndiff,re
---

## Claim

RenderWorldChunks 0x800258F0 is reproducible under the per-call differential: PSXPORT_NDIFF_IDENTITY=800258F0 with PSXPORT_NDIFF=20 (the generated body as BOTH the native and the substrate side, RAM+scratchpad+GPRs+COP2 rewound between them) reports all 20 sampled calls 'matches the recompiled body exactly' with 0 divergences. This answers the C198-era question (native_render.cpp:1 'can the differential validate a geometry renderer at all'): YES — a native reimplementation of the world renderer CAN be certified by ndiff, the standard acceptance path, because the body reads/writes only guest RAM + the GTE scratchpad + the COP2 register file (all rewound), never host GPU state. Ownership for DEPTH is therefore reachable through the normal gate, not a bespoke test.

## Evidence

scratch/logs/ndiff_world2.log: 'IDENTITY@0x800258F0 call #1..#20 matches the recompiled body exactly', 0 DIVERGES, rc=0. The other ndiff lines (spin60/fill/setg3c30/copyw) are the framework's built-in sites enabled by PSXPORT_NDIFF=20 and are unrelated.

## What would falsify it

a 0x800258F0 identity call reporting DIVERGES (RAM/scratchpad/GPR/COP2/FLAGS/hi-lo) under the rewind, or a divergence once the world renderer reaches a variant path (for example adaptive GT3/GT4 replacement at 0x8002A0A0..0x8002A6B0, corrected by C215) not exercised by these 20 calls
