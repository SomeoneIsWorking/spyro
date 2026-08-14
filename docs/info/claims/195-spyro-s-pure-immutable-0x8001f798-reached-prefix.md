---
id: C195
kind: claim
status: holds
created: 2026-08-14
tags: actor-chain,native-producer,oracle
depends: game/render/actor_prefix_builder.cpp#build, game/core/actor_chain_oracle.cpp#actor_chain_prefix_build_oracle
---

## Claim

Spyro's pure immutable 0x8001F798 reached-prefix builder reproduces the supported transform,
projection and negative-header status endpoints plus PositiveBlend scratch colors without consuming
guest scratch or GTE state

## Evidence

`scratch/logs/actorchain_prefix_status.log`: 31 PASS, 0 REFUSED/FAIL/NO_CORPUS; every call compared
195/195 RTPS inputs/post-ops, 2,145 controls and 195 scratch words. Twenty-one negative-header records
(3,297 vertices) reached the status arm with exact packed words and zero common-status rejections.
PositiveBlend compared 1,094/1,094 scratch colors. High colors and primitive words are capture-only
denominators, not independent output proof. The same pure `classifyCall` seam is exercised by
hermetic Owned/NoCorpus/Unsupported cases before any future queue mutation.

## What would falsify it

a fresh supported prefix-build call mismatches any RTPS input, control, MAC/IR/SXY/SZ, packed status
scratch word or PositiveBlend scratch color; the boundary accepts an uncovered record; or the builder
gains a Core, guest-scratch, ambient-GTE, or opcode dependency
