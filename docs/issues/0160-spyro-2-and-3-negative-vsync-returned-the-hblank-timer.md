---
id: 160
title: A negative VSync(-1) answered with the 16-bit HBlank root counter, but retail returns the guest vblank word, so the Spyro 2 frame limiter never converged
status: resolved
symptom: Spyro 2 draw stuck at 0x80015944 for thousands of steps; the same class of stall in Spyro 3 once the world ran
tags: spyro2,spyro3,vsync,pacing
created: 2026-10-01
updated: 2026-10-01
---

## Answer

Retail libetc VSync on the negative path returns a guest word, not a hardware register:
Spyro 2 `0x80058F34 bgez $a0` not taken, `0x80058F3C lui $v0,0x8006 ; 0x80058F40 lw $v0,0x6618($v0)` returns
`[0x80066618]`, incremented by the vblank callback at `0x8005ACB0 addiu ; 0x8005ACB8 sw` (zeroed at
`0x8005AC58`). Spyro 3: `0x800595CC lui $v0,0x8007 ; 0x800595D0 lw $v0,-0x4b80($v0)` returns `[0x8006B480]`.
The earlier comments read the blocking-wait pointer pair (GPUSTAT and `0x1F801110`) as the query source.

The Spyro 2 frame limiter (draw `0x80015900..0x80015990`) compares `VSync(-1)` against its saved last value
at `[0x80066F9C]` and waits for a difference of two. The framework answered with the wrapping HBlank timer,
so after the timer wrapped `last` (`0xFFC8`) lay in the future and `now - last < 2` held for thousands of steps.

Fix: `kVSyncQueryCounter` is `0x80066618` (Spyro 2) and `0x8006B480` (Spyro 3), through the existing
`PlatformHlePlan::vsyncQueryCounterAddress`. No framework change was needed.

## Result

Spyro 2 passes the limiter (resume PC no longer parked at `0x80015944`); it plays logo, sign and the
scene. See 0161 for the next stall.

