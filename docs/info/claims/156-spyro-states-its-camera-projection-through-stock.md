---
id: C156
kind: claim
status: holds
created: 2026-08-06
tags: gte,projection,renderer,re
depends: game/core/game_config.cpp
---

## Claim

Spyro STATES its camera projection through stock libgte SetGeomOffset (0x80062618) and SetGeomScreen (0x80062638), and the constants it passes are OFX=256, OFY=120, H=341 — half of its own 512x240 display, NOT libgte's 160/120 and NOT H=350. Both leaves are now wired into GameConfig::hle so ProjParams records the projection where the game states it; geomValid() is true from boot init onward, so requireGeom() will not abort a native producer.

## Evidence

RE: Ghidra headless on the resident snapshot (project spyro470 over scratch/raw/snap_470.bin) — 'external/psxport/tools/decomp.sh decomp spyro470 scratch/decomp/g7_geom.c list 0x80062618 0x80062638 0x800127C0'. FUN_80062618(p1,p2){setCopControlWord(2,0xc000,p1<<0x10); setCopControlWord(2,0xc800,p2<<0x10);} = CR24/CR25 = SetGeomOffset. FUN_80062638(p1){setCopControlWord(2,0xd000,p1);} = CR26 = SetGeomScreen. Call site FUN_800127c0 (boot init, main's 2nd call, jal at 0x80012818/0x80012824): FUN_80062618(0x100,0x78); FUN_80062638(0x155). Independently corroborated by the recompiled substrate (different tool, same executable): generated/shard_6.c gen_func_800127C0 sets r[4]=256,r[5]=120 then calls func_80062618, then r[4]=341 then func_80062638. DENOMINATOR: the whole substrate (main + 7 overlays) contains 6 CR24 writes, 6 CR25 writes, 2 CR26 writes; enumerated all of them. RUNTIME (gdb on ./scratch/bin/spyro_port, headless, 2026-08-06): libgte_set_geom_offset fires with ofx=256 ofy=120 while ProjParams offsetSet=0 screenSet=0; libgte_set_geom_screen with h=341; afterwards offsetSet=1 screenSet=1 OFX=256 OFY=120 H=341, geomValid()=1. BYTE-EXACTNESS of the native replacement, measured on the same run: CR24 0x00000000->0x01000000, CR25 0x00000000->0x00780000, r[4]/r[5] left-shifted in place, CR26 0x000003E8(InitGeom's 1000)->0x00000155 — the complete effect of the two recompiled bodies. NEGATIVE CONTROL: with both GameConfig fields set back to 0 and rebuilt, neither breakpoint fires at all and the port logs '[plat-hle] 4 primitive(s)' instead of 6; gate.sh 60 is numerically identical in both legs (15 PASS, 2 pre-existing FAIL from issue 0046, 3544 frames both).

THE GATE ITSELF, EXECUTED BOTH WAYS. There is no native producer in this port yet, so the gate function was called directly in the running port under gdb (scratch/g7/require.gdb, scratch/g7/require_neg.gdb) rather than waited for:

  * WIRED:  `call c->rsub.projParams.requireGeom("g7-gate", $p[0], $p[1], $p[2])` after boot init ->
    returns normally, ofx=256.000000 ofy=120.000000 H=341.000000.
  * ZEROED (both GameConfig fields 0, rebuilt), same call at the same guest function ->
    "[proj:error] FATAL: g7-gate-negative asked for the camera projection before the game set one.
     SetGeomOffset (OFX/OFY) NEVER RAN — SetGeomScreen (H) NEVER RAN." then SIGABRT.

So the instrument demonstrably produces BOTH answers; the passing result is not a check that could only pass.

NOT VERIFIED — the honest limits. (a) No native producer exists yet, so requireGeom has never been reached from real render code, only called directly. (b) These two natives are NOT ndiff sites (PlatformHle entries do not go through ndiff_run), so gate.sh's "native/substrate divergences 0" does not cover them; the byte-exactness evidence is the GTE control-register comparison above plus the fact that the recompiled bodies are 4 and 1 instructions with no other effect. (c) Everything above was measured HEADLESS; the geom setters are on the guest boot path and cannot branch on the sink, but a windowed run was not taken.

## What would falsify it

if a game function is found that calls 0x80062618/0x80062638 with different constants at runtime, or if the two inline CR24/CR25 writers (FUN_80022a2c, FUN_80023384) are shown to use a per-view OFX/OFY that differs from 256/120 on a real frame — ProjParams cannot see those, it only records what passes through the leaf
