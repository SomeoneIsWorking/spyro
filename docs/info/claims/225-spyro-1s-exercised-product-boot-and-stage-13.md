---
id: C225
kind: claim
status: holds
created: 2026-08-27
tags: frame,vsync,runtime,widescreen
depends: titles/spyro1/core/spyro1_frame_driver.cpp, titles/spyro1/core/spyro1_field_scheduler.cpp
---

## Claim

Spyro 1's exercised product boot and stage-13 native route advances only through the title
FrameDriver and framework FramePresenter: guest VSync is a fatal trap, boot/update host turns do not
become competing display owners, and each delegated host step advances exactly one presentation
fence.

## Evidence

Real SCUS_942.28, Clang build `scratch/build/agent-spyro`, psxport `784e5212-dirty`.
`scratch/logs/agent-spyro-wide-capture-600.stdout.log` exited 0 at
`PSXPORT_NATIVE_FRAMES=800`, reached native stage 13, printed `frame-loop contract SATISFIED`, and
contained neither `GUEST VSYNC VIOLATION` nor a FrameDriver fence violation. The same run announced
`aspect=1`, `wide_engine=1`, `native_width=512`, `render_width=684`; its three native producer rows
attributed 262,789 primitives. The present-600 capture is a real 960x720 image with 69.7% non-black
pixels and 3,022 colors. The dedicated Clang tree passed 34/34 CTests after building all test
executables.

## What would falsify it

Any capped native Spyro 1 run reaches the VSync trap, advances the presentation fence other than once per FrameDriver step, fails before the stage-13 product boundary, reports wide_engine=0 under aspect=1, or produces a flat/missing present-stage capture.
