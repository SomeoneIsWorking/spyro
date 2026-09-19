---
id: 126
title: The `playing` picture checkpoint compares two different moments of the level intro, so its 18.49% ranks nothing
status: resolved
symptom: tools/picture_oracle.py reports 22717/122880 pixels (18.49%) differing at the `playing` checkpoint with EVERY picture-decisive range equal, so it reads as a rendering defect. It is not one — the reference is on the black "THE ADVENTURE BEGINS…" card and the product is already in the lit Artisans courtyard
state_items: S019, S020
tags: oracle,picture,instrument,checkpoint
created: 2026-09-19
---

## The number, and why it looks like a verdict

`tools/picture_oracle.py --bios ../SCPH1001.BIN`, reproduced identically on three separate runs
(2026-09-19):

```
[picture] console: playing after 1532 game frames
[picture] native:  playing after 2300 game frames
[picture] playing: 22717/122880 pixels differ (18.49%), 224/480 tiles touched — spread
[picture]   worst tiles (96,16):256, (272,16):256, (368,16):256, (384,16):256
```

`PictureRun.at` runs the issue-0119 state guard before it photographs, and it did NOT refuse here —
so every picture-decisive range, `camera` and `dragon_cutscene` included, was EQUAL. A reader is
entitled to conclude from that pairing that the renderer is 18% wrong in gameplay. It is the only
gameplay picture number this project has.

## What the two pictures actually are

* reference: a near-black frame with `THE ADVENTURE BEGINS….` in gold (the capture is a 1,862-byte
  PNG — almost all one colour).
* product: the Artisans courtyard, lit, Spyro on the pad, sky banding across the top. That is why
  the worst tiles sit on the y=16 row: the product draws sky where the reference draws black.

They are two different moments of the same level-intro sequence.

## FALSIFIED: the product is missing the intro-card producer

That was the obvious reading of the pair and it is wrong. The product renders the card: claim 228
records the user observing "THE ADVENTURE BEGINS transition card" from the running product, and
`docs/project-state.md` measures a `level-intro card (f3300)` capture on the artisans-arrival
replay, noting the scene there is black. The producer exists and runs.

## The mechanism

`reach_playing` drives each core until `gamestate == GS_PLAYING` and photographs it the moment that
holds. That predicate becomes true DURING the intro, whose phase — card up, fading, lit — is not in
`picture_decisive` (the declared decisive ranges plus `camera` and `dragon_cutscene`, all of which
genuinely agree at that instant). So the guard passes and the two cores are photographed at
whatever point of the animation each happened to trip the predicate.

Measured on the product with `tools/drive.py gameplay --settle N`, all three runs reaching
`GS_Playing` at frame 6360:

| settle | non-black (>24) pixels |
|---|---|
| +1 | 66426/122880 (54.1%) |
| +30 | 114670/122880 (93.3%) |
| +90 | 114663/122880 (93.3%) |

The product is already half-lit one frame after the transition and fully lit by 30, while the
reference at its own +0 has not started. The 768-frame gap between the two arrival counts is NOT a
product slowdown — each core is driven to a STATE, not a frame, and the reference spends ~50 frames
of real card I/O creating the save where the product's HLE takes 6 (already recorded in this
title's `excluded` notes) — but it does mean the sequences are offset when the shutter fires.

## The guard is not broken; the predicate is too coarse

The same guard refuses correctly one step later. `--play 600`:

```
[picture] played-600f: REFUSED — the two cores are not at the same guest state here, so a picture
difference would not be about rendering: camera (+0: native E5 console F8),
dragon_cutscene (+24: native D6 console C2)
```

So where the compared state really differs, it is caught. What is missing is a decisive quantity
that differs while the intro is animating.

## What this does NOT say

It does not clear the renderer, and it does not change issue 0120. It says this number cannot be
read either way: 18.49% is not evidence of a defect, and driving it to 0% by changing rendering
would not be evidence of a fix.

## Next

Either give `playing` a decisive range that carries the intro/fade phase (so the guard refuses
instead of printing an uninterpretable percentage), or move the gameplay picture checkpoint to a
settled in-level state both cores can be driven to and compared at. The second is also what issue
0120's remaining open item needs — a console-comparable scene with an occluded gem — so one
settled-gameplay checkpoint would serve both.

## RESOLVED 2026-09-20 — a settled checkpoint, and a comparator that separates rounding from rendering

Two changes, because the checkpoint alone would have produced a second uninterpretable number.

