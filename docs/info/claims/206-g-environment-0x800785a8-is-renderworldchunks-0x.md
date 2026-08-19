---
id: C206
kind: claim
status: holds
created: 2026-08-19
tags: render,world,re,widescreen,fps60
depends: game/core/native_world.cpp#kEnvironment
---

## Claim

g_Environment (0x800785A8) is RenderWorldChunks 0x800258F0's INPUT DATA MODEL, and six of its offsets are verified against this port's byte-exact body: +0x00 m_SectorPointer (both entry arms), +0x04 m_SectorCount (the a0<0 arm's length-counted end pointer), +0x08 m_OcclusionGroups (the a0>=0 arm's per-group 0xFF-terminated index list), +0x18 m_LQTexturePointer (Phase 2), +0x1C m_HQTexturePointer (Phase 3), +0x24 m_LodDistance, +0x28 m_CullingDistance (Phase 1's cull radius, written by the CALLER 0x8002B9CC as 0x28000/0x1C000/0x14000). Two further globals this renderer touches are now named: 0x8007591C g_SkipLowPolyWorld and 0x80078560 g_EnvironmentAnimations.

## Evidence

The names come from the vendored decomp's Environment struct (external/spyro-1/include/environment.h) and its annotated hand-written assembly for this very function (external/spyro-1/asm/renderers/r_environment.s, 5271 lines under glabel func_800258F0) — a REFERENCE, so every offset was checked against what game/core/world_body.inc actually does with it rather than accepted as a label. The addresses are decoded from that asm's own lui/addiu word pairs, not from a linker script, and all 24 symbols it references resolve; four of them (0x80077DD8, 0x80076DD0, 0x800785A8, 0x800757B0) land exactly on names this port had already derived independently, and 0x80075820 resolves to g_WorldOT, which the decomp's GamestateDraw assigns from db->m_WorldOT. THE DECISIVE CHECK is that the two entry arms fall out of the offsets without being looked for: world_body.inc:59 loads +0x00 in both arms, then the a0<0 arm computes sectors + count*4 from +0x04 while the a0>=0 arm indexes +0x08 by a0*4 and walks BYTES until 0xFF. That is exactly the two structurally different entry shapes C199 measured at runtime, arrived at from a completely different direction. Execution unaffected: after adding the two names and re-emitting, transcribe.py check re-derives all 5065 generated statements exactly, and a 4000-frame reference-leg run with PSXPORT_NATIVE_WORLD=1 PSXPORT_NDIFF=100000 verified 1555 native world calls with zero divergences (scratch/logs/world_ndiff_names.log).

## What would falsify it

a run in which the a0>=0 occlusion-group arm reads an offset this map does not name, or a level whose m_CullingDistance is not one of the three values 0x8002B9CC writes — the 4000-frame run above fired the FLAT arm 1555 times and the occlusion arm ZERO times, so the +0x08 reading is static evidence from the body, not a live observation
