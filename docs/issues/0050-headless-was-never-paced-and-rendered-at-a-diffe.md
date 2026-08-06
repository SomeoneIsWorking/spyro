---
id: 50
title: Headless was never paced and rendered at a different internal resolution — every headless timing number described a program the user never runs
status: resolved
symptom: headless and windowed disagree: headless runs at hundreds of fps while a window runs at ~60, and a headless capture is rendered at ires=1 where the same build in its window renders at ires=3
tags: pacing,headless,parity,framework,ires,field-rate,gpu_pace_frame,all-ports
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

Two things differed between a headless run and a windowed run of the SAME build, neither of them
"no window and no audio":

1. **Headless was never paced.** `gpu_native.cpp` `gpu_pace_subframe` opened with
   `if (!gpu_has_window() || cfg_on("PSXPORT_NOPACE")) return;`. Measured on spyro, one build, 60 s
   per leg, headless both times: **3420 presents/59.09 s (57.9 Hz) paced vs 18511/59.31 s (312 Hz)
   unpaced.** Before the fix, headless was ALWAYS the 312 Hz leg.
2. **Headless rendered at a different internal resolution.** `gpu_vk.cpp` derived the AUTO ires scale
   as `round(win_h()/240.0)`, and `win_h()` falls back to 240 when there is no window — so headless
   got 1 where a 960x720 window got 3. `ASPECT_AUTO`'s widened framebuffer had the same input.

## Root cause

**Speed and resolution were being inferred from whether a window existed.** `PSXPORT_NOPACE` was
ALREADY the independent switch for "run unpaced", so `!gpu_has_window()` was pure redundancy that
coupled SPEED to WINDOWING. And the presentation SINK already had a leg-independent definition
(`sink_size()`, headless-default 960x720 = the window's own creation size); the resolution decisions
were simply reading the WINDOW instead of the SINK.

A second, independent defect lived in the same function: the pace interval was
`quota * 1000.0 / 60.0` — **a literal 60.000 Hz** — while the port counting the fields it paces
against uses the real NTSC rate, 60000/1001 = 59.940 Hz (spider1 `sync_native.cpp`
`kFieldRateMilliHz`, `vblank_advance`). Two clocks at different rates across one wait loop: over a
simulated minute the 60.000 Hz pacer lands **60.06 ms — 3.6 whole fields — short** of where the
field counter is.

## Fix

- `runtime/recomp/pace_plan.h` (NEW) — the pacing decision as a pure function that **HAS NO WINDOW
  INPUT**. Leg-independence is structural, not asserted.
- `runtime/recomp/video_plan.h` (NEW) — `video_ires_scale` / `video_wide_native_w`, taking the SINK.
- `runtime/recomp/field_rate.h` (NEW) — ONE definition of the field rate per standard, in milli-hertz.
- The rate is READ from the game: **GP1(0x08) bit 3** (0 = NTSC, 1 = PAL) is now decoded into
  `GpuState::s_disp_pal` and served by `gpu_field_rate_millihz(Core*)`. Writing 59940 at the pacer
  would have been the same bug with a different number.
- `gpu_has_window()` is DELETED — the pace gate was its only caller.

## Consequence you will hit

**A headless run is a REAL-TIME run now.** Every gate/tool whose intent is "as fast as possible"
passes `PSXPORT_NOPACE=1` (swept in the same change: `tools/gate.sh`, `tools/shot.py`,
`tools/depth_cov.py`, `tools/overlay_scan.py`, `tools/whowrites.py`, and the equivalents in
Tomba2Engine). The documented boot gate is now
`PSXPORT_NOWINDOW=1 PSXPORT_NOPACE=1 PSXPORT_WATCHDOG=30 ./run.sh`.

## Evidence, with its negative control

Hermetic: `psxport/tests/test_pace_plan.cpp` (13 cases, 105 checks) and
`tests/test_video_plan.cpp` (11 cases, 63 checks). Each carries a transcription of the rule psxport
shipped at 9890eaa8 and is asserted to FAIL the property against it; compile with
`-DPSXPORT_TEST_LEGACY_PACE_PLAN` / `-DPSXPORT_TEST_LEGACY_VIDEO_PLAN` to watch them go red
(4/11 and 8/10 passing respectively). Framework suite 21/21.

On real data, same headless mode on BOTH sides — this is the negative control for the ires half:
AUTO ires with the default 960x720 sink logs `ires targets (re)built: 3072x1536 (scale=3)`; the same
run with `PSXPORT_PRESENT_SINK=320x240` (exactly the `win_h()` fallback the old code used) builds no
ires target at all, i.e. scale 1 — the failing answer.

## Corrects a previous entry

Issue #42 records "Headless was never affected (pacer is a no-op without a window)". That was true
of the old code and is the defect itself; it is no longer true of the port.

## NOT verified

- **PAL.** All three consuming games are NTSC discs, so the PAL arm of `field_rate_millihz` has never
  executed on real data. 50000 mHz is the documented hardware rate, not a measurement.
- **spider1 and Tomba!2 boot gates.** Sibling agents held those trees (dirty game dirs, running port
  binaries) while this landed, and gating them would have meant injecting an un-landed framework
  change into their in-flight builds. Operator obligation at landing.
- **Whether Spider-Man's user-reported flicker is gone.** The two-clock mechanism it was traced to is
  fixed; whether the user still sees flicker is the user's verdict, not this measurement.
