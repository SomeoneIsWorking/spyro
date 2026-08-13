---
id: C182
kind: claim
status: holds
created: 2026-08-14
tags: 
depends: game/render/fx_paired_actor.cpp#project_rtps, game/render/fx_paired_actor.cpp#capture_guest_projection
---

## Claim

Spyro's pure native 0x80023AC4 fixed-point transform/projection evaluator reproduces guest SXY/SZ for all 238 resolved vertices across 384 live producer invocations

## Evidence

scratch/logs/paired_projection_pure.log completed 4100 frames exit 0: 384 invocation summaries each target_rtps=241/241 xyz=238/238 projected=238/238 mismatches=0. The exact post-GTE observer was discriminating: the retired direct GTE replay previously produced 235/238 mismatches on its second invocation, first guest (418,-5,3938) versus native (409,-14,3698). Shipping evaluator project_rtps reads explicit DR0/1 and CR0..7/24..26 and mirrors wrap44, UNR division and clamps without COP2/global hooks.

## What would falsify it

Any change to fx_paired_actor.cpp project_rtps/divide_unr/wrap44, the 0x80023AC4 target-PC/warm-up correlation, psxport's GTE post-op observer ordering, or a live invocation reporting other than target 241/241, projected 238/238, mismatches=0 falsifies this claim.
