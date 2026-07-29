---
id: 35
title: Level geometry is submitted but the captured frame is a flat fill
status: open
symptom: Past the frame-4531 stall the guest submits ~1900 prims / ~13900 GP0 words per frame for long stretches, but every REPL 'shot' capture is a single uniform colour: sky-blue at f20000, tan at f50000, dark maroon at f46404 (a frame in a ~1900-prim run). No geometry is visible in any capture.
tags: gpu,geometry,vk,blocker
created: 2026-07-29
updated: 2026-07-29
---

EVIDENCE THAT THE CAPTURE PATH IS NOT SIMPLY DEAD: the colour CHANGES between captures (blue / tan / maroon) and the display region alternates 512x240 @ (0,0) and @ (0,240) as double-buffering flips. A dead readback would give one constant image. So the instrument responds — but 'responds' is not 'correct'.

THE INSTRUMENT IS ITSELF A SUSPECT AND MUST BE SETTLED FIRST. gpu_native_shot takes a DIFFERENT path under vk_path(): it calls gpu_vk_shot_region to read back the GPU image, whereas the upload-based screens verified earlier this session (the Universal logo at f300, C105) went through the s_vram PPM path. gpu_vk_shot_region has never been validated on this port. A flat fill is exactly what a readback of the wrong image / wrong subresource looks like, and it is also exactly what a real rendering failure looks like. These are indistinguishable from the capture alone.

TWO HYPOTHESES, both untested:
  1. The geometry genuinely is not being rasterised (transform, depth, or ordering-table fault), and the flat colour is the clear.
  2. gpu_vk_shot_region reads back something other than what present() samples, so the picture is fine and the instrument is lying.

HOW TO TELL THEM APART, cheapest first: run the port WINDOWED (PSXPORT_VK_WINDOW / PSXPORT_WINDOWED) and look at the screen with a human eye — the user is observing the running system and that is ground truth here. If the window shows geometry, hypothesis 2 is confirmed and the fix is in the shot path, not the renderer. Only if the window is also flat does the renderer become the subject.

DO NOT start debugging the renderer before that check. This project has already been bitten by reasoning from an unvalidated instrument (I008: PSXPORT_GPU_DUMP reads s_vram, which the VK path never touches, so it reads as permanently black the moment real geometry starts — which is the same trap one layer down).

CONTEXT: reachable only since issue 0034 was fixed; before that the port never got past frame 4531.
