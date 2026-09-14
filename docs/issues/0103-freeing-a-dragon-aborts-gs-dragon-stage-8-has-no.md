---
id: 103
title: Freeing a dragon aborts: GS_Dragon (stage 8) has no native producer
status: open
symptom: the port aborts during Artisans gameplay with 'NATIVE RENDER NOT IMPLEMENTED — stage selector = 8 (no producer is registered for this stage)'; reported by the operator as a crash when Spyro breathes fire
tags: render,field,cutscene,dragon,producer,crash
created: 2026-09-08
updated: 2026-09-14
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

## `0x80059F8C` landed, and what the FIELD path was missing with it

The producer is implemented (`game/render/moby_shadow_recipe.*`, `moby_shadow_submitter.*`,
`fx_moby_shadow.*`) and wired into stage 0, since `0x80019698` draws Moby shadows between the shaded
pass and Spyro's own model and the port's FIELD composition had every other layer of that routine.
Staging was independently broken — see `docs/project-state.md` for the `0x1200` sign — so no Moby had
cast a shadow in this port at all.

Every layer of `0x80019698` is now owned and wired into FIELD, including glows/sparkles
`0x80058BA8` (`asm/renderers/r_particles.s`); see below.

## Flame `0x80058D64` ported, wired, and the cross-producer publication it needed

`game/render/spyro_flame_recipe.*`, `spyro_flame_submitter.*` and `fx_spyro_flame.*` port the
handwritten routine: eight parts walked last to first, each a tip fan of four untextured Gouraud
triangles followed by a ribbon of Gouraud textured quads walking backward through the part's
cross-section array, with retail's own ring scales, five-per-row grey ramp, half-step closing pair,
and the two ordering-table shifts (7 for the tip, 8 for the ribbon). A new `Flame` link phase sits
between `SpyroShadow` and `Cyclorama`; nine phases no longer fit three bits, so the phase field is
four bits wide.

Measured on a live flame in Artisans: parts 8, tips 8, ribbon quads rising 15 -> 155 as the flame
extends, with the empty-part, past-limit and tip-backfacing rejections all observed.

It was first held out of the FIELD composition because the geometry was degenerate. `0x80023AC4`
reads its live GTE rotation matrix back at `0x8002401C` and publishes it into `g_SpyroFlame+0xB8..+0xC8`
at `0x80024110`, gated on `g_SpyroFlame+0x9A`, and the port's native Spyro producer replaced that
routine without carrying the publication over. The five words were zero at runtime while the flame
position updated correctly, so every flame-local point projected onto the flame origin and the entire
ribbon collapsed onto one pixel — measured: all four tip triangles of all eight parts had `NCLIP == 0`
at screen (342,117).

`game/render/spyro_flame_matrix.*` now carries that publication and the FIELD composition calls the
flame after `0x80059A48`. The matrix published is the port's layer 1 matrix: retail loads the camera
rotation into the GTE at `0x80023F18`, composes `g_Spyro+0x0C`, publishes, and only then composes
`g_Spyro+0x10` for layer 1 — but layer 1 is computed from the same parent matrix and the same angle
word, so the two agree, and layer 2 is wrong because `0x80024148` restores the layer 0 matrix first.

Wiring it in exposed a second defect that made the port abort on a producer the flame does not touch.
The tip fan is untextured and the submitter named that with colour mode -1; the queue's untextured
sentinel is 3 and `validateFace` rejects any mode outside 0..3, so the flame's items poisoned the
painter-object preflight of the NEXT producer and the frame aborted with "environment producer
0x8002B9CC refused its atomic recipe". A textured face whose colour mode reads 3 is now an atomic
`InvalidMaterial` refusal in `prepare` rather than a face silently skipped during emission.

Measured after both fixes, over one breath in Artisans: parts 8, tips 8, ribbon quads 8..160, tip
rejections 0..24 of 32, and the census decaying back to zero as the flame dies. `scratch/screenshots/
flame.ppm` shows the flame drawn in front of Spyro. The operator's "crashes when you breathe fire" is
still the stage-8 abort below, not this one — the flame was not wired in when they saw it — but a
player who breathes fire now gets a flame instead of nothing.

## Related

Other reachable gamestates with no native producer, same abort: 1 (GS_LevelTransition), 2/3 (pause
and inventory, already recorded), 9 (GS_EntranceAnimation), 10 (GS_ExitLevel), 11 (GS_Fairy),
12 (GS_Balloonist), 15 (GS_Credits).

## Glows `0x800580F4` and sparkles `0x800584C4` ported, and `0x80058BA8` wired

