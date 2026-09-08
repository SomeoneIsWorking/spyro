---
id: 103
title: Freeing a dragon aborts: GS_Dragon (stage 8) has no native producer
status: open
symptom: the port aborts during Artisans gameplay with 'NATIVE RENDER NOT IMPLEMENTED — stage selector = 8 (no producer is registered for this stage)'; reported by the operator as a crash when Spyro breathes fire
tags: render,field,cutscene,dragon,producer,crash
created: 2026-09-08
updated: 2026-09-08
---

## Symptom

A normal Artisans session aborts the moment the dragon-rescue cutscene starts:

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 8 (no producer is registered for this stage)
[render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8 stage=8/3/2 load_stage=4294967295 state_switch=0
```

Reproduced headlessly with the new state-driven driver:

```
python3 tools/drive.py gameplay --hold up --hold-frames 90 --tap circle --after 40 --repeat 15 \
  --log scratch/logs/flame-session.log
```

The operator reported it as "crashes when you breathe fire"; flaming and walking both end at the
same guest state, so the reported input is not the discriminator — entering GS_Dragon is.

## Root cause

`SpyroRenderer::renderScene` has branches for the FIELD stages, stage 13 (front end) and stage 14
(cutscene) only. Gamestate 8 (`GS_Dragon`, the dragon-rescue cutscene) reaches
`abortUnimplemented(sc, "no producer is registered for this stage")`. That abort is the designed
deliverable for an unported stage, not a defect in itself; the gap is the missing producer.

## What the producer has to compose

Retail draws this state through `0x8001CFDC` (external/spyro-1 `src/gamestates/draw.c`,
`func_8001CFDC`). It is a per-`g_DragonCutscene.m_State` composition over eight states. Owners it
needs that this port ALREADY has: the moby queue `0x800521C0`, actor `0x80019698`/`0x8001F798`,
moby prepare `0x8001F158`, shaded mobys `0x80022A2C`, Spyro `0x80023AC4`, Spyro shadow
`0x80059A48`, environment `0x8002B9CC`, cyclorama `0x80050BD0`, particles `0x800573C8`, screen fade
`0x800190D4`, screen border `0x80018F30`.

Owners it needs that are still UNOWNED:

- `0x80059F8C` — moby shadows. Already recorded as unowned in `docs/project-state.md`.
- `0x80018728` — builds the "Rescued <name>" text mobys.
- `0x80018880` — copies the HUD mobys into the shaded-moby list.

State 0 alone composes only owned producers; states 1-7 all need at least one of the three above, so
a state-0-only branch would still abort a second later. The complete producer is the unit of work.

## Related

Other reachable gamestates with no native producer, same abort: 1 (GS_LevelTransition), 2/3 (pause
and inventory, already recorded), 9 (GS_EntranceAnimation), 10 (GS_ExitLevel), 11 (GS_Fairy),
12 (GS_Balloonist), 15 (GS_Credits).
