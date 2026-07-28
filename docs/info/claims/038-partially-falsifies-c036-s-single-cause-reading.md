---
id: C038
kind: claim
status: holds
created: 2026-07-28
tags: gpu
---

## Claim

PARTIALLY FALSIFIES C036's single-cause reading: draining the queue fixed the overflow but NOT the black screen

## Evidence

I predicted the missing flush explained both symptoms. It explains one. After adding the flush the overflow abort is gone, but the frame dump is unchanged where it matters: 434 frames with content, last content still at frame 434, and every sampled frame from 500 to 3900 is 0.00% occupancy. So prims now reach the renderer without reaching the SCREEN. Untested hypothesis worth checking first: the framework's native path pairs its draw with gpu_set_disp_origin(c,0,0) so 'present scans the page we draw', while Spyro's guest double-buffers (disp alternating (0,0)/(0,240)) — the engine batch may be drawn to a target the present never scans.

## What would falsify it

Content appearing after frame 434 without any display-origin change, which would mean the black screen had a different cause entirely.
