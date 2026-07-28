---
id: C096
kind: claim
status: holds
created: 2026-07-29
tags: gpu,milestone
---

## Claim

The port renders GAMEPLAY correctly, not just menus: the title screen and an in-level demo frame both come out clean at 512x240 with correct geometry, colours and compositing.

## Evidence

Two captures via REPL shotregion, at guest frames 2500 and 4500. Frame 2500 (0,240): the title screen — SPYRO the Dragon logo, Spyro on his platform, PRESS START, mountains and sky, 93.3% non-black, 3428 colours. Frame 4500 (0,0): an in-level DEMO MODE frame — green terrain, stone cliffs, two gnorc enemies, Sparx, Spyro, clouded sky, 93.3% non-black, 2390 colours. No speckling, no horizontal truncation, no obvious geometry or texture faults in either.

## What would falsify it

these are attract-demo frames. Interactive gameplay, transparency-heavy scenes, or the specific screens of issue 0016 could still show defects; three clean frames is not a rendering audit.
