---
id: C186
kind: claim
status: holds
created: 2026-08-14
tags: render, paired-actor, depth, ordering-table
depends: game/render/paired_actor_decode.cpp#analyze_overlap_depth, game/render/fx_paired_actor.cpp
---

## Claim

Submitting Spyro's normal 0x80023AC4 faces with geometric D32 and insertion order alone cannot reproduce the guest painter chain.

## Evidence

The reached-frame discriminator in `scratch/logs/paired_overlap_witness.log` evaluates integer pixel centers with top-left coverage and barycentrically interpolates the same per-vertex reciprocal-depth order values proposed for Vulkan. Of 207 opaque overlapping face pairs covering 571 pixels, 19 contain a guest-order/D32 contradiction and one ties; seven contradictions are within one OT bin. The first witness is source 323/bin 136 before source 263/bin 130 at pixel `(430,-10)`, where the later guest face has order `0.041617583` but the earlier face has `0.042118069`. Hermetic tests exercise stable, inverted, disjoint, tie, quad, semi-excluded, same-bin, and fractional-depth cases.

## What would falsify it

An exact shipping-raster discriminator over the same resolved faces that reports nonzero opaque overlap coverage but zero D32/guest-order contradictions would falsify this claim. A policy that preserves both per-pixel authored order and real winning-fragment depth would supersede the insertion-only proposal without falsifying the measured conflict.
