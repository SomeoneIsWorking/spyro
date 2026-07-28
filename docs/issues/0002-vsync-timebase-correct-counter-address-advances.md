---
id: 2
title: VSync timebase: correct counter address advances the boot, then SIGSEGV
status: open
symptom: With the vblank counter corrected to 0x800749E0, the boot gets further than ever (SDL_GPU device comes up) and then dies with signal 11. Watchdog backtrace is truncated to 2 frames.
tags: vsync,boot,crash
created: 2026-07-28
updated: 2026-07-28
---

## What changed

The libetc vblank wait helper (0x8005DD0C) is now overridden game-side (game/core/vsync.cpp). Its loop condition is `[0x800749E0] < a0` — lui 0x8007 (0x80070000) + lw offset 18912 (0x49E0).

## The bandaid this replaced (worth remembering)

The address was FIRST written as 0x80074C20 — an arithmetic slip. The counter then read 401217493 (garbage), so `cur >= target` was always true, the wait returned immediately, and every `VSync: timeout` disappeared.

That looked exactly like success. It was a no-op wait hiding the problem: the timeouts stopped because the wait stopped waiting, not because the timebase worked. It was caught ONLY because the debug line printed the counter value and `+0 frames` — a handler that logged just 'ok' would have shipped the bandaid.

**Lesson: a diagnostic must print the VALUE it acted on, not that it ran.**

## Current state

Counter now reads 0 (a plausible vblank count). First call is `wait target=0` → no advance needed, correct. Boot proceeds further than any previous run — `[gpu_vk] SDL_GPU device up (driver: vulkan)` now appears, which it never did before — then SIGSEGV.

## Next steps

- Get a usable backtrace (the watchdog's is 2 frames). Try a debug build, or run under gdb/ASan.
- Determine whether the fault is inside gpu_present/gpu_pace_frame called from the wait handler, or in guest code that simply got further than it ever had.
- Note only ONE vsync line was logged before the crash, and it advanced 0 frames — so present() was never called from the handler. That argues the fault is in guest code newly reached, NOT in the present path.
