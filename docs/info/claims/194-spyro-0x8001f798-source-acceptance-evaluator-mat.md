---
id: C194
kind: claim
status: holds
created: 2026-08-14
tags:
depends: game/core/actor_chain_oracle.cpp#evaluate_candidate, game/core/actor_chain_oracle.cpp#finish_prediction
---

## Claim

Spyro 0x8001F798 source acceptance evaluator matches every reached candidate

## Evidence

PSXPORT_ACTOR_CHAIN_ORACLE=payload, 500-present run scratch/logs/actorchain_acceptance_green.log: 32 calls, 6464 candidates = 3021 emitted + 3443 rejected; exact next-source cursor, family, origin, pool delta and payload with zero mismatches. Shipping actorchainrecipe selftest drives direct one/two-sided/zero/skip/outcode/depth, all quad sign cells, FT4 unsupported and corruption negatives.

## What would falsify it

Any same-path run with evaluator/cursor/pool/payload mismatch; any reached unsupported FT4/raw command; or a generated-code reread that changes W0 control-bit, NCLIP, cursor, or depth formulas falsifies this claim.
