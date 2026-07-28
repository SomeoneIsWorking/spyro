---
id: C099
kind: claim
status: holds
created: 2026-07-29
tags: gpu,framework
---

## Claim

Spyro's SCE and Universal logo screens are BLACK on screen — psxport's renderer deliberately clears to black and composites only submitted PRIMITIVES, discarding the uploaded PSX VRAM backdrop, and those screens are uploads with zero primitives.

## Evidence

gpu_vk.cpp render_geom draws three bands; band 1 (2D_BG) passes clearColorBlack=true unconditionally, with the comment 'composite native submits over BLACK, not over the uploaded PSX VRAM — the PC renderer shows ONLY what a native producer submitted; anything else is black'. Bands 2 and 3 then LOAD. So upload_vram's copy of CPU VRAM into the texture is overwritten every present before anything is drawn. Matches the measurements exactly: at presents 25-425 (the logo screens, identified from the GPU_DUMP frame series) the VK readback shows both display buffers at 1 distinct colour, while the GPU_DUMP frames — which read CPU s_vram BEFORE the wipe — plainly contain the artwork. From present 450 the VK readback holds real frames because the guest is submitting primitives by then. This is DELIBERATE framework design for a consumer whose native producer owns rendering; Spyro is not there yet and still relies on the guest's own uploads for these screens.

## What would falsify it

if a windowed run shows the logo screens correctly, the present path must composite the uploaded VRAM somewhere the headless readback does not see, and this is a headless-only artefact
