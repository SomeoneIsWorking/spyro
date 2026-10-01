---
id: 162
title: BootPrefixFrameDriver ran at most one of update/draw per product step, so a draw that suspended at the display wait froze the world
status: resolved
symptom: both titles presented a frozen first frame; world never advanced
tags: spyro2,spyro3,frame-driver
created: 2026-10-01
updated: 2026-10-01
---

## Answer

The main-loop stage ran one retail call per product step and alternated update/draw; a draw that suspended
at its display wait consumed every following step, so update never ran again. Fix (commit a8781a3):
`BootPrefixFrameDriver::stepMainLoop` runs update then draw in a loop of at most `kMaxLoopCallsPerStep = 4`
calls until the next display wait suspends, resuming a suspended call first. Test
`test_main_loop_keeps_running_the_update_after_the_draw_suspends` fails on the old driver
(`gVisitUpdate >= kLoopSteps - 1u`) and passes on the new.

## Result

Spyro 3 plays logo, sign, egg and ninja (non-black 84 to 86 percent); Spyro 2 plays the logo sign.

