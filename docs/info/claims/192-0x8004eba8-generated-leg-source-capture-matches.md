---
id: C192
kind: claim
status: holds
created: 2026-08-14
tags: render,terrain,oracle
depends: game/core/native_terrain.cpp#terrain_owned, game/recomp_seeds.json
---

## Claim

0x8004EBA8 generated-leg source capture matches the native body's F3/G3 packets, packet chain, and OT splice for 100 consecutive nonempty calls

## Evidence

PSXPORT_NATIVE_TERRAIN=1 PSXPORT_TERRAIN_ORACLE=1 PSXPORT_NDIFF=100 bounded run in scratch/logs/terrain_recipe_oracle.log: every call result=PASS; final call objects=17 candidates=984 clip_rejects=429 F3=56 G3=499 other=0 pool=0 emitted=555 bytes=15092 chain=555 splice=1 mismatched=0; PSXPORT_SELFTEST=terrainrecipe rejects corrupt XY and low24 link

## What would falsify it

Any change to native_terrain.cpp source decode/capture, generated checkpoint ownership/PCs, packet layouts, or OT linkage; or a live call with mismatch/other command/pool rejection
