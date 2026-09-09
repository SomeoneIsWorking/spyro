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

## `0x80059F8C` decoded (moby shadows) — the only native producer this stage still needs

`0x80018728` and `0x80018880` emit no GPU primitives. They build guest state only: the first creates
the "RESCUED <name>" HUD text mobys through `func_80017FE4` and spins their Z rotation, the second
appends the HUD moby range onto `g_SonyImage.m_ShadedMobys`. Unported guest behaviour executing
through Lightrec is the product architecture, so they need no native owner — the drawing producers
that consume the lists they build are already owned. That leaves ONE missing producer.

`0x80059F8C` is handwritten GTE (`asm/renderers/r_shadows.s`, 325 lines, sibling of the already-owned
Spyro shadow `0x80059A48`). Addresses recovered from the instruction encodings:

| symbol | address | role |
|---|---|---|
| `D_8006FCF4 + 0x2800` | `0x800724F4` | shadow-list start (already the port's `kShadowListStart`) |
| `g_MobyShadows` | `0x80075EF8` | `+0` uv0/clut, `+4` uv1/tpage, `+8` list end cursor |
| `D_8006CBF8` | `0x8006CBF8` | sine table, 8-byte stride, cosine at `+0x80` |
| `D_800757B0` | `0x800757B0` | packet-pool cursor, rewritten on return |
| `g_WorldOT` | `0x80075820` | ordering table |

Each list entry is 8 bytes: `{Moby *, radius}`. Per entry:

1. Load the camera rotation into R11..R33 with TR = 0; camera position is `g_Camera+0x28/0x2C/0x30`
   each `>> 2`.
2. `shadowWord = moby[0x1C]`. Its low half is the shadow's Z; `shadowWord == 0` in the low half skips
   the entry. Project `(cameraY - moby[0x10]>>2, cameraZ - shadowZ, moby[0x0C]>>2 - cameraX)` — the
   same packing `world_projection_math::packProjectionInput` already uses. Reject when view Z >= 0x1000.
3. Two optional plane rotations, from `(shadowWord >> 16) & 0x3F` and `(shadowWord >> 22) & 0x3F`,
   each `<< 3` into the sine table, composed into the loaded matrix by two `MVMVA 1,0,0,3,0` pairs.
   **Reproduce `add $t3, $t3, $a0` at `0x8005A158` literally**: the encoding is `0x01645820`, rs/rd
   `$t3`, rt `$a0`, so R11 gains the second angle*8 rather than IR1<<16 as the other three lanes do.
   It reads like a register slip in the handwritten source; retail's behaviour is the spec.
4. Ring of four points at radius r in the shadow plane — `(0,0,r)`, `(r,0,0)`, `(0,0,-r)`, `(-r,0,0)` —
   the first three by `RTPT`, the fourth by a following `RTPS`.
5. Reject on `NCLIP` MAC0 < 0, on anchor screen Y <= 0 or >= 0xF00000, and on anchor screen X outside
   `(-8, 0x208)`.
6. When anchor view Z > 0x400, nudge each stored screen XY by +/-1 in Y according to whether that
   vertex's SZ is above or below the anchor's.
7. Emit four `POLY_FT3` fan triangles `(anchor, v[i], v[i+1])`, 7 words after a `0x07000000` tag,
   32-byte slots. Word 1 is `0x26808080`, or `0x26000000 | g|g<<8|g<<16` with
   `g = (0xC00 - viewZ + 0x400) >> 3` when `viewZ < 0xC00` — a distance fade. UVs are
   `g_MobyShadows[0]`, `g_MobyShadows[1]` and `g_MobyShadows[0] + 0x1F00`.
8. OT bin `((szA + szB) * 3/2 + anchorSz) >> 7`, minus the signed byte `moby[0x47]` decremented by
   one; negative bins are dropped. Note the shift is **7**, where the 16-point Spyro shadow uses 9 —
   `field_shadow_recipe::otBin` needs the shift as a parameter rather than being reused as is.

So the shape mirrors `field_shadow_recipe` closely enough to follow its structure, but it is textured
and distance-faded where Spyro's shadow is a flat `0x808060`, and it iterates a list of mobys rather
than one actor.

## Related

Other reachable gamestates with no native producer, same abort: 1 (GS_LevelTransition), 2/3 (pause
and inventory, already recorded), 9 (GS_EntranceAnimation), 10 (GS_ExitLevel), 11 (GS_Fairy),
12 (GS_Balloonist), 15 (GS_Credits).
