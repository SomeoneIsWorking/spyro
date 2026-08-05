---
id: 45
title: spyro headless renders only ~3 distinct pictures then stalls — blocks the flicker investigation
status: open
tags: render,headless,flicker,user-reported
created: 2026-08-05
updated: 2026-08-05
---

USER-REPORTED (2026-08-05): 'spyro has flickering graphics' in a WINDOWED run. Chasing it headless produced a different, more basic finding, recorded because it BLOCKS the flicker investigation.

MEASURED, headless, no input, PSXPORT_GPU_DUMP every frame: 3545 frames dumped; across 141 widely-spaced consecutive pairs only 3 differ. A contiguous 40-frame window deep in the run is BIT-IDENTICAL frame to frame (0.00% pixels changed, nonblack steady at 93.33%). The run then dies on the watchdog 3s frame-progress timeout (signal 06 = SIGABRT).

MEASURED with the USER'S OWN INPUT replayed — their windowed session pad, auto-captured to scratch/bin/pad_session.pad at 12:21 and preserved as replays/bugs/flicker-session.pad before a later run could overwrite it: 42264 frames, still only 3 of 211 widely-spaced pairs differ. Real input does not make the picture advance.

THE INSTRUMENT IS VALIDATED, so 'frozen' is an observation and not a broken dump. Positive control, both classes: the same comparison DOES report change where change exists — 3 pairs differ, the first at f00025 with 40.48% of pixels changed. And the dump is not reading a fixed corner of VRAM: it follows the display origin (gpu_native.cpp:1393 dumps at s_disp_x/s_disp_y, written by GP1(05) at line 1240), so a double-buffer flip would appear as ALTERNATING content, not as stillness.

WHAT IT MEANS FOR THE FLICKER REPORT: the user sees flicker in a WINDOW, so windowed the game advances and headless it does not. That is a headless/windowed behavioural divergence, which this project bans outright ('Headless and windowed should never be different code paths', USER). The divergence must be closed BEFORE the flicker can be reproduced headless — otherwise every headless measurement is of a different program from the one the user is looking at.

NOT ESTABLISHED, deliberately not guessed: whether the stall is the game waiting on something only the windowed path supplies (an event, a pad/vblank cadence, the present itself) or a genuine hang. vsync.cpp vblank_wait is this port's per-frame boundary and does present + pace + audio + event delivery there; windowed and headless differ inside gpu_present, which is where to look first.

NEXT: instrument vblank_wait to log per-frame progress (vblank count + whether the guest submitted any GP0 that frame) and compare a windowed run against a headless one at the same frame index. Do NOT chase the flicker until headless advances like windowed.
