---
id: 104
title: Field Spyro model renders detached: horns/head arc above the body and the wings smear sideways
status: open
symptom: in Artisans gameplay the player model is drawn as a purple torso with its horns/head arc floating ABOVE it and a yellow wing/Sparx smear thrown out to the right; the same model on the stage-13 title screen renders correctly
tags: render,field,actor,player,depth,oracle
created: 2026-09-08
updated: 2026-09-08
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
