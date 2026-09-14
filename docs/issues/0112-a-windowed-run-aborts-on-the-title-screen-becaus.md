---
id: 112
title: A windowed run aborts on the title screen because the sink waits for a window nobody is showing
status: investigating
symptom: windowed run exits SIGABRT (134) ~17 s in, still on the title screen, with [watchdog] STUCK: no frame presented within the timeout
tags: render,present,sink,watchdog,windowed,crash,abort
created: 2026-09-14
updated: 2026-09-14
---

## Symptom

A windowed run dies before the game can be played:

```
[watchdog] STUCK: no frame presented within the timeout — backtrace:
/home/.../spyro_port(_ZN10GpuVkState18show_present_imageEP20SDL_GPUCommandBuffer+0xd9) [0x4d9f79]
... present <- gpu_vk_present <- GpuState::present_window <- gpu_fps60_present_pass
    <- Fps60::present_vk <- Fps60::present <- FramePresenter::commit <- SpyroRenderer::drawFrame
```

exit 134 (the watchdog's `_exit(134)`). This matches the operator's report that the port "aborts
before it can be played" better than issue 0103 does: 0103's stage-8 producer landed (rewatch below)
and does not imply a title-screen exit.

## Reproduction and denominators

Windowed Wayland run, log `scratch/repro/abort_repro.log`: 1036 presents, then STUCK. Line 93 of that
log records `[gpu_vk] swapchain present mode: MAILBOX`, so the blocking-VSYNC leg documented in
`gpu_vk_present_mode.h` was NOT in play. The stuck frame was resolved against the running artifact:
`addr2line -e build/player/bin/spyro_port 0x4d9f79` -> `gpu_vk.cpp:2478`, inside
`show_present_image`, whose first statement was `SDL_WaitAndAcquireGPUSwapchainTexture`.

## Root cause

The sink WAITED for a swapchain image. A window the compositor is not currently showing receives no
frame callbacks, so that wait never returns: no present, no watchdog progress, abort. With a
full-screen terminal in front of the game window it reproduces every time, which is the most likely
shape of the operator's launch.

Ruled out, each with its own measurement:

- the present mode: MAILBOX (log line above), the leg `gpu_vk_present_mode.h` was written for;
- the reuse/skip path: `REUSE_LAST` skips REBUILDING a frame and never skips presenting one, and the
  watchdog is pinged unconditionally once per present (`gpu_native.cpp`'s `gpu_present_ex` tail), so a
  trip means presents genuinely stopped rather than that a presenter skipped them;
- the watchdog's timeout itself: 3 s of a genuinely parked thread, not a threshold too small for the
  work.

## Fix

psxport `runtime/psx/gpu_present_sink.h` (new, header-only) owns the acquire policy and the idle
accounting: `sink_acquire` uses `SDL_AcquireGPUSwapchainTexture`, whose documented NULL result ("not
an error") means no image is ready, and treats it as an IDLE FIELD — skip the blit, keep executing the
guest, and report only the idle/resume TRANSITIONS with counts on both sides, since the sink is called
once per field and sixty identical lines a second are noise. `GpuVkState::show_present_image` is now
the thin call site and is one line shorter; `gpu_vk.cpp` stays at its frozen 4288-line cap.
`tests/test_present_sink_idle.cpp` pins the latch semantics, including the negative case that a steady
state announces nothing.

## Remaining before this can be called fixed

The windowed path needs a display an agent may open; the branch is exercised headlessly (a null window
already takes it) and by the unit test, but the actual "occluded window keeps running" behaviour has
not been observed end to end. The operator re-running `./run.sh` is the cheapest confirmation, and a
terminal tail settles it immediately if the abort survives.
