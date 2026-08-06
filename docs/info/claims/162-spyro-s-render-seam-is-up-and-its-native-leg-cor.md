---
id: C162
kind: claim
status: holds
created: 2026-08-06
tags: render
depends: game/render/render_frame.cpp#SpyroRenderer::drawFrame
---

## Claim

SPYRO'S RENDER SEAM IS UP, and its native leg correctly ABORTS on the first drawn frame naming the scene. game/render/ (SpyroRenderer::drawFrame) now decides where the picture comes from, once per drawn frame, from the FRAMEWORK's per-Core RenderMode (PSXPORT_RENDER_PSX=1 = the reference OT walk; default = native). The classifier reads the guest's own stage selector [0x800757D8] and, on a 20 s reference boot, distinguishes exactly TWO identities: stage 13 (6007 drawn frames) and stage 0 / FIELD (2898), alternating.

## Evidence

Clean Release dir (scratch/build-rel), headless, PSXPORT_NOPACE=1, vsync.cpp PINNED to HEAD on both sides so the only variable is the render seam. CONTROL (HEAD): presents 12525/13682/12949, rebuild_geom 12088/13245/12512, 3/3 survive 20 s. LEG B (seam, PSXPORT_RENDER_PSX=1): presents 15305/13968/13758, rebuild_geom 14868/13531/13321, 3/3 survive — same range, no fault. LEG A (seam, native default): aborts on the FIRST drawn frame, 3/3, deterministic presents=436 and rebuild_geom=0 (not one geometry frame was ever rebuilt — the built-in negative control that no picture was produced), printing 'stage selector = 13 / SPLIT on [0x80078D78]==3 -> 0x8001E6B8, else 0x8007CEE4 / [0x80078D78] = 0' plus geomValid=1 ofx=256 ofy=120 H=341. Logs scratch/logs/nativerender/{ctl,gate2A,gate2B}_*.log.

## What would falsify it

if a scene ever renders natively without a producer having been written for it, or if the native leg stops aborting on a stage that has no producer, the seam has grown a fallback and this claim is void
