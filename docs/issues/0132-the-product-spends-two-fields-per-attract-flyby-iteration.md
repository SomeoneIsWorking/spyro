---
id: 132
title: "The product spends two fields per attract-flyby iteration where the reference spends one, so the flyby is presented at half speed"
status: open
symptom: driving with no pad input reaches the attract demo's flyby on both cores; the guest runs the same 383 iterations on each, but the product delivers 2.00 fields per iteration (768 fields) and the reference 1.00 (385 fields), so the flyby is presented over twice the wall clock it should be
state_items: S011
tags: timing,pacing,frame-loop,attract-demo,presentation
created: 2026-09-26
updated: 2026-09-26
---

## The measurement

A per-iteration census of both cores on the no-input attract route
(`tools/oracle_spyro1_demo.py`'s route; probes under `scratch/oracle/`). The guest's own flyby clock,
`g_TitlescreenState.m_Tick`, runs 1 -> 384 on BOTH cores, so both run the same 383 iterations.

What differs is the fields each core spends per iteration:

| | fields per flyby iteration | total fields |
|---|---:|---:|
| product | **2.00** | 768 |
| reference | **1.00** | 385 |

So the flyby is presented over 768 fields where the console presents it over 385 — the product's
attract flyby runs at half speed, and a player who leaves the title screen alone watches it for twice
as long as the retail card.

## Why the product spends two

`Spyro1FrameDriver::stepFrame` (`titles/spyro1/core/spyro1_frame_driver.cpp`) delivers
`kFieldsPerLogicFrame` = 2 fields per logic frame, and then requires the frame to end on a host field
boundary (`finishLogicFrame`), aborting if it does not. That two-field cadence is what makes the
Artisans route agree with the reference for 477 consecutive game frames, because in gameplay the
guest's own draw waits two fields per iteration and the two numbers coincide.

In the flyby they do not. The guest's iteration issues no field-consuming wait, so the reference retires
two iterations per field while the product retires one, and the product's fixed cadence stretches the
phase by exactly the factor of two. The same reasoning applies to any guest phase that does not wait:
the product cannot express "this iteration cost no field", because its frame IS a field pair.

## WHAT AN EARLIER VERSION OF THIS ISSUE GOT WRONG, and how it was found

This issue has been wrong twice, and both corrections are recorded because the errors are the
instructive part.

**First claim (withdrawn):** that the reference retires the flyby "inside one main-loop iteration" and
that the product therefore "cannot run a guest spin phase". That came from reading the comparator's
GAME-FRAME counts, which are not fields: inside the flyby the reference's game-frame barrier cannot
resolve, because `g_UnprocessedFrames` is assigned 2 by `PadDemoUpdate` (gamepad.c:214) and read-and-
zeroed in the same main-loop iteration (main.c:24-32) while `PadVSync` returns before incrementing it
when `g_DemoMode` is set (gamepad.c:236-238). The barrier's "the counter decreased" test then never
fires and one "game frame" spans the whole phase.

**Second claim (withdrawn):** that the two cores spend the SAME fields per phase (768 against 777).
Those numbers were `g_LevelTicks` deltas, and the level load zeroes that counter (loaders.c:400), so
the deltas straddled the load and measured nothing about the flyby. The census above counts fields
per iteration directly and does not depend on a counter the load resets.

The conclusion survived both corrections, but for a different reason and with a different reference
number than either earlier version stated.

## What this costs, and what it does not

- It costs presentation time in a draw-suppressed guest phase. The attract flyby is the measured
  instance; the same shape would apply to any other phase whose iteration does not wait.
- It does NOT affect guest state: the flyby's own clock and the demo's simulation match the reference
  (issue 0133's route holds 553 consecutive per-iteration comparisons, and the level entry matches on
  every decisive range). This is a pacing divergence, not a correctness one.
- It does NOT affect the Artisans parity, which is why it went unnoticed: in gameplay the two numbers
  coincide, so the fixed cadence is correct exactly where all the evidence was gathered.

## What would fix it

Let a guest iteration that spends no field retire without a host field, the way the reference's loop
does — i.e. the field scheduler must be able to run update iterations back to back inside one host
step when the guest asks for no wait. That is a change to the pacing model, and the constraint is
measured: the Artisans route's 477-frame agreement depends on the two-field cadence in gameplay, so
the change must leave that leg untouched and may only alter phases where the guest spends no field.
Verifying it means the Artisans per-frame oracle comparison still at 0 divergences AND this route's
census reading 1.00 fields per iteration.

It is NOT a frame-count tweak to make a number match, and it is NOT a change to the flyby.
