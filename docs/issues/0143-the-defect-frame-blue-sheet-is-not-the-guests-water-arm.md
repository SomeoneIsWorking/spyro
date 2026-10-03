---
id: 0143
title: The "pool water" in the defect frame is a 50%-blended screen rectangle, and the guest's water arm is unreachable in Artisans
status: open
symptom: `docs/issues/0138` and the fix at `c229e45` rest on two claims: the pool is a grid of
  low-poly world faces carrying material bit 2, and the defect frame's blue sheet is those faces'
  per-face colour. Both were measured against the resident level and neither holds. A translucent
  screen-aligned rectangle of constant colour RGB(0,56,192) is what the defect frame contains, and
  0 of 1783 low-poly faces in Artisans' Home carry material bit 2.
tags: render,water,blend,probe
created: 2026-09-28
updated: 2026-09-28
---

> **The attribution this issue left open is now closed, and its answer is not either named candidate.**
> `docs/issues/0144` names the producer: the rectangle is the **pause menu's panel quad**, submitted
> by `spyro::pause_menu_scene::submitPanel` under guest handler `0x8001A40C` at guest
> `g_Gamestate` = 2, and it is painted **147 of its own 232 columns under widescreen** because the
> port passes a guest-space constant as a drawn-space clip. `func_80018F30` and `func_8001860C` are
> both excluded by the guest's own bytes. Two corrections to this issue's §1 stand: (a) the
> "unfilled outline box beside it" **is** the same panel's own right-hand border, 86 columns past
> where the fill stops — it was never a second primitive; and (b) the geometry this issue fitted
> assumed the port scales screen-space x, and it does not: the margin is `(684-512)/2 = 86` added
> whole, so the rectangle is the guest's `140..372` and not a `110x110` box at `(169,67)`.
> **`PSXPORT_MUTE_FN`, which §"What would settle it" names as the instrument, no longer exists** (see
> 0144 §1), and §"What would settle it" item 2's "0 of 104 captures" is explained by 0144 §5: the
> overlay is on the picture in half the presents, so a one-shot capture is a coin flip.

## 1. The defect frame's blue is a screen rectangle, and it is not the water

`scratch/screenshots/pause2.png` and `scratch/screenshots/pause-final.png` are the SAME camera: they
are **147,904 of 164,160 pixels byte-identical (90.1%)**, and the 16,256 that differ form an exact
rectangle — `x=226..372` in every one of the ten-row bands from y=70 to y=169, 1470 px per band,
147x110 in total, and nothing outside it differs by a single bit.

Fitting `out = (in + S) / 2` per channel over that rectangle's 16,170 pixels
(`scratch/blend_fit.py`, and the identical fit run on `field-16x9-interp.png` against `pause2.png`):

| channel | fitted S | mean abs residual | within +/-8/255 |
|---|---|---|---|
| R | 0 (0x00) | 6.87/255 | 15,933 of 16,170 |
| G | 56 (0x38) | 5.12/255 | 15,941 of 16,170 |
| B | 192 (0xC0) | 6.28/255 | 15,915 of 16,170 |

So the sheet is **one constant colour at a 50/50 blend** — RGB(0,56,192), PSX word `0xC03800` — and
98.5% of its pixels fit that single source to within 8/255. The outliers are Spyro's own sprite and
the gem's sparkle. A textured or multi-coloured source would not fit one S.

**It cannot be the water the fix reconstructs.** `0xE1000600 | ((material & 7) << 5)` has
**B = 0 for every one of the eight values of `material & 7`** — it is a 50/50 blend with essentially
BLACK, which darkens and desaturates. The measured source is a saturated blue with B = 192. The two
differ in sign on the channel that decides the picture.

**And it is screen-aligned.** The rectangle's top edge cuts the castle archway -- a vertical wall --
horizontally at y=67, and the scene is tinted inside the rectangle and untinted outside on the same
scanline. A sheet lying on the courtyard floor cannot tint the wall above its own waterline. In the
512-wide native framebuffer the rectangle is 110x110 at (169,67), horizontally centred to the pixel
((169+279)/2 = 224 vs the framebuffer's 256, with the second, unfilled outline box beside it at
(281,67)-(343,179) completing a centred 174x112 pair).

## 2. The guest's water arm is DEAD in this level

`tools/pool_viewpoint.py` walks the resident level's own sector list and reads every low-poly chunk
with the port's field layout (`game/render/world/world_chunk_codec.cpp:44`). Over **178 of 178** sectors,
**1783 faces**:

- **0 of 1783** carry material bit 2, which is the guest's water decision at `0x8002651C`.
- The material-byte histogram has **six** values, every one a multiple of 8:
  `0x00:150, 0x10:1160, 0x18:15, 0x20:245, 0x30:212, 0x40:1`. Bit 2 is clear in all six.
- **CONTROL:** 1783 of 1783 faces have every vertex index inside their own chunk's vertex count, and
  5199 of 5199 high-poly faces have every vertex offset inside theirs. This is the level's data, not a
  mis-strided read. (Reading each sector's face table to the NEXT sector's address instead, which is
  a real bound, produces 27,365 "faces" with all 256 material bytes present -- the shape of a scan
  that has run off the end of the tables, and it is why the narrow reading is the one reported.)
