---
id: 35
title: Level geometry is submitted but the captured frame is a flat fill
status: resolved
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

### Note (2026-07-29)
CORRECTED — THE RENDERER IS FINE. The full-VRAM dump at f46501 (REPL 'vram') shows a COMPLETE Spyro attract-demo frame at VRAM (0,0)-(512,240): sunset sky, canyon terrain, Spyro, two characters, and the 'DEMO MODE' caption. So the port renders the game. Hypothesis 2 was the right one and I was one command away from debugging a renderer that works.

WHAT THE FLAT FILL ACTUALLY WAS, in two parts, and I had BOTH wrong at first:

1. EVERY CAPTURE LANDED ON AN EMPTY FRAME. The port alternates: EVEN frames submit 0 prims, ODD frames ~1600 (double-buffered draw). f46400/46402/46404/20000/50000 are all even. 'shot' on an even frame is a picture of nothing. Any frame-targeted capture in this port must pick an ODD frame — check the '[gpu] frame N: P prims' line first.

2. THE DISPLAY REGION POINTS AT THE WRONG BUFFER. Even on odd frame 46501 the shot was still flat, because gpu_native_shot captures [s_disp_x, s_disp_y] and that read (0,240) while the finished frame sat at (0,0). The (0,240) block in the VRAM dump is uniformly (40,0,24) — a FRESH CLEAR, not the previous frame. So the guest cleared the buffer that s_disp says is being displayed: the display region is one flip out of step with the draw region.

That second point is a REAL defect and is not confined to the shot: present_window() uses the same blit_src(s_vram, s_disp_x, s_disp_y), so a windowed run should show the same empty buffer. Not yet confirmed by eye — CONFIRM BEFORE FIXING, because this issue has already cost one wrong conclusion.

MEASURED, so it does not have to be re-derived:
  * prims_f46501.csv: 1609 polys, bboxes sane (median bbox area 112 px, no full-screen prims, x -177..769, y 223..730), 416 textured, 47 semi.
  * ALL 1609 classify is3d=0 — projprim.lookupPz never resolves a vertex for this game, so no world geometry gets depth ordering. Worth its own issue; it did NOT cause the blank capture, and saying so is the point.
  * The captured 512x240 held exactly TWO colours: (40,0,24) x114688 and black x8192 (a 16-row band).

INSTRUMENT NOTE. PSXPORT_PRIMDUMP=<frame> writes scratch/logs/prims_f<N>.csv from the gp0 tee and is the right tool for 'what did the guest actually submit'. It produced NO file at f300 — correct, because the logo screens are uploads with no polygons. An empty result from it means 'no polys that frame', which is indistinguishable from 'not armed' unless you check the frame's prim count first.

### Resolution (2026-07-29)
NOT A RENDERING BUG AT ALL — and not the buffer-flip bug I claimed either. Both of my explanations were wrong; the measurement is in I032.

VRAM dumps at f46501/46503/46505/46507 with the display origin logged per frame show the rendered image sitting in EXACTLY the buffer the log says is displayed, four times out of four. So the flip is correct and present_window() is fine. What actually happened is a capture-procedure fault with two parts:

  1. Every screenshot landed on an EVEN frame, and this port submits 0 prims on even frames (~1600 on odd) because it is double buffered. A capture on an even frame is a picture of nothing.
  2. REPL 'run N' returns AFTER frame N presented and after the display origin has flipped to the next frame's draw target, so a 'shot' at that prompt captures a fresh clear.

Neither is an engine defect. Recorded as I032 with the rules that make capture reliable: use 'vram <path>' (both buffers in one image, nothing to trust) and pick an odd frame.

WHAT THE PORT ACTUALLY DOES: renders Spyro's attract-mode demo in 3D — terrain, sunset sky, Spyro, two characters, 'DEMO MODE' caption (C124, scratch/screenshots/spyro_gameplay.png).

CARRIED FORWARD as its own work, since it is real and this issue is not the place for it: all 1609 polys at f46501 classify is3d=0, because projprim.lookupPz never resolves a vertex for this game. No world geometry gets depth ordering, so occlusion is currently draw-order only. That is the native-depth work.
