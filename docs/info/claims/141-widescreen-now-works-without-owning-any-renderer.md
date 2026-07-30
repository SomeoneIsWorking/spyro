---
id: C141
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen
---

## Claim

Widescreen now works WITHOUT owning any renderer: all five contributing renderers run interpreted with their right clip bound moved to the wide width and OFX re-centred to nw/2 for the duration of the call. Because every contributor is treated in the same frame, the all-or-nothing constraint of C135 is satisfied by construction rather than by finishing a transcription queue. 4:3 is unaffected — the patch is reverted around each call and the gate still passes 16/16 with zero divergences.

## Evidence

scratch/screenshots/wide_ofx_all.png (all five) vs wide_ofx.png (four, terrain omitted) vs wide_bounds2.png (no OFX). Column-brightness offset search between the no-OFX and all-five captures: sky rows 0-45 best-match +85 px, ground rows 90-150 +77 px, against a designed +86. With the terrain renderer omitted the sky stayed at 0 while the ground moved — the omission reproduced C135's misalignment exactly. tools/gate.sh at 4:3: 16/16 PASS, 0 native/substrate divergences, 160 native bodies verified.

## What would falsify it

any scene where a renderer outside this table of five contributes 3D geometry — its content would stay at the 4:3 centre and the frame would misalign against itself again