`0x80058BA8` is a two-line C function: `func_800580F4()` then `func_800584C4(g_DeltaTime)`.

`game/render/glow_recipe.*` and `glow_submitter.*` port the first. Sixteen records of 0x24 bytes at
`0x80078800`, cleared by `func_80058B68`, each holding a point count at `+0x00`, a screen-space
direction table at `+0x04` (pairs of words, stride 8), a followed world position at `+0x08`, a colour
at `+0x0C`, a radius at `+0x10`, a world offset at `+0x14`, and an ordering-table bias at `+0x20`.
Per record: subtract the camera, scale the whole delta down by `min(manhattan >> 13, 4)` so the GTE's
16-bit vector registers cannot overflow, project, restore the depth by the same shift, then
`IR0 = (radius << 12) / depth` and multiply each direction pair by it with `GPF 1` to get screen
offsets from the centre. The triangles are GP0 0x32 — untextured, semi-transparent — with the centre
carrying the record's colour and both ring vertices black, headed by an `0xE1000220` draw-mode packet
that selects blend mode 1 (additive) and dithering. N ring points give N-1 triangles; the direction
table repeats its own first entry when the author wanted a closed halo. Rejects: an empty record, a
zero restored depth, a bin at or in front of the table's front, and the four-edge outcode AND across
centre and both ring points.

Note the packed vector word is built with `or` over a masked low half here, not the `add` the world
and actor paths use, so a negative X does not borrow into Y. Six focused tests pass.

`game/render/sparkle_recipe.*` and `sparkle_submitter.*` port the second half. Eight records of 0x18
bytes at `g_Sparkles` = `0x80077108` (`asm/data/game.bss.s`), read as a life byte at `+0x0C`, a total
lifetime at `+0x0D` used as the fade denominator, an angle at `+0x0E`, a signed spin rate at `+0x0F`,
a colour at `+0x10`, a size multiplier at the low byte of `+0x14`, and a far-depth limit at its
second byte. `g_DeltaTime` is `0x800756CC` (`lui 0x8007` / `lw 0x56CC`, `asm/data/game.sbss.s`).

Per record: `remaining = life - deltaTime`, and a sparkle that runs out is killed on the spot with no
angle write. Otherwise the life and the spun angle are stored, `fade = (|total - remaining| << 8) /
total`, and the position is projected — packed with `or`, like the glow and unlike the world path, so
no borrow. Four kill-and-skip tests follow, each of which also zeroes the lifetime: the depth
reaching the record's own far limit, the depth inside `0x80`, and the packed `SXY2` word leaving the
retained window on either axis.

The cross itself is the interesting part. `GPF 0` scales the sine/cosine pair by
`(0x100 - fade) * size` to give one arm's reach, and the four corners are then projected a SECOND
time through a matrix `ctc2`'d from `max(MAC3, 0x1000)` — the view depth, floored — with a
translation of twice the view position. The doubling cancels in the perspective divide and the depth
scale cancels the perspective shrink, so a sparkle holds a constant screen size until it comes
closer than the floor. Those corner words use `add`, so they DO borrow. Retail then builds two
packets in one 0x20-byte pool slot and links the second into the bin, whose tag points at the first,
so the corner 1-2 stroke is ahead of the corner 0-3 stroke. Bin is `(SZ3 >> 5) - 6`, floored at zero
and stepped 0x46 further back past 0x100.

`0x800584C4` writes guest state, which no other producer in this port does. The derivation stays pure
and returns the lifetime/angle writes; `fx_glow_sparkle.cpp` commits them at one named call, only
once the frame is certain to be accepted, so a refused submission cannot silently age every sparkle.
A zero total lifetime would be a hardware divide by zero, which is undefined rather than reproducible
— that record keeps retail's advance and produces no line, counted as `no_lifetime`. Eight focused
tests pass.

