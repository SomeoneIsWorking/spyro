---
id: C184
kind: claim
status: holds
created: 2026-08-14
tags: render, paired-actor, oracle, ordering-table
depends: game/render/fx_paired_actor.cpp#capture_ot_checkpoint, game/recomp_seeds.json
---

## Claim

Spyro's native normal-face resolver reproduces the guest 0x80023AC4 producer's local numeric OT-bin membership and high-to-low, FIFO-within-bin face order.

## Evidence

An operator run in `scratch/logs/operator-paired-ot-live.log` reached 384 producer invocations. Every invocation reported both owner-qualified checkpoints exactly once, scanned all 288 local bins, joined every emitted packet, and compared every numeric bin with zero unmapped packets, duplicates, cycles, bad tails, uncleared words, or mismatches. Packet counts varied from 172 to 202, contradicting fixed-count instrumentation. `PSXPORT_SELFTEST=pairedpose` passes 12 checks and includes positive high-bin/FIFO cases plus corrupt-tail, corrupt-link, and corrupt-bin negatives.

## What would falsify it

Any same-path run that does not reach exactly one pre/post checkpoint, scans other than 288 bins, fails to join every guest packet exactly once, or reports a numeric-bin/order/topology/clearing mismatch falsifies this claim. Placement relative to packets already present in the global OT is explicitly outside the claim.
