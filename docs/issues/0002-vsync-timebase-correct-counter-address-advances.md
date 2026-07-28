---
id: 2
title: VSync timebase: correct counter address advances the boot, then SIGSEGV
status: resolved
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

## RESOLVED — and my inference in the section above was WRONG

The note above argued the fault was "in guest code newly reached, NOT in the present path,"
reasoning that only one vsync line was logged with 0 frames advanced. **That was backwards.**
The debug line prints *after* the wait loop, so a crash *inside* the loop means it never
printed at all. Absence of the log was evidence *for* the present path, not against it.

A real backtrace (gdb, not the watchdog's truncated 2 frames) showed it immediately:

    vblank_wait (vsync.cpp) -> gpu_present -> present_window -> blit_src
      -> gpu_vk_present -> GpuVkState::present (gpu_vk.cpp:833) -> call to 0x0

**Root cause:** `gpu_vk.cpp` called `hooks->renderFadeState(...)` unguarded in both present
paths. `game_iface.h` documents GameHooks members as tolerated-null and other call sites
guard; these two did not. Spyro's Phase-0 hook table is almost entirely null, so its first
present jumped to address 0. Invisible with one consumer — Tomba!2 always supplies the hook.

**Fixed upstream** in psxport (`gpu_vk: guard renderFadeState`): default to a zeroed
FadeState — mode 0, rgb 0, exactly the state a game with no fade subsystem is in — and only
call the hook when present.

**Verified:** Spyro presents without crashing and the vblank counter advances:
`target=7 -> counter=7 (+1 frames)`, `target=8 -> counter=8 (+1 frames)`.

**Lesson (companion to C006):** don't infer a crash site from which log lines are missing.
A line that prints after the suspect region is silent whether the region crashed or never ran.
Get a real backtrace.

### Resolution (2026-07-28)
Root cause: gpu_vk.cpp called hooks->renderFadeState unguarded in both present paths; Spyro's Phase-0 hook table is null there, so the first present jumped to 0x0. Fixed upstream in psxport (default to a zeroed FadeState, call the hook only when present). Verified: Spyro presents without crashing and the vblank counter advances (+1 frame per wait). Also corrected a WRONG inference in the original note - I had argued the fault was in guest code, reasoning from a missing log line, but that line prints after the wait loop so its absence was evidence FOR the present path.
