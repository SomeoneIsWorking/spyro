---
id: C125
kind: claim
status: holds
created: 2026-07-29
tags: gpu,depth,gte,milestone
---

## Claim

Native per-vertex depth works in the Spyro PC port: 210/210 primitives in a sampled level frame carry real view-space Z, with zero unresolved lookups.

## Evidence

PSXPORT_PRIMDUMP=41340 -> prims_f41340.csv, is3d=1 for all 210 prims. PSXPORT_DEBUG=ndepth at the same frames: records=1386 lookups hit=670 miss=0 (was records=1120 hit=0 miss=1655). 128 sampled frames show hit>0. Gate 14/14.

## What would falsify it

is3d dropping back toward 0 in a primdump, or ndepth showing miss>0 again — either means a tap or the cache lifetime regressed; also if a scene with a THIRD buffer in the pool cycle appears, two generations would no longer cover one flip
