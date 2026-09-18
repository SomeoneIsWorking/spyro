---
id: 113
title: "Secondary actors abort outside Artisans: the specular lighting program is unimplemented"
status: open
symptom: the port aborts with 'secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic recipe' whenever a secondary actor with control bit 2 is drawn; first seen in the attract demo, now reproduced on demand by entering Stone Hill
tags: render,field,actor,secondary,lighting,re,crash
created: 2026-09-14
updated: 2026-09-18
---

## Symptom

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 0
  (secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic recipe)
[render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8
  stage=0/3/2 load_stage=4294967295 state_switch=0
[watchdog] FAULT (signal): backtrace:
  SpyroRenderer::abortUnimplemented -> renderScene -> drawFrame -> Spyro1FrameDriver::stepFrame
```

## Reproduction (deterministic, 2026-09-18)

Walk out of Artisans through any portal. The refusal follows within a few hundred fields of the
level becoming playable:

```
uv run --frozen python tools/drive.py gameplay --seek-portal --debug fieldactors --after 900
```

The same route inside `tools/oracle_compare.py`'s `level` checkpoint aborts the product the same
way, after both cores have entered level 11.

This supersedes the earlier attract-demo observation, which was never deterministic. The attract
demo simply happened to be the only route that drew such an actor; the discriminator is the actor,
not the demo. Every ordinary driven run stayed inside Artisans, whose secondary actors do not use
this program, which is why nothing saw it for so long.

## Root cause

`fieldactors` names it exactly:

```
[fieldactors] REFUSED secondary recipe=3 reason=0 record=0 source_word=65
```

Status 3 is `secondary_actor_recipe::Status::UnsupportedLighting`. `game/render/secondary_actor_recipe.cpp`
refuses any emitted candidate whose first prefix word has bit 2 set:

```cpp
// Bit 2 selects the distinct view-normal/specular program at 0x80021C70.
// Base material colour does not reproduce that lighting contract.
if (candidate.evaluation.emitted && (candidate.input.words[0] & 4u) != 0u) {
```

The refusal is the designed deliverable for an unported variant, not a defect in itself. The gap is
the missing program: retail's `func_80020F34` (external/spyro-1 `asm/renderers/r_moby.s`, from asm
line 2069; the arm begins at guest `0x80021C70`) computes a per-face specular term from the view
normal instead of taking the base material colour. It is not decompiled to C, so recovering it is
MIPS reading.

## The program, transcribed (2026-09-18)

`func_80020F34` keeps two spare registers in HI and LO for its whole run. `mtlo $gp` at `0x8002173C`
parks the real global pointer so `$gp` can be repurposed as the vertex-scratch cursor, and
`mthi $at` at `0x80021768` parks the word at `$gp + 0x30` = `0x80075294`, which selects between the
two per-face colour programs. Measured at the refusal: `0x00535245`. Its top byte is the selector —
zero here, which is what sends the face into the specular arm rather than the additive one at
`0x80021FE0`.

Control bit 2 branches at `0x80021C0C` into `.L80021DB4`. The arm computes ONE colour and uses it
for all of the face's vertices, then rejoins the ordinary emission path at `.L80021C14` with
`$t2 = $t3 = $t4` set to it; the ordinary path would instead have taken three colours from the
material table at `words[1]` offsets `(>>17)&0x7FC`, `(>>8)&0x7FC`, `(<<1)&0x7FC`, which is exactly
what `actor_draw_recipe.cpp` already reproduces.

```
t6 = HI                              ; the program-select word
if ((s32)t6 >> 24 != 0) -> 0x80021FE0 ; the other program
t5 = LO                              ; both are restored before the jump back

; two edge vectors from the three vertices' scratch records
IR1 = SY1-SY0 ; IR2 = lo(w1)-lo(w0) ; IR3 = hi(w1)-hi(w0)
R11R12 = SY2-SY0 ; R22R23 = lo(w2)-lo(w0) ; R33 = hi(w2)-hi(w0)
save RBK, GBK
OP 0                                 ; cross product -> MAC1..3

; the material colour becomes the GTE background colour
RBK = (t6 >> 6) & 0xFF0 ; GBK = t6 & 0xFF0 ; BBK = (t6 << 6) & 0xFF0

; |n|, through the guest's own sqrt helper
IR1 = MAC1>>4 ; IR2 = MAC3>>4 ; IR3 = MAC2>>4   ; note the 3/2 swap
SQR 0 ; sum = MAC1+MAC2+MAC3
LZCS = sum ; normalise, index D_80074B84, shift back  -> len   (0 when sum == 0)

LO = ((t6 >> 18) << 14) / len        ; the intensity
; one colour, splayed across the light-colour matrix
c0,c1,c2 = u16 [D_800770C8 + 0xC, +0x10, +0x14]
LR1LR2 = c1<<16|c0 ; LR3LG1 = c0<<16|c2 ; LG2LG3 = c2<<16|c1 ; LB1LB2 = c1<<16|c0 ; LB3 = c2
RGB = 0xFFFFFF ; IR0 = LO
GPF 0                                ; MAC = IR0 * IR
IR1..3 = MAC1..3 >> 8
CC                                   ; MAC = BK*4096 + LCM*IR
colour = RGB2 ; restore RBK, GBK, LO, HI
```

So the face colour is the material colour plus a light colour scaled by the screen-space face
normal over its own length — a per-face directional term, not a per-vertex one. Every operation is
a real GTE op, and the port already reaches the GTE through `gte_op` and reproduces this exact
normalise/`D_80074B84`/shift tail in `game/core/native_gte.cpp`, so a native implementation does
not need a second software GTE.

### What is still unmeasured

Which native field each scratch half corresponds to. The arm reads a halfword at `+2` and a word at
`+4` of an 8-byte per-vertex record at `(vertexByteOffset << 1) + D_8006FCF4 + 0x900`, and the
projection loop at `0x800212B0` writes `SZ3`, `MAC1`, `MAC2`, `MAC3` as four halfwords at `$sp` and
the packed `SXY2` as a word at `$fp`, so the mapping onto `PrimitiveInput::xy` and `::depth` is not
yet pinned down. One runtime read of that record beside the native `projected` values settles it;
the port must take the values from its own projection, not from guest scratch.

## Why it matters

It is the first hard stop on any route that leaves Artisans, so it blocks representative gameplay
(state item S011) rather than one cutscene. It also bounds what the oracle's `level` checkpoint can
compare: the product dies partway through the segments that follow the level entry.

## Not the same as issue 0103

0103 is the dragon cutscene's shaded producer refusing a single un-implemented variant; that is fixed.
