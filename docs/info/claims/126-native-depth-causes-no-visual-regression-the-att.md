---
id: C126
kind: claim
status: holds
created: 2026-07-29
tags: gpu,depth,render
---

## Claim

Native depth causes no visual regression: the attract-demo scene at frame 46501 renders pixel-comparably before and after native depth was enabled.

## Evidence

REPL 'vram' dump at f46501 on the depth-enabled build (scratch/screenshots/vram_depth.png) is visually identical to the pre-depth dump (vram46501.png) — same terrain, sky, characters, DEMO MODE caption. Expected: with the game's own painter order still correct for its own camera, real depth changes nothing until the camera widens or moves. Gate 14/14 on the rebased tree.

## What would falsify it

any later scene where enabling depth changes the image — that would mean the recovered Z disagrees with the game's own sort order, i.e. a wrong depth rather than a missing one
