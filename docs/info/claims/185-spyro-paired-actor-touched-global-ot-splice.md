---
id: C185
kind: claim
status: holds
created: 2026-08-14
tags: render, paired-actor, oracle, ordering-table
depends: game/render/fx_paired_actor.cpp#snapshot_local_ot, game/render/fx_paired_actor.cpp#capture_ot_checkpoint
---

## Claim

For observed normal 0x80023AC4 invocations, Spyro's native model reproduces every global OT pair and packet-tag word touched by the guest local-to-global splice.

## Evidence

The 4,100-frame operator run in `scratch/logs/paired_ot_global_4100.log` reached 384 producer invocations. It matched 69,881 packets and 22,709 touched global words across 4–9 global slots per invocation, with both exact-PC checkpoints reached once, all 288 local bins scanned, and zero local/global mismatches, topology errors, uncleared words, or simulation errors. The same global-word comparator rejects a deliberately corrupted expected word. `PSXPORT_SELFTEST=pairedpose` passes 14 checks, including pre-existing-chain append, compact-tag high-byte preservation, and corrupt-link rejection.

## What would falsify it

Any reached normal-path invocation with missing or duplicate checkpoints, a touched global word/tag mismatch, an unjoined packet, a local topology/clearing error, or a corruption control that is accepted falsifies this claim. Untouched global slots and the alternate/status-plane parser are explicitly outside it.
