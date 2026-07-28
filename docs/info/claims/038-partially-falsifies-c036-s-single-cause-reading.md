---
id: C038
kind: claim
status: falsified
created: 2026-07-28
tags: gpu
falsified_on: 2026-07-28
---

## Claim

PARTIALLY FALSIFIES C036's single-cause reading: draining the queue fixed the overflow but NOT the black screen

## Evidence

I predicted the missing flush explained both symptoms. It explains one. After adding the flush the overflow abort is gone, but the frame dump is unchanged where it matters: 434 frames with content, last content still at frame 434, and every sampled frame from 500 to 3900 is 0.00% occupancy. So prims now reach the renderer without reaching the SCREEN. Untested hypothesis worth checking first: the framework's native path pairs its draw with gpu_set_disp_origin(c,0,0) so 'present scans the page we draw', while Spyro's guest double-buffers (disp alternating (0,0)/(0,240)) — the engine batch may be drawn to a target the present never scans.

## What would falsify it

Content appearing after frame 434 without any display-origin change, which would mean the black screen had a different cause entirely.

## FALSIFIED 2026-07-28

Not supported by evidence — the measurement could not see the answer. I concluded 'prims reach the renderer but not the screen' from a PPM dump that is structurally incapable of showing rasterised output: PSXPORT_GPU_DUMP reads s_vram, and VK-path polygons never touch s_vram (instrument I008). The correct signal contradicts it: 680 frames in the last quarter of the run are submitting prims, so the guest is drawing. Whether those pixels are CORRECT is still unknown and now genuinely unmeasured — there is no headless way to capture VK output (the readback is REPL-only, and adding a per-frame one hung the port at frame 0, see issue 0018). C036's remaining live question was never 'why is the screen black' but 'why does the port still exit early', which is now the recomp miss at 0x8008772C (issue 0017).

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
