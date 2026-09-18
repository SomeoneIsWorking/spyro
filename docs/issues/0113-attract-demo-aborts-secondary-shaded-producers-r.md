---
id: 113
title: "Secondary actors abort outside Artisans: the per-face colour program is unimplemented"
status: open
symptom: the port aborts with 'secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic recipe' whenever a secondary actor with control bit 2 is drawn; first seen in the attract demo, then reproduced on demand by entering Stone Hill
tags: render,field,actor,secondary,lighting,re,crash
created: 2026-09-14
updated: 2026-09-19
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

A `level` checkpoint in `tools/oracle_compare.py` aborted the product the same way, after both
cores had entered level 11. That checkpoint has since been withdrawn for an unrelated reason
(issue 0114), so `tools/drive.py` is the reproduction.

This supersedes the earlier attract-demo observation, which was never deterministic. The attract
demo simply happened to be the only route that drew such an actor; the discriminator is the actor,
not the demo. Every ordinary driven run stayed inside Artisans, whose secondary actors do not use
this program, which is why nothing saw it for so long.

## What bit 2 actually selects

The measured refusal carried `control=0x038181A6`. Bit 31 is clear, so the face is a TRIANGLE, and
`func_80020F34` (external/spyro-1 `asm/renderers/r_moby.s`) dispatches on bit 2 differently for the
two topologies:

| face | where bit 2 branches | what it is |
|---|---|---|
| quad (`words[0] < 0`) | `0x800217E0` -> `.L8002256C` | a camera-facing textured billboard: one projected vertex, a depth-cued half-size, and a GT4 packet built from four screen-aligned corners |
| triangle | `0x80021B64` sets `$a1`, `0x80021C0C` -> `.L80021DB4` | one computed colour for all three vertices, replacing the three material-table colours |

The evaluator already refuses the quad arm as `Reason::Ft4` and advances the prefix cursor by its
five words, which is correct and unchanged. The triangle arm is the one this issue is about.

An earlier revision of this issue transcribed the triangle arm but attributed it to bit 2 on a quad
and named its control word `$gp + 0x30` = `0x80075294`. Both were wrong: `$gp` is repurposed as a
cursor into `D_8006FCF4 + 0x1600` from `0x80020F78` and restored from LO at `0x8002287C`, so the
word is the draw record's own `+0x30`. Both record builders copy it there from the Moby's `+0x4C`
(`0x80020CA0` reads `0x4C($sp)` with `$sp` holding the Moby, and stores `$sp` itself at `+0x34`),
which the port already captures as `secondary_actor_scene::Record::lightingControl`.

## The triangle program, transcribed

`func_80020F34` parks that word in HI at `0x80021768` for the model's whole face loop, and each
face re-reads it with `mfhi`. Its top byte selects between two programs; the arm below is the one
that runs when the top byte is zero.

```
t6 = HI                               ; the Moby's +0x4C word
if ((s32)t6 >> 24 != 0) -> 0x80021FE0  ; the other program

; two edge vectors from the three vertices' 8-byte projection records
IR1 = x1-x0 ; IR2 = y1-y0 ; IR3 = z1-z0
RT11 = x2-x0 ; RT22 = y2-y0 ; RT33 = z2-z0
save RBK, GBK
OP 0x4B70000C  (sf=0, lm=0)            ; cross product -> MAC1..3

; the material colour becomes the GTE background colour
RBK = (t6 >> 6) & 0xFF0 ; GBK = t6 & 0xFF0 ; BBK = (t6 << 6) & 0xFF0

; |n|, through the same table libgte's own sqrt uses
IR1 = MAC1>>4 ; IR2 = MAC3>>4 ; IR3 = MAC2>>4   ; note the 3/2 swap
SQR 0x4AA00428 (sf=0, lm=1) ; sum = MAC1+MAC2+MAC3
LZCS = sum ; normalise, index D_80074B84, shift back -> len

LO = ((t6 >> 18) << 14) / len          ; the intensity
c0,c1,c2 = u16 [D_800770C8 + 0xC, +0x10, +0x14]
LR1LR2 = c1<<16|c0 ; LR3LG1 = c0<<16|c2 ; LG2LG3 = c2<<16|c1 ; LB1LB2 = c1<<16|c0 ; LB3 = c2
RGB = 0xFFFFFF ; IR0 = LO
GPF 0x4B90003D (sf=0, lm=0)            ; MAC = IR0 * IR
IR1..3 = MAC1..3 >> 8
CC  0x4B38041C (sf=1, lm=1)            ; MAC = BK*4096 + LCM*IR, then RGBC * IR
colour = RGB2 ; restore RBK, GBK, LO, HI ; rejoin .L80021C14 with t2 = t3 = t4 = colour
```

The light-colour matrix ends up with all three rows equal to `(c0, c1, c2)`, so the term is
uncoloured before the material background is added. The rejoin point is the only place the arm
touches: nothing but the three vertex colours differs from the ordinary path.

The 8-byte per-vertex record is written by the projection loop at `0x800212B4`: `+0` is SZ3 and
`+2`/`+4`/`+6` are the RTPS MAC1..3 outputs, so its cursor `D_8006FCF4 + 0x900 + (index << 1)` holds
view-space coordinates, while the packed screen XY goes to the scratchpad at `0x1F800000`. The port
takes those coordinates from its own projection's `raw_view_fixed`, never from guest scratch.

## What the port does now

`game/render/face_light_program.cpp` owns the transcribed arm as a pure function over the three
view-space vertices, the control word, and a snapshot of the light colour and magnitude table that
`face_light_environment` takes from the Core once per composition. `tests/test_face_light_program.cpp`
runs the pure result against the real GTE for 23 faces through `GTE_ExecuteIsolated` and the same
`mtc2`/`ctc2` register-transfer path the guest uses, which is what settles the shift and saturation
flags; it also proves the comparison can fail.

## What is still refused

The other program at `0x80021FE0`, which a non-zero top byte selects. It adds a constant to each
vertex colour's red byte with saturation, subtracts it from green and blue, and forces the packet's
command byte to `0x34`, so it changes the emitted primitive rather than only its colours and does
not belong in the colour path. `face_light::Status::Additive` names it, and a face that asks for it
still refuses the whole call. Nothing has yet been observed reaching it.

The quad billboard at `0x8002256C` is likewise still refused, as `Reason::Ft4`.

## Why it matters

It was the first hard stop on any route that leaves Artisans, so it blocked representative gameplay
(state item S011) rather than one cutscene.

## Not the same as issue 0103

0103 is the dragon cutscene's shaded producer refusing a single un-implemented variant; that is fixed.
