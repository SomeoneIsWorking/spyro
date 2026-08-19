---
id: C204
kind: claim
status: holds
created: 2026-08-19
tags: depth,render,widescreen
depends: external/psxport/runtime/recomp/proj_prim.cpp, external/psxport/runtime/recomp/proj_prim.h
---

## Claim

The port's per-primitive depth coverage is 63.60% of 3,483,268 prims (85.30% of vertex-depth lookups resolved), up from 2.10%/6.41%, and the picture is unchanged. The blocker was NEVER the world renderer's ownership: it was ProjPrim keying entries by guest address alone, which forced entry lifetime down to one buffer flip because a recycled packet-pool slot would otherwise be served the depth of the vertex that used to occupy it. Guarding each entry by the WORD it was recorded against makes a reused address unable to alias, so retention could go to 8 generations (framework 2de90164, kGens). Evidence that the guard is load-bearing rather than decorative: compiling it out reads 70.53% on the same run, and that extra ~7 points is exactly the prims whose recorded word no longer matches memory.

## Evidence

One binary, one recipe (PSXPORT_NATIVE_FRAMES=6100 PSXPORT_RENDER_PSX=1 PSXPORT_NATIVE_WORLD=1): before '2215285 of 3483268 ... = 2.10% 3D ... 6.41% of lookups hit'; after '63.60% 3D ... 85.30% of lookups hit ... 2201 were STALE and 1265782 were ABSENT'. Picture: scratch/screenshots/f6001.png md5 b6223ab7b68726613ed04f07c449ce9e, byte-identical to the pre-change capture, with the run log showing the native world body RAN 2588 times. Native leg: gate 13 PASS / 0 FAIL, 709529 native producer prims unchanged, title screen sorts logo+HUD above the world. Framework suite 60/60; tests/test_proj_prim_stale.cpp shown RED without the guard (4 of 5 cases fail).

## What would falsify it

The 63.60% is measured on the REFERENCE leg (PSXPORT_RENDER_PSX=1), which computes depth but does not use it for ordering — so an unchanged picture there is NOT evidence the depths are correct, only that they are not consumed. Falsified if the native leg, once it reaches the field (issue 0065), mis-sorts geometry, or if a run shows stale refusals climbing into the same order as hits (entry lifetime outrunning the pool).
