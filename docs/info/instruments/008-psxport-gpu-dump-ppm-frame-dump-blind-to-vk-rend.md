---
id: I008
kind: instrument
status: DISTRUSTED
created: 2026-07-28
distrusted_on: 2026-08-06
---

## Instrument

PSXPORT_GPU_DUMP PPM frame dump — BLIND to VK-rendered geometry

## Validated by

DISTRUSTED for the question 'is the game still drawing'. It reads s_vram, the software VRAM array. Under vk_path() (the default — soft_gpu is only set in SBS oracle mode) every polygon and sprite goes to the VK rasterizer and NEVER touches s_vram; only VRAM uploads and fills land there. gpu_native_shot says so outright: 'VK render lives in the GPU image, not s_vram'. Consequence: on a game whose front-end is uploads and whose gameplay is geometry, this dump shows the front-end and then goes PERMANENTLY BLACK the moment real rendering begins. I read exactly that as 'the game stopped drawing' and built a claim and a gate check on it. The correct signal is the guest's own prim-submission count: 680 frames in the last quarter of the run submit prims. VALID for: uploads, fills, and display-region geometry. INVALID for: anything rasterised.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-06

FRONTMATTER CORRECTED 2026-08-06 — the body of this entry has said DISTRUSTED since 2026-07-28, but its `status:` field still read `trusted`, so `info.py instrument list` printed "✓ I008  [trusted]" and `info.py brief` ranked it as a trusted tool. A ledger whose headline contradicts its own body is worse than no ledger: the headline is what a hurried session reads.

WHAT IT COST, measured: issue 0045 was filed on 2026-08-05 on the strength of this dump, concluding "spyro headless renders only ~3 distinct pictures then stalls while windowed advances" — a headless/windowed divergence, which this project bans outright — and it stood for a day telling every session that spyro measurement was BLOCKED. It was not. Headless and windowed both run 3545 presents and abort identically (2026-08-06 triage), and the death is issue 0046's rec_dispatch_miss SIGABRT at f3544, not a watchdog timeout. The frozen dumps were this instrument's floor. Issue 0045 is rewritten.

HOW THE FALSE VALIDATION PASSED, because this is the reusable part: 0045's positive control was f00025 with 40.48% of pixels changed — a frame in the UPLOAD-driven front end, the one class this dump CAN see — and the result was then claimed for the geometry phase, which it CANNOT see. A discriminator run against ONE class only. Its other check (the dump follows s_disp_x/s_disp_y, so a buffer flip would alternate rather than freeze) is TRUE and irrelevant: the right region of the wrong buffer is still the wrong buffer.

TO TRUST IT AGAIN it would have to emit its own denominator — how many prims went to the VK rasterizer during the dumped frame — so that "identical to the previous frame" and "0 of 4212 prims are visible to me" stop being the same picture on screen. Until then: VALID for uploads, fills and display-region geometry; INVALID for anything rasterised. Use a present-stage capture (I033) for the picture and the guest's own prim-submission count for liveness.

> Every result this instrument produced is suspect until it is re-validated.
