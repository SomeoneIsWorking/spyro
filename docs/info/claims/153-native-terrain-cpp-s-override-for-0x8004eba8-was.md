---
id: C153
kind: claim
status: holds
created: 2026-08-05
tags: render,widescreen,hazard
depends: game/core/native_terrain.cpp#spyro_register_native_terrain, game/core/wide_clip.cpp#spyro_register_wide_clip
---

## Claim

native_terrain.cpp's override for 0x8004EBA8 was registered UNCONDITIONALLY while three separate comments documented it as off unless PSXPORT_NATIVE_TERRAIN=1. The behaviour was correct only by registration ORDER: wide_clip.cpp registers later and claims the same single override slot, silently displacing it. Net effect right, mechanism a lie, and nothing would have caught a reordering.

## Evidence

Read: game_hooks.cpp:76 comment 'off unless PSXPORT_NATIVE_TERRAIN=1', wide_clip.cpp:62-66 same, wide_clip.cpp:264 'if (!cfg_on(PSXPORT_NATIVE_TERRAIN)) shard_set_override(kRenderers[0].addr, hook<0>)' — while native_terrain.cpp:370 called shard_set_override(0x8004EBA8, terrain_owned) with no gate. MEASURED both directions after gating the registration on the same flag: flag OFF, tools/gate.sh 90 = 17/17 PASS, 56602 frames, 'native bodies verified' 160 — IDENTICAL to the 160 of the pre-change baseline, which proves terrain_owned was never running on a normal run (had it been, gating it off would have reduced the count). Flag ON, 60s run: the new install log fires and ndiff reports 8 'terrain@0x8004EBA8 ... matches the recompiled body exactly', wide_clip still arms 5 renderers / 11 sites. Baseline before change: 17/17 PASS, 47592 frames, 160 verified.

## What would falsify it

if a run with PSXPORT_NATIVE_TERRAIN unset ever reports a terrain@0x8004EBA8 ndiff line, the registration is live again and the flag is not the only gate
