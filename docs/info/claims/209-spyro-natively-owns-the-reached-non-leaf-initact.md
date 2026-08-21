---
id: C209
kind: claim
status: holds
created: 2026-08-21
tags: native,ownership,non-leaf
depends: game/core/native_actor_mesh_scratch.cpp#initActorMeshScratchNative, game/core/actor_mesh_scratch.h#actorMeshScratchLayout, game/core/game_hooks.cpp#spyro_registerOverrides, tests/test_actor_mesh_scratch.cpp#main
reconfirmed: 2026-08-21
verified_at: 2026-08-21 14:17:54
---

## Claim

Spyro natively owns the reached non-leaf InitActorMeshScratchRegions body at 0x8005B6F8 while retaining its generated oracle and already-owned FillWord child.

## Evidence

SCUS_942.28 disassembly and the byte-matching open-spyro symbol identify the 56-instruction body and its three static callers. A 3,000-field FNTRACE run reached it once at frame 437 from ra=0x8005B8C0 while seven other dependency-valid static candidates were never called. On the rebuilt Clang port, PSXPORT_NDIFF=1 reported actor-scratch@0x8005B6F8 call #1 matches the recompiled body exactly; the focused actor_mesh_scratch CTest covers both binary-derived 0x1C000 and 0x13000 layout branches.

## What would falsify it

The executable or generated body changes, the runtime override no longer fires on the reached boot call, either layout branch fails its focused test, or PSXPORT_NDIFF reports a divergence.

## Re-confirmed 2026-08-21 02:41:00

Final Clang evidence: scratch/logs/gate-boot-20260821-022906.log traces 0x8005B6F8 once at frame 437 from ra=0x8005B8C0 and names the other seven dependency-valid candidates NEVER CALLED; scratch/logs/gate-boot-20260821-023423.log line 71 reports actor-scratch call #1 matches the retained recompiled body exactly; CTest passed 9/9 including both stride modes and cpp-policy 37/37; ordinary shipping gate scratch/logs/gate-boot-20260821-023650.log passed 14/14 at 3,000 fields.

## Re-confirmed 2026-08-21 03:00:47

Final framework-2b5ef7b5 evidence: clean CMake --fresh configure selected Clang 22.1.8 and recorded psxport 2b5ef7b5522f3b879b69315acd11a037ca7a78bb; focused actor_mesh_scratch CTest passed 1/1 and full CTest passed 9/9 including cpp-policy 37/37; scratch/logs/gate-boot-20260821-025913.log line 71 reports actor-scratch@0x8005B6F8 call #1 matches the retained recompiled body exactly; ordinary shipping gate scratch/logs/gate-boot-20260821-025949.log passed 14/14 at 3,000 fields, 13 scenes, and 707690 native-producer primitives.

## Re-confirmed 2026-08-21 03:03:57

Final exact-tree evidence against psxport 2b5ef7b5522f3b879b69315acd11a037ca7a78bb: clean Clang 22.1.8 build; focused actor_mesh_scratch CTest 1/1 and full CTest 9/9 with cpp-policy 37/37; scratch/logs/gate-boot-20260821-030237.log line 71 reports actor-scratch@0x8005B6F8 call #1 matches the retained recompiled body exactly; ordinary shipping gate scratch/logs/gate-boot-20260821-030254.log passed 14/14 at 3,000 fields, 13 scenes, and 709529 native-producer primitives.

## Re-confirmed 2026-08-21 03:12:16

2026-08-21 landed-tree reconfirmation: exact clean psxport 2b5ef7b5 build passed focused actor_mesh_scratch 1/1 and full CTest 9/9; NDIFF log gate-boot-20260821-030237 line 71 matched call #1 exactly; ordinary gate-boot-20260821-030254 passed 14/14 at 3000 fields.

## Re-confirmed 2026-08-21

Post-landing current-tree NDIFF log gate-boot-20260821-032916.log reports actor-scratch@0x8005B6F8 call #1 exact; focused actor_mesh_scratch CTest passed.

## Re-confirmed 2026-08-21

Post-landing full CTest 13/13 and long native differential retained the actor mesh scratch owner while composing the new text owner.
