---
id: C174
kind: claim
status: holds
created: 2026-08-13
tags: camera,fps60,render
depends: game/core/game_hooks.cpp#spyro_fps60ReadSceneCam
---

## Claim

Spyro's native scene-camera hook is the affine form of renderer 0x80022A2C's own camera transform: packed rotation at 0x80076DD0 and camera position at 0x80076DF8, with T=-(R*C)/4096.

## Evidence

generated/shard_7.c gen_func_80022A2C loads 0x80076DD0..E0 into GTE rotation CR0..4, loads 0x80076DF8..E00 as camera position, subtracts it from object world positions, then runs MVMVA. game/core/game_hooks.cpp implements the same affine transform. PSXPORT_SELFTEST=scenecam passed 5/5 identity, signed packing, R22 and rotated-translation checks on the shipping binary.

## What would falsify it

if renderer 0x80022A2C is shown to source a different scene matrix/position on a drawn frame, or a native projection using this hook disagrees with the guest renderer for the same world points
