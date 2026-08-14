---
id: C195
kind: claim
status: holds
created: 2026-08-14
tags: actor-chain,native-producer,oracle
depends: game/render/actor_prefix_builder.cpp#build, game/core/actor_chain_oracle.cpp#actor_chain_prefix_build_oracle
---

## Claim

Spyro's pure immutable 0x8001F798 reached-prefix builder reproduces the supported transform and projection endpoints and PositiveBlend scratch colors without consuming guest scratch or GTE state

## Evidence

scratch/logs/actorchain_prefix_scoped.log: 10 PASS, 22 classified ClipStatus REFUSED, 0 FAIL/NO_CORPUS; each PASS 195/195 RTPS inputs/post-ops and 2145 controls exact; 1094/1094 PositiveBlend scratch colors exact. High colors and primitive words are capture-only denominators, not independent output proof.

## What would falsify it

a fresh supported prefix-build call mismatches any RTPS input, control, MAC/IR/SXY/SZ, or PositiveBlend scratch color; or the builder gains a Core, guest-scratch, ambient-GTE, or opcode dependency
