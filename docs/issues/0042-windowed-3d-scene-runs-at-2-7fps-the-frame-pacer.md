---
id: 42
title: Windowed 3D scene runs at ~2.7fps: the frame pacer read a Tomba2 scratchpad byte as its quota
status: resolved
symptom: windowed 3D scene ~2.7fps; blocked (CPU idle) ~99% of run time
tags: perf,pacing,vsync,windowed,fixed
created: 2026-08-03
updated: 2026-08-03
---

ROOT CAUSE: gpu_pace_subframe (external/psxport/runtime/recomp/gpu_native.cpp) read the scratchpad byte 0x1F800235 as its per-call vblank quota. That address is the FIRST consumer's (Tomba!2) engine field ('vblanks per displayed frame'); in Spyro it is ordinary scratchpad working memory — gen_func_8001F798 (EmitActorDrawList) mem_w32s vertex data over it. Proven with a gdb store-watchpoint on the byte during a live windowed run (writer = mem_w32 from gen_func_8001F798, values 7/10/26/29/38 at different moments). Each vblank_wait iteration then slept byte*1000/60 ms (116ms-633ms) instead of 16.67ms, so the windowed run dropped to ~2-2.7fps in any scene that drew geometry.

Headless was never affected (pacer is a no-op without a window): the same run does ~1000fps headless, and the windowed CPU profile had only 486 samples in 55s (99.5% blocked) — the smoking gun that this was a sleep, not CPU work.

FIX: GameConfig::paceQuota (appended at end, positional-init safe) now carries the per-call quota; Spyro sets 1 (vsync.cpp paces once per vblank; 1 vblank = 1/60s). The legacy byte-read stays as the 0 fallback so the reference consumer is untouched. Windowed: 2673 frames in 45s (~59.4 vblanks/s = the true 60Hz timebase; display = 30fps per C072's 2-vblank flip). Gate 16/16. NOTE: this also halves the boot's previous per-vblank sleep, so the whole game now runs at real-time instead of the old half-speed 30 vblanks/s; earlier '30fps' claims were display-vs-vblank ratios, not wall clock.
