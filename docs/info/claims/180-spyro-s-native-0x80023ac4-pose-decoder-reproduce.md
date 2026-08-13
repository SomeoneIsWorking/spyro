---
id: C180
kind: claim
status: holds
created: 2026-08-14
tags: render,paired-actor
depends: game/render/fx_paired_actor.cpp#compare_actual_guest
---

## Claim

Spyro's native 0x80023AC4 pose decoder reproduces all 238 resolved RTPS input vertices across its three animation layers on both unblended and paired-INTPL live frames.

## Evidence

scratch/logs/pairedpose_oracle_fixed2.log: repeated exact-PC pre-GTE comparisons report target_rtps=241/241, compared=238/238, mismatches=0; the 241 operations are 238 vertices plus one software-pipeline warm-up per layer. Earlier discriminator runs independently produced 165/238 then 108/238 mismatches before the layer-0 stream direction and DR0 arithmetic-pack fixes, proving the instrument can report the opposite result.

## What would falsify it

Any same-state exact-PC pre-GTE run reports a target denominator other than 241/241, a vertex denominator other than 238/238, or any XYZ mismatch; or a reachable model uses non-monotonic layer boundaries rejected by the decoder.
