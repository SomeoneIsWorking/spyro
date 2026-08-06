---
id: 48
title: Does Spyro have Spider-Man's paceQuota defect? NO — re-derived from Spyro's own call site and measured: quota=1 is correct
status: resolved
symptom: asking whether spyro's windowed frame rate is throttled the way spider1's was (spider1 rendered 15.6fps while presenting 60 because GameConfig::paceQuota was 2 against a one-vblank calling cadence)
tags: pacing,perf,windowed,paceQuota,gpu_pace_frame,vsync,ruled-out,cross-port
created: 2026-08-06
updated: 2026-08-06
---

## Question

spider1 issue #10: `GameConfig::paceQuota` was 2 while both of its pace call sites paced once per
one-field wait, so every wait was served a two-field sleep and the port rendered ~15.6 fps while
presenting 60. Does the same defect exist in Spyro?

## Answer: NO. Spyro's paceQuota=1 is correct. Re-derived here, not copied across.

`paceQuota` is the number of vblanks that ONE `gpu_pace_frame` call represents, by CALLING CADENCE
(`external/psxport/runtime/recomp/game_iface.h:255-271`); the framework sleeps `quota/60 s` per call.

Spyro has exactly ONE `gpu_pace_frame` call in `game/` — `game/core/vsync.cpp` inside
`vblank_wait`'s `while (cur < target)` loop. That same loop body also calls `gpu_present` once and
advances the vblank counter by exactly one (the guest's root handler bumps `[0x800749E0]`, else
`cur++`). So pace calls == presents == vblanks advanced, 1:1:1 BY CONSTRUCTION, and the wait quantum
is one vblank. quota=1 is the only value consistent with that.

STRUCTURAL DIFFERENCE FROM SPIDER-MAN, which is why the same wrong value would have looked different
here: spider1's vblank counter is rederived from REAL ELAPSED TIME (`vblank_advance`), so an inflated
quota lengthened the guest's waits while presents kept coming — rendered fps fell to a quarter of
presents. Spyro's counter is advanced BY the pacing loop itself, so an inflated quota does not
desync anything; it slows the ENTIRE game clock uniformly. Measured below: quota=2 gives a 30 Hz
port, not a 60 Hz port dropping frames.

## Measurement (windowed — headless is never paced)

New game-side instrument `PSXPORT_DEBUG=pace` (see instrument I043), same 32.98 s steady window
(t=10..43 s) in every leg, analyzer `scratch/logs/pace/analyze.py`:

| leg | vbl/s | pace entries/s | presents/s | rendered frames/s (presentskip rebuild_geom) | pace per vblank |
|---|---|---|---|---|---|
| quota=1 (shipped) | 60.00 | 60.00 | 60.00 | 60.00 | 1.0000 |
| quota=2 (NEGATIVE CONTROL, rebuilt) | 30.00 | 30.00 | 30.00 | 25.91 | 1.0000 |
| quota=1, pace channel OFF (control for the instrument itself) | — | — | 60.00 | 60.00 | — |

The negative control is the same instrument in the same mode on a deliberately wrong build, and it
produced the failing answer — so a run that prints 60.00 is a measurement, not a tautology.

## What this does NOT establish

- Nothing about the R3 flicker. `rebuild_geom` is 1979 of 1979 presents in the quota=1 window (the
  vsync loop re-flushes an already-consumed queue every present, so the batch is never empty), so on
  Spyro that counter does NOT discriminate a new rendered frame from a re-emitted one the way it did
  on spider1. Use distinct-colour-count per present for the flicker question.
- The pacer targets exactly 60.000 Hz (`quota * 1000.0/60.0` ms), not NTSC's 59.94. That is a 0.1%
  fast clock owned by the framework's `gpu_pace_subframe`, unrelated to this field's value.
- Runs were made while a sibling agent also had a windowed spyro_port up. Both legs hit their
  THEORETICAL ceiling (60/quota) exactly, so host contention did not bind — a starved host would
  have measured BELOW the ceiling, not at it.

Logs: `scratch/logs/pace/{A_quota1,B_quota2_control,C_quota1_no_pace_instrument}.log`.