The lines needed a framework admission: `RenderQueue` has always carried `nv = 2` and `emitItem` has
always had a `gpu_vk_draw_line` path, but `painter_object_layer.cpp`'s `validateFace` admitted three
and four vertices only. It now admits two, untextured only, because a GP0 line carries no texture
word on the hardware and a textured line would be drawn with its material silently dropped (psxport
`25a432e3`, 145/145 tests, positive and negative cases plus one proving a queued line does not
refuse the next producer's preflight).

`fx_glow_sparkle.*` owns `0x80058BA8` and is called last in the FIELD sequence, with a new `Sparkle`
link phase below `Glow`. Measured live in Artisans: one active glow record fanning 4–8 faces per
field, one live sparkle emitting two lines and aging out to `alive=0` on its own schedule at `dt=2`,
no refusal, and no Lightrec fallback across 20.5 M translated blocks.

## The stage-8 producer landed, and the four defects it uncovered

`game/render/dragon_scene_recipe.*` derives the branch `0x8001CFDC` will take from
`g_DragonCutscene` and `fx_dragon_scene.*` applies it. The recipe is a plan — a producer list, the
two Moby lists to publish, and which source the regular actor pass reads from — so the eight-branch
state table is one readable structure instead of eight copies of a composition. Fourteen focused
tests cover it. State 0 shares FIELD's model chain through the extracted
`game/render/field_model_chain.*` rather than a second copy of those seven layers.

Two owners the branch needed and did not have:

- `0x80058864`, the burst star, drawn before every branch when `D_80076248`'s enable word is set.
  It is armed in the real cutscene. `dragon_burst_recipe.*` / `fx_dragon_burst.*` port it: eight
  spokes, an inner and an outer sine-table ring around one projected origin at shifts 12 and 10, the
  last inner point copied in front of the first so the ring closes, and two triangles per spoke — one
  out to the outer point, one back to the shared centre. It links into the HUD table, not the world
  one. Six focused tests.
- `0x80018728` (the "Rescued ..." text mobys) and `0x80018880` (the HUD-moby copy) emit no GPU
  primitives at all; they build guest lists. They execute through Lightrec, which is what the product
  architecture is for.

Driving into the cutscene then found four real defects, none of them in the new code:

1. **The paired-actor ownership gate aborted with no message.** `drawFrame` asserts exactly one
   invocation of `0x80023AC4` when the scene draws Spyro and exactly zero when it does not, and its
   scene predicate knew only about FIELD and the front end. The dragon states draw Spyro too, so the
   gate failed and `drawFrame` called a bare `abort()` — a fatal with no diagnostic. The predicate
   now asks the dragon state table itself (`spyro_dragon_scene_draws_player`).
2. **The Moby scale byte at `+0x57` was unimplemented.** The shaded queue refused any actor carrying
   one; the regular actor pass refused it too, under the misleading name `TransformBlend`, because
   the byte arrives in the low bits of the record header. Both retail renderers do the same thing
   with it (`0x80022CCC` and `0x8001F864`): run the shifted view translation through `GPF` with the
   byte in IR0 and read MAC back with `sra 5`, leaving the rotation and the depth key untouched. A
   zero byte means "unscaled" and skips the multiply. `actor_transform_math::scaledTranslation` is
   the one implementation; the scaled path in `0x8001F798` additionally drops a record whose scaled
   depth passes `0xF618`, which is a per-record cull, not a refusal.
3. **Particles emitted unordered world items.** `0x800573C8` published no painter object, and the
   queue refuses a world run that mixes ordered and unordered items — it had simply never yet met a
   frame with particles alive alongside another world producer. `0x80057724` links each packet into
   the bin head and threads the previous head forward to it, so the emit list replays in scan order
   and on top of everything else in its bin; `LinkPhase::Particle` is the new phase 0. The ordinal
   has to be the record's position in the guest's single scan, because the three arms interleave
   there and are split into three lists here, so `field_particles_recipe` now carries `scanOrdinal`.
4. **`spyro_field_player_submit` applied a hide gate the cutscene does not have.** Only state 0
   reaches Spyro through `0x80019698`, which owns that gate; every other branch calls `0x80023AC4`
   directly.

Measured after all four: the Artisans dragon cutscene runs to completion with no refusal and no
abort. One walk covered every branch, with the frame counts each held:

    state 0: 23   state 1: 48   state 2: 8    state 3: 12
    state 4: 527  state 5: 13   state 6: 48   state 7: 16

State 0's fade ramps 0 -> 255 into state 1, and state 7 fades back out to 31 as the cutscene ends,
so the whole sequence is covered rather than sampled at its start.

### Note (2026-09-14)
## 2026-09-14: the cutscene still aborts, and the cause is now measured

The "Measured after all four: the Artisans dragon cutscene runs to completion with no refusal and no
abort" claim above does NOT hold for the route in this issue. Re-run today,
`tools/drive.py gameplay --hold up --hold-frames 90 --tap circle --after 40 --repeat 15`, reaches
stage 8 and aborts:

```
[fieldactors] REFUSED shaded recipe=UnsupportedVariant variant=1 actor=0x80171010 primitive=0 candidates=1
[dragon]      state=1 ticks=2 fade=192 draw=1 shaded=1 steps=13 REFUSED at 0x80022A2C
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 8 (dragon cutscene producer 0x8001CFDC
              refused its atomic composition)
```

Log: `scratch/logs/dragon_why4.log`. So the eight-branch walk that produced the completion claim
carried a different Moby set; it cannot be quoted as coverage of this route. The stage-8 producer
itself is present and does run — what refuses is one primitive inside the shaded producer.

## Cause, from the source

`0x80022A2C`'s variant word: `r_moby.s` 0x80023320/0x80023324 test bit 0 (`andi $t6,$at,0x1`) and bit 1
(`andi $t7,$at,0x2`) SEPARATELY, and bit 0 branches (`bgtz $t6, .L80023534`) while bit 1 is read only
INSIDE that path (`bgtz $t7, .L80023720`). Two consequences:

1. `.L80023534`'s fall-through (bit 0 set, bit 1 clear = variant 1) is the flat TEXTURED family: the
   branch loads `0x28` (a textured quad) or `0x20` (untextured triangle, when the last two indices
   match), takes its UVs from the light table's `+0x200` half, and adds `0x02000000` to the packet
   word. This is the same family issue 0111 left refused (the eight `0x26` packets in the world OT),
   so the cutscene abort and 0111's residual are ONE missing feature.
2. Variant 2 (bit 1 alone set) is bit-identical to variant 0, because bit 1 is never read with bit 0
   clear. Refusing it was refusing a combination retail cannot distinguish.

## Landed here

- The recipe now decides the path with bit 0 and accepts variant 2 as the per-vertex path; variant 1
  is still refused, by name, with `firstUnsupportedVariant` reported. `tests/test_field_shaded_queue_recipe.cpp`
  covers both directions (8/8, 47 checks).
- Two silent refusals in this path now name their reason: the submitter plan prints its status plus
  the painter-preflight inputs, and the inverted-draw-area path prints the four registers it rejected.
  `statusName` was added for `field_shaded_queue_submitter`, `actor_face_submitter` and
  `field_shaded_queue_recipe` so refusals stop printing bare enum values.

## Remaining

Implement the flat-textured family in the recipe AND in `gpu_packet_decode` (both, or the oracle
cannot see what the recipe emits). Shape from `.L80023534`..`.L800236D4`, with `.L80023650` as the
semi-transparency arm. That is the fix for this abort, for the gems' `0x26` packets, and for the
cutscene actor 0x80171010.

### Note (2026-09-14)
## Variant 1 vs variant 3: the exact delta, read from the two arms

Both arms emit the SAME flat packet family this port already supports, so the shape is not the
problem. The differences, arm to arm:

| | variant 1 (bit 0 set, bit 1 clear, `.L80023534`) | variant 3 (bits 0 and 1, `.L80023720`) |
|---|---|---|
| packet | `0x20` (5 words, size 0x14) / `0x28` (6 words, 0x18) | same family |
| light table | `D_8006E3D8` + (`LO >> 22`) | `D_8006E44C` + (`LO >> 21`) |
| entry words | one (`lw $t3,0x0($t3)`) | two (`0x0($t2)`, `0x4($t2)`) — the port's `lightBase`/`lightScale` pair |
| colour | record word r/g/b into IR1..3, `GPF 0`, `CC` | same, plus the reverse-facing `>>10` and the `factor - 1472` boost |
| near / facing | gate on `$t7` (the index) alone | `TRZ - 0x800` near test, `sign << 25` / `lui 0x02000000` semi-transparency, `addi $v0, 0x200` OT bias |
| output flag | `ori $s7, 0x80000001` | `ori $s7, 0x80000002` |

`shade()` in `field_shaded_queue_recipe.cpp` already models the variant-3 column (its `>>10`, its
`lightBase`/`lightScale` pair, its `factor - 1472` boost, its unpack), so the work here is a
PARAMETERISED light lookup plus the variant-1 facing rule — not a second shader.

## The one input still open, and why it is not being guessed

`LO`'s producer. `mflo $t7` at `0x80023538` reads whatever the last MULT/MVMVA left, and neither the
primitive loop head (`0x800232A8`) nor the bit-0-set path between `0x80023338` and `.L80023534` sets
it, so the light index arrives from outside the loop. That index is the whole colour. Implementing
the family on a guessed index would produce a cutscene that runs with wrong colours and reads as a
success — so the trace of that producer is the next step, before any colour code is written.

Everything else about the family is established above, and the refusal currently names
`variant=1` plus the actor and primitive, so the moment the index is known the change is bounded.
