---
id: C143
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,depth
---

## Claim

psxport's widescreen 2D widen CANNOT be enabled on this port yet, and the previously recorded reason was wrong. It was recorded as an ORDERING problem (2D widened before the 3D projection was re-centred); the projection is now re-centred across every contributing renderer, and enabling the widen still makes the picture worse. Measured: sky, ground AND the screen-space caption each move a further +86 px, so it is not shifting 2D relative to 3D — it shifts the WHOLE FRAME a second time. The real gate is 2D-vs-3D discrimination, which rides on per-primitive depth; at ~2.5% depth coverage almost nothing is classified 3D, so 'widen the 2D' means 'widen everything'.

## Evidence

Enabled the latch (s_prev_had3d/s_prev_had_bg2d no longer rolled on a zero-primitive frame) so the 2D widen fires, captured frame 46501 at 16:9, and measured the per-band shift against the same frame with the widen inactive (scratch/screenshots/wide_2d.png vs wide_ofx_all.png, fixed-denominator offset search): sky rows 0-45 +86 px, ground rows 90-150 +86 px, caption rows 180-210 +86 px. All three bands, by the margin, on top of the OFX re-centring already applied.

## What would falsify it

native depth coverage rising enough that s_seen3d is set by real world prims — then the widen would move only screen-space content and this measurement should show the caption moving while sky and ground do not
