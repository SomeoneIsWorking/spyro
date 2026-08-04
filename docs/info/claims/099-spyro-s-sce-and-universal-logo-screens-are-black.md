---
id: C099
kind: claim
status: falsified
created: 2026-07-29
tags: gpu,framework
falsified_on: 2026-08-04
---

## Claim

Spyro's SCE and Universal logo screens are BLACK on screen — psxport's renderer deliberately clears to black and composites only submitted PRIMITIVES, discarding the uploaded PSX VRAM backdrop, and those screens are uploads with zero primitives.

## Evidence

gpu_vk.cpp render_geom draws three bands; band 1 (2D_BG) passes clearColorBlack=true unconditionally, with the comment 'composite native submits over BLACK, not over the uploaded PSX VRAM — the PC renderer shows ONLY what a native producer submitted; anything else is black'. Bands 2 and 3 then LOAD. So upload_vram's copy of CPU VRAM into the texture is overwritten every present before anything is drawn. Matches the measurements exactly: at presents 25-425 (the logo screens, identified from the GPU_DUMP frame series) the VK readback shows both display buffers at 1 distinct colour, while the GPU_DUMP frames — which read CPU s_vram BEFORE the wipe — plainly contain the artwork. From present 450 the VK readback holds real frames because the guest is submitting primitives by then. This is DELIBERATE framework design for a consumer whose native producer owns rendering; Spyro is not there yet and still relies on the guest's own uploads for these screens.

## What would falsify it

if a windowed run shows the logo screens correctly, the present path must composite the uploaded VRAM somewhere the headless readback does not see, and this is a headless-only artefact

## FALSIFIED 2026-08-04

SUPERSEDED, and its 'holds' status was actively misleading on 2026-08-04. C099's stated mechanism (band 1's unconditional clearColorBlack discarding the uploaded backdrop) was ALREADY fixed on 2026-07-29 by C104 (render_geom's total==0 early return honours preserveVramBackdrop) plus C105 (24bpp display depth). Both logo screens were verified rendering correctly then. The screens went black again because of a DIFFERENT, later cause: afca817d added an empty-batch early-out to GpuVkState::present() ABOVE upload_vram, so an upload-only screen never reached render_geom at all and the preserveVramBackdrop control could not be consulted. Anyone reading C099 as a live description of the renderer would conclude the fix had never been attempted and would re-derive the whole of issue 0029. See C-new / issue 0043.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
