---
id: C188
kind: claim
status: holds
created: 2026-08-14
tags: render,paired-actor
depends: game/render/fx_paired_actor.cpp#submit_native, game/render/paired_actor_decode.cpp#resolve_normal_faces, game/render/render_frame.cpp#SpyroRenderer::drawFrame
---

## Claim

Spyro 0x80023AC4 normal opaque/textured path submits exactly one authored-order painter object from production pose, transform, projection, face and GPU material state

## Evidence

paired_submit_reference_final.log exit 0: 384 full guest oracles and 2021 reference ownership gates at groups 0/0; paired_submit_native_final.log exit 0: 274/274 armed native groups accepted with no refusal/FATAL; first native and reference censuses both 355 candidates/184 faces; paired_transform_oracle_join2.log 384/384 at regs36/36 roots6/6 vertices238/238

## What would falsify it

any native invocation emits more than one group, reference leg emits a native group, ordered face/material census differs, a refusal path falls through, or submit_native/resolve_normal_faces/PainterObjectScope renderer behavior changes without rerunning both 4100-frame legs
