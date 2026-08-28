---
id: C229
kind: claim
status: holds
created: 2026-08-28
tags: world,animation,render,ownership,re
depends: game/render/world_animation.cpp, game/render/world_scene_prepare.cpp, game/core/world_animation_oracle.cpp
---

## Claim

RenderWorldChunks 0x800258F0's phase-1 per-sector animation is OWNED natively and byte-exact with the retained body on the real refusing frame. The four channels (0 LQ vertices, 1 LQ colours, 2 HQ vertices, 3 HQ paired colours) each have a direct-copy and a GTE-interpolated form (INTPL for vertices, DPCS for colours), gated by the sector's four stamp bytes ORed with the emitted-quality mask; a channel runs when its byte is below 0x80 and is retired by stamping it back to 0xFF. Verified by running the retained gen_func_800258F0 TWICE from the same captured RAM — once as the guest (animate+render), once after the native animation retired the channels (render only) — and requiring byte-identical guest RAM. On scratch/raw/stage0_artisans_refusal.bin at selection 5 the two legs matched across the whole 2 MB after 2 channels / 28 writes.

## Evidence

PSXPORT_WORLD_ANIMATION_ORACLE_SNAPSHOT=scratch/raw/stage0_artisans_refusal.bin -> 'call 1 selection 5: IDENTICAL guest RAM after 2 channel(s) (2 direct, 0 blended, 28 write(s))'; negative control PSXPORT_WORLD_ANIMATION_ORACLE_MUTATE=1 flips one byte the animation wrote and the same comparison reports 80 differing bytes. tests/test_world_animation.cpp covers all four channels and both forms hermetically (46 checks).

## What would falsify it

The BLENDED form is NOT covered by the live corpus — 0 blended channels on the only real frame compared. If a frame with a nonzero keyframe blend factor ever diverges under the same oracle, the INTPL/DPCS transcription is wrong and this claim falls.
