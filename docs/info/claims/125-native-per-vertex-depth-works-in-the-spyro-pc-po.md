---
id: C125
kind: claim
status: falsified
created: 2026-07-29
tags: gpu,depth,gte,milestone
falsified_on: 2026-07-29
---

## Claim

Native per-vertex depth works in the Spyro PC port: 210/210 primitives in a sampled level frame carry real view-space Z, with zero unresolved lookups.

## Evidence

PSXPORT_PRIMDUMP=41340 -> prims_f41340.csv, is3d=1 for all 210 prims. PSXPORT_DEBUG=ndepth at the same frames: records=1386 lookups hit=670 miss=0 (was records=1120 hit=0 miss=1655). 128 sampled frames show hit>0. Gate 14/14.

## What would falsify it

is3d dropping back toward 0 in a primdump, or ndepth showing miss>0 again — either means a tap or the cache lifetime regressed; also if a scene with a THIRD buffer in the pool cycle appears, two generations would no longer cover one flip

## FALSIFIED 2026-07-29

TRUE AS MEASURED BUT UNREPRESENTATIVE, which makes it dangerous — the frame I sampled is one of the few that reach 100%. Aggregating the ndepth summary over the WHOLE run instead of one frame: 1572 sampled frames, 826 with primitives, and of 921,709 primitives only 23,281 carry real depth — 2.5%. Per frame: 47 at 100%, 81 partial, 698 at ZERO. Frames 3960-4200, with ~1900 prims each, get none at all.

I picked the sampling frame BECAUSE the ndepth summary reported 3D prims there, which selected for success. The mechanism is real and the chain works end to end (C125's evidence stands as evidence), but 'native depth works' overstates it: what works is native depth for the renderer family the taps happen to cover, in the scenes where that renderer is active. 97.5% of primitives still fall to the OT band and are ordered by draw order.

Replaced by C128, which states the coverage.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
