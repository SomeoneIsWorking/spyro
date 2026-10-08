---
id: 173
title: Spyro 2 Record in-betweens open gaps where terrain changes visibility
status: resolved
symptom: thin background-coloured wedges and white/pink slivers in in-between frames
tags: spyro2,record,fps60,terrain
created: 2026-10-08
updated: 2026-10-08
---

## Symptom

Confirmed on the current cadence (psxport b74684e4, guest timing unchanged): in-betweens of a
walking preseq at 4:3 with fps60 on show long thin white/pink slivers across the ground and wedges
at wall edges; the real frames are bit-identical to fps60 off. Shots: in-betweens p0007 (a diagonal
white line, lower left; a white edge on the right wall) and p0010 (a pink band along the bottom
left, white lines at the left wall) in `scratch/record/v5/s2_43f/preseq`, montage
`scratch/record/v5/s2_43f/inbetweens.png`. First seen while the driver briefly presented after the
draw returned (p0002, p0006, p0012, p0014 of `scratch/record/exp/seq43f`).

## Cause

`keyedBlend` moves a keyed primitive halfway to its partner and leaves an unpaired one at the
current frame's position. A terrain packet is unpaired when its polygon record was not drawn by the
same pass in the previous record: it crossed a detail/split/far threshold, or it was culled (edge-on
wall tops and sides flip between frames). Logging every name on that route: per frame 5-13 unpaired
packets in that region, mostly detail-pass records the previous frame did not draw at all, 54 of
them drawn two frames earlier. Its paired neighbours move, it does not, and the background shows
between them.

## Resolution

Pairing was exact, so the gap was the blend, not the keying. Spyro 2's terrain is no longer blended:
the drawer saves its camera and sector visibility, and `guest_terrain::TerrainStateProducer`
redraws the terrain from the drawer at the in-between camera over the union of both fields'
visibility, so a sector that enters or leaves is drawn in both. Evidence at 1x 4:3 with fps60 on:
every composed recordcheck line in gameplay is `mismatched=0` (197 lines), and the in-betweens of a
walking preseq show no slivers or wedges (`scratch/record/v6/final/inbetweens.png`, all 16 frames in
`all16.png`, against `scratch/record/v5/s2_43f/inbetweens.png`). The title screen and intro cutscene
still mismatch: issue 0179.
