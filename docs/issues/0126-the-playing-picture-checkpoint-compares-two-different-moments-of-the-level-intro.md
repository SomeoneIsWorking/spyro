---
id: 126
title: The `playing` picture checkpoint compares two different moments of the level intro, so its 18.49% ranks nothing
status: open
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
