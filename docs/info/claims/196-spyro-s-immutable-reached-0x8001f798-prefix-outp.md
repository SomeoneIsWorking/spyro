---
id: C196
kind: claim
status: holds
created: 2026-08-14
tags: actor-chain,native-producer,composition
depends: game/render/actor_draw_recipe.cpp#compose
reconfirmed: 2026-08-28 04:08:50
verified_at: 2026-08-28 04:08:50
---

## Claim

Spyro's immutable reached 0x8001F798 prefix outputs compose into the exact reached candidate, packet-content, numeric-local-bin and global-splice sequence without guest scratch as an input

## Evidence

scratch/logs/actorchain_recipe_payload.log: 32/32 PASS, 6464/6464 candidate semantic inputs and 3021/3021 G4/GT4/G3/GT3 faces matched final-pool order/content with zero mismatch; scratch/logs/actorchain_recipe_ot.log: 32/32 PASS, the same immutable recipe matched 6464 candidates and 3021 faces while 18432 local slots and 5076 predicted splice words compared with zero mismatch. actorchainrecipe selftest drives valid empty, atomic semi/malformed/bin refusals and named source/depth/fog plus payload/link/splice corruption answers.

## What would falsify it

any fresh payload or OT recipe join reports an input/order/payload/bin/splice mismatch; a reached FT4/semi/raw branch appears; or composition begins reading guest scratch/Core state

## Re-confirmed 2026-08-28 04:08:50

2026-08-28: focused actor_draw_recipe 13/13 checks pass after the prefix status cleanup; Unsupported remains exercised with NegativeBlend and atomic composition semantics are unchanged.
