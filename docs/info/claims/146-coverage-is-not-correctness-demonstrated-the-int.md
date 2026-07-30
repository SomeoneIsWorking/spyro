---
id: C146
kind: claim
status: holds
created: 2026-07-30
tags: render,depth,interp
---

## Claim

COVERAGE IS NOT CORRECTNESS, demonstrated: the interpreter depth tap reports 99.8% of vertex lookups resolved while destroying the picture. It attaches a depth to every address a projected value reaches, and most of those are not packet vertices — so once they resolve, the renderer culls on depths that were never real. C144's 13.2% figure is retained as a coverage measurement, but it must not be read as 13.2% of the frame being correctly depth-sorted.

## Evidence

With the depth-cache generation counting pool fills, the interpreter's dynamic mfc2/lw/sw tap took resolved lookups to 99.8% per-vertex and 99.2% per-primitive — and the rendered frame collapsed to near-empty (scratch/screenshots/wide_depth99.png: 48 distinct colours against 1169 for a good frame). The tap is now off by default behind PSXPORT_INTERP_DEPTH=1.

## What would falsify it

a version of the tap that records ONLY addresses which are genuinely packet vertex words — if the picture still collapses then, the fault is in the depth values rather than in which addresses get them
