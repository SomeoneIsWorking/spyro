---
id: I008
kind: instrument
status: trusted
created: 2026-07-28
---

## Instrument

PSXPORT_GPU_DUMP PPM frame dump — BLIND to VK-rendered geometry

## Validated by

DISTRUSTED for the question 'is the game still drawing'. It reads s_vram, the software VRAM array. Under vk_path() (the default — soft_gpu is only set in SBS oracle mode) every polygon and sprite goes to the VK rasterizer and NEVER touches s_vram; only VRAM uploads and fills land there. gpu_native_shot says so outright: 'VK render lives in the GPU image, not s_vram'. Consequence: on a game whose front-end is uploads and whose gameplay is geometry, this dump shows the front-end and then goes PERMANENTLY BLACK the moment real rendering begins. I read exactly that as 'the game stopped drawing' and built a claim and a gate check on it. The correct signal is the guest's own prim-submission count: 680 frames in the last quarter of the run submit prims. VALID for: uploads, fills, and display-region geometry. INVALID for: anything rasterised.

## Known failure modes

(none recorded yet)
