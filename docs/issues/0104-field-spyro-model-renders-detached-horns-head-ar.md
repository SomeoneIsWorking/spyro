---
id: 104
title: Field Spyro model renders detached: horns/head arc above the body and the wings smear sideways
status: open
symptom: in Artisans gameplay the player model is drawn as a purple torso with its horns/head arc floating ABOVE it and a yellow wing/Sparx smear thrown out to the right; the same model on the stage-13 title screen renders correctly
tags: render,field,actor,player,depth,oracle
created: 2026-09-08
updated: 2026-09-28
---

## Symptom

Capture: `scratch/screenshots/drive-settled.ppm` (Artisans home, 120 frames after GS_Playing),
zoomed in `scratch/screenshots/gems-zoom.png`. Reproduce with:

```
python3 tools/drive.py gameplay --shot scratch/screenshots/drive-settled.ppm
```

The player reads as three disconnected pieces: a purple torso, a brown horn/head arc sitting above
and detached from it, and a yellow spray of wing/Sparx geometry to the right. The operator reports
the same class of problem as "gem colors and depth" when comparing against an emulator oracle.

The DISCRIMINATOR that makes this a FIELD-path defect rather than a model-decode defect: the same
Spyro model on the stage-13 title screen (`scratch/screenshots/p6.png`) renders correctly, with
head, horns and wings attached. So the actor model codec and its material path are fine; something
in the FIELD composition of the player producer `0x80023AC4` (or the depth/OT ordering that places
its sub-parts) is not.

## Ruled out: the per-layer root translation

`0x80023AC4` places three layers. Layer 0 is positioned from the instance position alone; layers 1
and 2 add a per-layer root offset decoded from the animation's root words. A detached head or wing
would show as a layer translation far from layer 0's, so `PSXPORT_DEBUG=pairedroot` now prints all
three camera-space translations and the root words they came from, on the refusing path as well.

Measured in a driven Artisans run (`scratch/logs/pairedroot.log`):

```
layer0_tr=0,0,2546  layer1_tr=0,42,2638  layer2_tr=4,147,2357
root1=0,72,89       root2=4,226,-200     words=0B4003DC/E71FF38F/0B0003DC/E71FF38F
```

The layer translations are small, stable offsets from layer 0 at a view depth of ~2550, and they
track frame to frame. The root decode is therefore NOT the fault; look at the per-layer pose vertex
decode, or at which primitives reference which layer's vertices, instead.

A zoomed capture with the same build is `scratch/shots/spyro-zoom.png`. At 6x it reads as Spyro from
behind with horns, wings and body all present; the specific defect is narrower than "three
disconnected pieces" and needs to be restated against an oracle before it is chased further.

## Not yet determined

Whether the horn arc and the wing smear are one fault (a per-part transform) or two (a transform
plus an OT-bin/depth ordering fault). Nothing here has been isolated to a producer yet — this entry
records the observation and its discriminator, not a cause.

## Related

Gems themselves (the dark-red diamonds left of the player) are present. Their colour and depth
against an independent oracle is the operator's separate report and is not settled by this capture.

## 2026-09-28: re-read against a longer, wider camera comparison, and the "detached" reading is WITHDRAWN

The capture above is 120 frames after `GS_Playing`, and at 6x it reads as Spyro from behind with
horns, wings and body all present. Two things measured since then bear on how this entry is worded,
and neither is a fix to the model:

**The camera was not the same picture, and now is compared.** `tools/oracle_spyro1.py` carries the
camera as a declared range and `picture_decisive` includes it, for the stated reason: at a
dragon-cutscene frame with gamestate, level_id, game_tick, state_switch and player.position all
equal, the product framed the dragon about 25px left and 20px below the reference and **87% of
pixels differed** — while every decisive RAM range agreed. A capture taken at a moment when the
camera's own phase is unverified cannot separate "the model's parts are detached" from "the picture
is framed differently".

**That phase is now measured to a known cause, and it is one CD read.**
[Issue 0114](0114-no-reproducible-route-out-of-artisans-so-level-entry-is-uncompared.md) and
[0110](0110-artisans-camera-checkpoints-are-not-yet-phase-aligned.md) record it: the console spends
6 display fields in one update waiting on a synchronous CD read (`g_CDMaxReadTime = 600`, live at
that one iteration and 0 at all 496 others), `g_DeltaTime` clamps it to 4, and the camera's
`m_Rotation` then differs by about 1.3 degrees of yaw one frame later — with `m_Position`,
`m_DestinationPosition`, `m_State`, `m_OcclusionGroup` and all four `SphericalCoordsOffset` blocks
byte-identical. The product performs that read at zero guest-time cost, so it does not have the
divergence.

**What this means for 0104, stated narrowly.** The symptom as written ("three disconnected pieces")
is not supported by the evidence in this file, and the entry's own discriminator already said the
actor model codec and its material path are fine because the same model renders correctly on the
stage-13 title. So: the capture remains, the "detached" reading is withdrawn, and the open question
is restated as **whether the FIELD player model's per-part transforms agree with the console's** —
which is a per-part comparison of the pose and the primitive that references each part's vertices,
not a visual impression. The camera phase has to be equal before that comparison means anything, and
it is not equal until the CD-read field cost is fixed.

**Not measured here:** no per-part vertex or primitive comparison was run. Nothing in this entry
should be read as evidence that the field model is now correct, or that it is wrong.
