---
id: 66
title: wide_clip silently displaced the native world body: registration order, not the flag, decided who owned 0x800258F0
status: resolved
symptom: PSXPORT_NATIVE_WORLD=1 logs 'the native world body OWNS 0x800258F0' and the run exits 0, but the differential reports ZERO calls for that site — while PSXPORT_WORLD_CENSUS=1 on the identical recipe counts 3587 calls at the same address
tags: render,ownership,override,widescreen,gotcha
created: 2026-08-19
updated: 2026-08-19
---

ROOT CAUSE. psxport keeps ONE override slot per guest address, so the last registration wins. `spyro_register_native_world()` runs from game_hooks.cpp before `spyro_register_wide_clip()`, and wide_clip installed `hook<4>` on 0x800258F0 (kRenderers[4]) UNCONDITIONALLY — so it overwrote the native body every time. Nothing failed: the native registration logged success, wide_clip logged success, the run exited 0, and the body simply never executed.

WHY IT WAS CAUGHT. Only because the native body was checked for having RUN rather than for having been INSTALLED. The discriminator was the census (C199): the same env with PSXPORT_WORLD_CENSUS=1 counted 3587 calls at 0x800258F0, which separates 'my run recipe never reaches the field' from 'my override is not installed'. Without that control the obvious reading — 8000 frames of attract demo never reach the world renderer — was entirely plausible and entirely wrong.

FIX. Both sides test the same flag, which is the arrangement native_terrain.cpp already uses for 0x8004EBA8: wide_clip.cpp skips kRenderers[4] when PSXPORT_NATIVE_WORLD is on, and native_world.cpp installs only when it is on. native_render.cpp's PSXPORT_WORLD_CENSUS arm now REFUSES to install when PSXPORT_NATIVE_WORLD is also set, instead of winning by registering last and then reporting on the guest body while the log claims the native one is installed.

THE GENERAL SHAPE, since this is the third time this slot has bitten (fntrace, terrain, world): a registration whose correctness depends on the ORDER of the calls around it is a latent bug even while its behaviour is right. Test the flag on both sides; never let call order do the deciding.
