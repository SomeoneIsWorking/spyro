---
id: C131
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,milestone
---

## Claim

Widescreen now recovers real geometry in Spyro: at 16:9 the port emits 1246 prims/frame against 1171 at 4:3 (+6.4%), from the one owned renderer alone.

## Evidence

Two 110s runs differing only in psxport_settings.ini aspect (0 vs 1), mean prims over prim-bearing frames: 4:3 = 1171 (46223 frames), 16:9 = 1246 (46732 frames). The widening is the owned terrain renderer's clip bounds moving with the wide native width (684 for this game's 512-wide 4:3 frame); at 4:3 the bounds are exactly the guest's, so the body stays byte-identical and ndiff still reports 400/400. Gate 16/16.

## What would falsify it

the increase disappearing (the bounds are no longer being applied) or the picture showing geometry that should have been culled — and note this is ONE renderer of the family: the other 7 clip-bound renderers still reject at 512, so the full widening needs them owned too
