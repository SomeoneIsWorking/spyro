---
id: 114
title: No reproducible route out of Artisans, so level entry is still uncompared
status: open
symptom: the oracle matches the console byte for byte at every frame inside Artisans, but any route that walks out of it diverges, so the one boundary that discards and reloads guest code at a reused address has never been compared
state_items: S011
tags: oracle,camera,gameplay,timing,invalidation
created: 2026-09-19
updated: 2026-09-19
---

## Why a level entry is the checkpoint worth having

Everything the oracle compares today happens inside one resident WAD image. A portal entry is the
only place the product must discard and reload guest code at a reused load address and invalidate
every translation that came from it. That is the part of the runtime a matched walk around Artisans
cannot exercise at all, and it is why `tools/oracle_spyro1.py` carried a `level` checkpoint at all.

## What is measured, and it is not the product

Inside Artisans the product is exact. With `tools/oracle_compare.py --frame-step 1`, the twelve
representative gameplay segments are compared after **every one** of their 477 game frames rather
than once per segment: 485 comparisons including `save_picker` and `playing`, **zero divergences**,
exit 0, 153 s. Every decisive range agrees byte for byte at every frame, including
`g_Spyro.m_Position`, `g_Spyro.m_State`, `g_GameTick`, `g_StateSwitch`, the three pad words and the
occlusion result.

Outside it, nothing is reproducible. Two attempts:

| instrument | what happened |
|---|---|
| one `spyro1_steering.Walk` per core, each steering from its own camera | agreed for two decisions, drifted from the third (`bearing +42` against `+38`), pressed different buttons from the tenth (`up+left` against `up`), took 16 decisions against 15, and entered level 11 from two different places. The decisive `player.position` DIVERGE it reported was those two places, not a product defect |
| record the reference's 473 steered frames and replay exactly those on the product | the product did not reach any portal within the 6000-frame budget: `game_tick=6002, level=10` |

Replay excludes the input as the cause. Under the console's own pad, frame for frame, the product
goes somewhere else.

## Why, and it is one counter

Spyro's d-pad is camera-relative, so an identical pad with a different camera is a different
direction. The per-frame Artisans report says exactly where the camera parts company. Taking the
first comparison at which each declared range stops being equal:

| range | first unequal at | bytes |
|---|---|---|
| `player` +0x2a0 | `gameplay[1]` frame 43 | `m_damageSoundChannel`: native 3, console 2 |
| `dragon_cutscene` +0x8 | `gameplay[2]` frame 9 | native 1, console 0 |
| `delta_time` | `gameplay[3]` frame 26 | native 2, console 4 |
| `camera` +0x00 | `gameplay[3]` frame 39 | the projection and view matrices |

The sound-channel index is an SPU voice allocation and means nothing here. The camera one does. At
that frame the camera's `m_Position`, `m_DestinationPosition`, `m_State`, `m_OcclusionGroup` and all
four `SphericalCoordsOffset` blocks are byte-identical; what differs is only the two rotation
matrices and the Euler `m_Rotation` they come from:

```
m_Rotation   native ff0f e80f 3103   console ff0f f70f 2c03
             x 0x0fff / 0x0fff   y 4072 / 4087   z 817 / 812
```

15 units of 4096 is about 1.3 degrees of yaw. The camera is in the same place, pointing slightly
differently, one frame after `g_DeltaTime` read 2 against the console's 4. That counter is the lag
the last update was told about, derived from `g_LevelTicks`, whose constant offset from each load is
what [issue 0110](0110-artisans-camera-checkpoints-are-not-yet-phase-aligned.md) parked as a
pacing-model convention. Camera smoothing consumes it; heading consumes the camera; a 473-frame
walk integrates the difference.

So 0110's residual is not confined to counters after all. It does not move the player under held
input over 477 frames, which is why every decisive range still matches, but it does steer.

## What would close this

Either of these, and the first is the real fix:

1. Make `g_DeltaTime` reproduce retail's value per update, which makes the camera's rotation match
   and the recorded route replay. This is 0110 reopened at its cause rather than at its symptom.
2. Reach a level without a camera-relative route at all, if the title offers one that does not write
   guest state. A cheat or a forced teleport would be writing the answer, not measuring it, and does
   not count.

Until then `tools/oracle_spyro1.py` registers only the checkpoints it can honestly compare, and
S011 stays `missing` with WAD invalidation uncovered by the oracle.

## Not the same as issue 0113

0113 was a hard stop on the same route: the per-face colour program a secondary actor's triangle
selects was unimplemented, so the product aborted shortly after entering Stone Hill. That is fixed
and is not why the route is unreproducible; the walk diverges long before it, inside Artisans.
