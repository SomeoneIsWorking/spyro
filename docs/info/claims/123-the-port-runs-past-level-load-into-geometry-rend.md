---
id: C123
kind: claim
status: holds
created: 2026-07-29
tags: recomp,stall,level-load
---

## Claim

The port runs past level load into geometry rendering: 69360 frames in 60s, with 6632 of the last quarter's frames submitting prims. The frame-4531 stall (issue 0034) was a recompiler bug — jalr misclassified as a block terminator alongside jr — not a game-state or overlay problem.

## Evidence

60s headless run after the emit.py fix: last frame 69360 (was a hard stop at 4531). tools/gate.sh 60 -> 14/14 PASS: frames 49985, distinct occupancies 11, frames submitting prims in last 25% = 6632, 33.7 MB from disc, 7 overlays identified, recomp misses 0, native/substrate divergences 0 over 160 verified calls. Root cause proven by TDD: two tests in psxport tools/recomp/test_emit.py fail on the pre-fix emitter with s0=0x77 (the clobbered value) and pass after.

## What would falsify it

a run that stops presenting at a fixed frame again, or 'frames submitting prims (last 25%)' dropping to 0 — either means the port is back to a front-end-only boot and the geometry claim was measuring something else
