---
id: C189
kind: claim
status: holds
created: 2026-08-14
tags: render,paired-actor,projection
depends: game/render/fx_paired_actor.cpp#project_rtps, game/render/fx_paired_actor.cpp#submit_native
---

## Claim

Spyro's joined 0x80023AC4 normal producer owns subpixel float screen endpoints derived from its production transform and projection state, without reconstructing them from guest integer SXY.

## Evidence

`scratch/logs/paired_float_xy_oracle.log`, exit 0 over 4100 presents: 384/384 float-only gates PASS; every invocation compared 238/238 vertices against the independently implemented framework `proj_native_xform`, with zero mismatches across IR1..3, SZ, SXY, float XY and PZ. Rounded float endpoints were also within one pixel of exact guest SXY (maximum error 1), and the integer-forced negative differed for 238/238 vertices. `PSXPORT_SELFTEST=pairedpose` passes 17 checks including an endpoint whose integer SXY is 256 while its production float X remains strictly between 256 and 257.

## What would falsify it

Any live invocation exceeds the one-pixel guest-SXY envelope, the integer-forced negative ceases to distinguish the retained endpoint, or `project_rtps`, `ProjectedVertex`, `submit_native`, or the framework float-XY RenderQueue contract changes without rerunning the 4100-present float oracle and hermetic selftest.
