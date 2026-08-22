---
id: C203
kind: claim
status: holds
created: 2026-08-19
tags: render,depth,ownership
depends: game/core/world_body.inc, game/core/native_world.cpp#world_native
---

## Claim

The world renderer 0x800258F0 (RenderWorldChunks) is OWNED by a native body that is byte-exact with the recompiled one in SOURCE and in EXECUTION. game/core/world_body.inc is emitted by tools/transcribe.py as a bijective rendering of generated/shard_3.c gen_func_800258F0 (5065 statements, 50 lui/addiu address pairs folded); the round-trip check re-derives the generated source from it exactly, and runs in the gate. Executed, PSXPORT_NATIVE_WORLD=1 PSXPORT_NDIFF=100000 over an 8000-presented-frame reference-leg run verified 1682 calls with ZERO divergences across RAM, scratchpad, all 31 GPRs and the COP2 register file — and BOTH entry shapes were exercised: 1555 flat-list (a0<0) and 127 occlusion-group (a0>=0), reported as separate ndiff sites so the count cannot pass as coverage it does not have.

## Evidence

scratch/logs/ndiff_world_shapes.log: 1555 'world-flat@0x800258F0 call #N matches the recompiled body exactly' + 127 'world-occ@0x800258F0 ...', 0 divergence lines, rc=0. Source exactness: 'transcribe.py check 0x800258F0 --body game/core/world_body.inc' -> 'matched all 5065 generated statement(s)'. The body is NOT installed on a normal run (PSXPORT_NATIVE_WORLD is off by default) — see the falsifier.

## What would falsify it

This covers only what the run EXECUTED: 1682 of the 3587 calls the census (C199) counts for the same recipe, because the differential's cost changes how far 8000 frames reach. Any world-renderer path not entered in those 1682 calls is unverified — notably packet-pool exhaustion and adaptive GT3/GT4 replacement (C215) if they did not fire. Falsified if a longer or differently-routed run reports any world-flat/world-occ divergence, or if generated/ is regenerated and the gate's transcribe check fails.
