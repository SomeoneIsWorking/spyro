---
id: C098
kind: claim
status: falsified
created: 2026-07-29
tags: gpu,instrument
falsified_on: 2026-07-29
---

## Claim

The two VRAM observers hold DIFFERENT content: upload-only screens (the logos) appear in CPU-side s_vram but are absent from the GPU texture the VK path reads and presents.

## Evidence

VK readback (REPL shotregion) at guest frames 150 and 300 — the SCE/Universal logo screens — reports BOTH display buffers at 0.0% non-black, 1 distinct colour. At frame 500 one buffer holds a real frame (93.2%, 2392 colours). Meanwhile PSXPORT_GPU_DUMP frames from the same period DO contain the logo artwork (f00250: 36.1% non-black, 14357 colours) — legible text, just mis-decoded. I008 already recorded the converse: GPU_DUMP reads CPU s_vram and never sees VK-rasterised polygons. So neither observer sees the whole picture, and the split is by PATH: uploads land in one, 3D rasterisation in the other. Note an empty VK read is NOT the depth bug — 24bpp data misread as 15bpp would appear as scramble, not as a single colour.

## What would falsify it

if the present path uploads CPU s_vram into the GPU texture at present time, the logos WOULD reach the screen despite being absent from the readback — capture a windowed present, or check whether present composites both, before concluding the logos are invisible in-game

## FALSIFIED 2026-07-29

PARTLY WRONG — the mechanism I assumed was missing exists. gpu_vk.cpp's present path calls upload_vram() (line 867, 'CPU VRAM -> THIS Game's VRAM image (2D backdrop)'), copying the whole CPU s_vram into the GPU texture every present. So upload-only content is NOT structurally absent from the texture the readback sees, and 'the two observers hold different content by path' is not established. What remains true and measured: the VK readback reported both buffers empty at REPL run-counts 150 and 300, while GPU_DUMP frames from early in a run contain the logo artwork. The likely explanation is a MISMATCH BETWEEN THE TWO FRAME AXES — my REPL 'run N' counts vblank-wait iterations while the GPU_DUMP index counts presents, and I treated them as interchangeable without checking. Re-establish the logo screens' actual present index before drawing any conclusion about where their pixels live.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
