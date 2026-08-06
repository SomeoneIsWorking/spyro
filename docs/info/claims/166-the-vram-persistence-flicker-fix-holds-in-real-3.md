---
id: C166
kind: claim
status: holds
created: 2026-08-06
tags: render,flicker,persistence,gameplay
depends: external/psxport/runtime/recomp/gpu_vk.cpp, external/psxport/runtime/recomp/vram_dirty.h
---

## Claim

The VRAM-persistence flicker fix HOLDS IN REAL 3D GAMEPLAY and introduces NO ghosting: 0 ghost-candidate pixels across 22 three-way comparisons of 24 consecutive presents in a moving-camera field scene, while the pre-fix build measures up to 93.3% in the SAME window.

## Evidence

Stage 0 (FIELD) attract/demo, Artisans home world, camera and characters moving (present_4602/4611/4622 PNGs). 24 CONSECUTIVE presents 4600..4623 captured into a per-run dir, headless (PSXPORT_NOWINDOW=1 PSXPORT_NOPACE=1 PSXPORT_SPYRO_FRAME_LOOP=1 PSXPORT_RENDER_PSX=1), analysed with tools/present_seq.py --ghost. FIX: 0/24 flat, 70-75% of pixels change on each new drawn frame, 0 ghost-candidate px. NEGATIVE CONTROL on the same tree, ONE line toggled (upload_vram's region argument back to kWholeVram): 15/24 flat (2 colours), 5,524,275 ghost-candidate px (mean 36.3% of frame, 13 of 22 comparisons over 0.1%). non-black% reads 93.33% on BOTH legs and is useless here, as issue 0045 already recorded.

## What would falsify it

a ghosting report from the USER in a scene this run never reaches (cutscene, FMV->gameplay transition, pause screen, level load), or a GP0 VRAM->VRAM copy of a rasterised region — that path still reads CPU VRAM and is untouched by the fix