### 1. `settled_play`, and `playing` no longer photographed

`tools/oracle_spyro1.py` gained `SETTLED_GAME_TICK = 180` and a `settled_play` checkpoint driven to
`gamestate == GS_PLAYING and g_GameTick >= 180`. `g_GameTick` counts GS_Playing updates and is
already a decisive range, so the shutter is aligned by a quantity the RAM comparison independently
checks — unlike GS_Playing itself, which this issue showed becomes true mid-intro. Measured:

```
[picture] console: settled_play after 179 game frames
[picture] native:  settled_play after 179 game frames
```

The same count on both cores, against 1532 vs 2300 at `playing`. Both frames are the Artisans
courtyard from the same camera with Spyro on the pad — a real comparison, which `playing` never was.

`playing` is kept as a STATE checkpoint and declared `not_picture_comparable` (a new
`compare.Checkpoint` field, honoured by `picture.py`), so the run prints the reason instead of the
18.49%. Withholding also skips `advance_to_presented` there: that advance is per-core and unequal,
and paying it for a photo nobody takes would leave the two cores at different game frames.

### 2. The count was measuring rounding

`settled_play` first reported 54.62% of pixels differing. The magnitudes were banded on multiples of
8 — one 15-bit PSX colour step in 8-bit output (255/31 = 8.22) — and **29.45% of the whole frame
differed by exactly one step**, which no player can see. `compare_pictures` counted any inequality,
so rounding and a missing object were the same number.

`PictureDiff` now carries `significant` (magnitude beyond one colour step) and a magnitude
distribution, ranks `worst_tiles` by significant pixels only, and leads its printed line with the
colour difference while keeping the bare count beside it (the selftest keys on bit-identity and
must not move). At `settled_play`:

| measure | pixels | share |
|---|---|---|
| differ at all | 67113 | 54.62% |
| a different COLOUR (> 1 step) | 30922 | 25.16% |
| > 2 steps | 17666 | 14.38% |
| > 4 steps | 10180 | 8.28% |
| > 8 steps | 4792 | 3.90% |

### What the frame actually shows

`scratch/picture/settled_play.magnitude.png` maps the magnitudes. The residual is three things and
no fourth: every polygon EDGE outlined (sub-pixel rasterisation placement), dither speckle across
textured ground and sky, and two concentrated blobs at roughly (272,112) and (320,96) — Sparx and a
sparkle effect, whose positions live in the moby arrays this title excludes. There is no solid
contiguous region drawn wrongly, which is the signature issue 0120 is looking for and the reason
that issue stays open rather than closing here.

### What this does NOT say

`settled_play` is one scene with no occluded gem in it, so it does not answer issue 0120; it makes
0120 answerable, which is what this issue owed it. 25.16% is still a spread, edge-dominated
residual and is not a clean bill of health for the renderer — it is a number that can now be read.


## 2026-09-20, second defect of the same kind: a whiteout is as uninformative as a blank frame

Driving the scripted route on from `settled_play` found the same failure one step along.
`--play 480 --frame-step 60` compared at f180 and reported **23.94% of pixels a different COLOUR**
with every decisive range equal. Both frames were a near-white flash: the reference 56.76% one
colour, the product 87.76%. The number was about which moment of the flash each core was on.

The existing refusal did not catch it, because a fade is not `uniform` — it is one colour plus faint
tints, so `distinct_colours` was 28 and 102, not 1. `Picture` now carries `modal_share`, the share of
the frame the single commonest colour covers, counted on the console's own 15-bit grid so a dithered
fade still reads as one colour, and `at()` refuses above half. Real Artisans scenes in the same run
measured 6.67%–14.82% (the 6.67% being the letterbox bar), so the separation is a factor of three
and the threshold is a statement — "over half this frame is one colour" — rather than a fitted knob.

Both answers, in the shipping artifact, one run:

```
[picture] settled_play:      30922/122880 a different COLOUR (25.16%)
[picture] played-480f-f60:   30596/122880 a different COLOUR (24.90%)
[picture] played-480f-f120:  30684/122880 a different COLOUR (24.97%)
[picture] played-480f-f180: REFUSED — 87.76% of the product picture is a single colour ...
[picture] played-480f-f240: REFUSED — not at the same guest state: dragon_cutscene (+4: native 03 console 02)
```

So the comparable gameplay window is currently `settled_play` plus 120 frames of the scripted route,
ended by a flash and then by the camera/cutscene-tick residual of issues 0110/0114 — not by
anything this instrument can fix.
