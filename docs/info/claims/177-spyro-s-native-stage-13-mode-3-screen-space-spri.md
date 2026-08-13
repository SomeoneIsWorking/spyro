---
id: C177
kind: claim
status: falsified
created: 2026-08-13
tags: render,native-producer,sprite-queue
depends: game/render/fx_sprite_queue.cpp#stage13Mode3Render
falsified_on: 2026-08-13
---

## Claim

Spyro's native stage-13/mode-3 screen-space sprite-queue layer reproduces the reference RasterizeSpritePrimQueue output for the live flat bit01 class, while the separate paired-actor pass 0x80023AC4 remains unowned and blocks shipping that stage.

## Evidence

scratch/logs/spriteq-native-after-framehead.log: 244 drawing frames, emitted up to 214 per frame, rejected_world=0 and rejected_variant=0, no unmapped write after full 0x8001ED5C frame-head restoration. Native/reference present captures 3100,3200,3300,3400 with reference 0x80023AC4 muted: ImageMagick masked RMSE is 0 on every non-black native pixel in the PRESS START layer. Captures at 3000/3500 are excluded because the paired actor pass visibly overlaps the comparison mask. Native run then aborts loudly at stage 0 rather than falling back.

## What would falsify it

Any same-state isolated reference comparison differs on a screen-queue pixel; stage-13 mode 3 presents without a diagnostic override before 0x80023AC4 is owned; or a reachable screen-space record uses a primitive variant this producer rejects.

## FALSIFIED 2026-08-13

The capped runtime producer DB proves 0x80022A2C had not fired by present 3500, so the purported 3100-3400 pixel comparisons measured the earlier titlefx producer, not the new screen queue. The implementation's 244 emitting calls remain observed, but the cited visual-fidelity evidence does not test it.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