- The high-poly path is not the answer either: **0 of 5199** HQ faces have a signed material byte
  below -2, which is `world_material_codec::classify`'s own `semiTransparent` test.

**So `world_lq_recipe.cpp`'s new `translucent` arm cannot execute in Artisans' Home.** The fix is a
no-op for this level, and therefore cannot be the difference between a frame with the sheet and a
frame without it. It is not a regression either: the sheet is absent from `pause2.png`,
`pause3.png`, `pause-menu.png` and `pause_text.png`, which are all **pre-fix** captures of the same
camera.

**SUPERSEDED IN SCOPE, 2026-09-28, by `docs/issues/0145`.** "Dead in this level" was correct and was
not the whole question. Censusing every level DATA entry in `WAD.WAD`
(`tools/census_level_face_material.py`) finds **1,592 translucent faces across 6 of 35 levels**, the
largest population in level 17 (levelId 35, CRYSTAL FLIGHT) at 494 of 3,341 faces across 95 of its
219 sectors. **The arm is live code in this game and unreachable only in Artisans' Home** -- so the
fix is verifiable on a level nobody has driven to yet, and 0143's zero stands as a correct
measurement of the one level it measured.

## 3. `pool_water_probe`'s metric cannot answer the question it is being asked

The tool measures deviation **within** each 16x16 block. A translucent sheet over a textured scene
scores high on that, and so does a textured pool floor, and a *uniform tint over bare ground* scores
low. Over one named region, `x=226..373 y=67..177`:

| frame | sheet | distinct colours | mean deviation | blocks over 6.0 | worst |
|---|---|---|---|---|---|
| `field-16x9-interp.png` (the defect frame) | present | 674 | 20.40 | 58 of 70 | 39.70 |
| `pause2.png` (pre-fix, no sheet) | absent | 1895 | 39.23 | 59 of 70 | 54.85 |
| `pool_viewpoint/seek00.ppm` (current build) | absent | 1870 | 38.74 | 58 of 70 | 55.29 |

**The frame WITHOUT the sheet scores worse on the "defect" metric than the frame with it.** A 50/50
blend with one constant colour compresses exactly the contrast the metric measures. The tool
therefore cannot distinguish the defect frame from a clean one, and its verdict line --
"PER-BLOCK VARIANCE PRESENT -- This is the defect" -- fires on all three, worst on the cleanest.
This is the workspace's recorded failure mode in a new place: an instrument returning a confident
verdict about the wrong quantity.

The **auto-selection** is worse still, and its own output says so. In this level it selects the
**sky**: every one of the 16 fan captures and 4 of the 8 seek captures from the gem tour returned
"VERDICT: PER-BLOCK VARIANCE PRESENT" over a region at `y=8..51`, which is the top of the frame.

## 4. Issue 0140's falsifier, answered on live data

0140 asks whether real water faces set material bits 0 and 1, because the port takes its blend mode
from `(material & 3)` while the guest's command word carries its own semi-transparency code. On the
resident low-poly geometry of Artisans' Home:

    material & 3 == 0   for 1783 of 1783 faces
    material & 4 == 0   for 1783 of 1783 faces

**The falsifier does not fire.** The port's derived blend mode is 0 (`B/2+F/2`, average) for every
low-poly face in this level, which is what the guest's code nibble 1 asks for, so the two encodings
coincide here -- 0140's first outcome, "right by coincidence of encoding". The scope is stated
because it is the whole scope: **one level, the low-poly path, live data at arrival**. No water face
exists to test, and the high-poly path reads a different field entirely
(`classify((int8_t)materialWord, (materialWord >> 8) & 3)`).

## What this does NOT claim

- **Not a claim that the rectangle is not water.** It is water-coloured, it is a 50/50 blend, and the
  reference frame's own author read it as a pool. What is measured is that it is a screen-aligned
  constant-colour composite **and** that it is not the primitive `c229e45` changed.
- **Not an attribution of the rectangle.** No producer is named for it. `func_80018F30` (two POLY_F4,
  front list, `g_ScreenBorderEnabled`, `D_800756C0 border bar height`) and `func_8001860C` (DR_MODE +
  POLY_F4 + 4 lines) are the two screen-space candidates `tools/probe_field_2d_layer.py` names, and
  neither has been muted and tested. `PSXPORT_MUTE_FN` is the instrument.
- **Not a claim about other levels.** Artisans is level 10 (`src/gamestates/init.c:222`). A level
  whose geometry does carry material bit 2 would exercise the fixed arm, and this census says nothing
  about whether one exists.
- **The reference's second, unfilled outline box is also absent from the current build** and equally
  unattributed.

## What would settle it

1. `PSXPORT_MUTE_FN` over each screen-space producer, at a viewpoint where the rectangle IS present,
   with the capture file deleted before each run (C138's own lesson: a stale capture file was read as
   a finding there).
2. A run of the maintained arrival route in which the rectangle appears. It is **not** reachable on
   demand from the current build: **0 of 104 captures** from 7 independent runs
   (one 80-frame strip of 1200 fields at the arrival viewpoint, plus six repeats) produced it.
3. The census of §2 re-run over `WAD.WAD`'s other level entries, to say whether ANY Spyro 1 level
   carries a face with material bit 2. That is the question `c229e45` actually needs answered, and
   `tools/pool_viewpoint.py`'s reader already walks the resident form of it.
