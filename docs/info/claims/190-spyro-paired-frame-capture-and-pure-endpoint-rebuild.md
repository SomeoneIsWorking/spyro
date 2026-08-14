---
id: C190
kind: claim
status: holds
created: 2026-08-14
tags: render,paired-actor,temporal
depends: game/render/fx_paired_actor.cpp#emit_captured_endpoint, game/render/fx_paired_actor.cpp#submit_native
---

## Claim

Spyro's joined 0x80023AC4 normal producer owns an immutable host-only render recipe and can rebuild its real endpoint into an explicitly supplied RenderQueue without rereading guest state or copying prior resolved faces/RqItems.

## Evidence

`scratch/logs/paired_frame_native.log`, exit 0 over 4100 presents: 275 native joined groups, zero ownership failure, refusal or FATAL. The shipping submit path itself emits through `emit_captured_endpoint`, which reprojects captured pose through captured transforms and reruns `resolve_normal_faces` from captured primitives/materials. `PSXPORT_SELFTEST=pairedpose` passes 24 checks, including immutable material-copy independence, rejection of identical topology across a changed stage-2 epoch, and invalid/culled/duplicate rebuild refusal.

## What would falsify it

Any rebuild reads guest memory, reuses cached resolved faces or RqItems, accepts an invalid/culled/duplicate recipe, differs from the shipping endpoint, or changes the capture/rebuild/RenderQueue contract without rerunning the native 4100-present gate and hermetic selftest. This claim does not certify previous/current timing, interpolation, or FPS60 present integration.
