---
id: C216
kind: claim
status: holds
created: 2026-08-22
tags: render,world,widescreen,semantic-producer
depends: game/render/render_frame.cpp#renderScene, game/render/world_scene_builder.cpp#build, game/render/scene_painter_order.cpp, game/render/stage13_scene_recipe.cpp
reconfirmed: 2026-08-22 17:12:18
verified_at: 2026-08-22 17:12:18
---

## Claim

Spyro stage-13's static actor, world, and cyclorama picture is produced semantically without guest renderer execution or packet/OT/GTE-output reads, and composes correctly at 16:9 through one authored replay domain.

## Evidence

World final-stream oracle scratch/logs/gate-boot-20260822-153121.log: 1,275 changing calls over 3,000 frames with no packet-observable difference; real Spyro Vulkan selftest: painter draw-area outside=0000 and inside=001F; scratch/screenshots/world-stage13-authored-clip-wide-f701.png: coherent 684-column scene with uniform black y0..7 and y232..239 guard bands; painterplan f122: 710 cyclorama + 639 world + 93 actor items in one domain/range.

## What would falsify it

any static stage-13 final-stream oracle difference, producer refusal, non-black guard-band pixel, atlas pixel in the 684-column display, or evidence that a shipping producer executes a guest/generated renderer or consumes guest packet/OT/GTE output

## Re-confirmed 2026-08-22 17:12:18

Reverified after derived-runtime migration and producer-key verifier completion: 24/24 CTest and real 3000-field boot gate 14/14 pass; stage 13 reaches 1,492,683 attributed native prims and the real Vulkan draw-area discriminator remains exact.
