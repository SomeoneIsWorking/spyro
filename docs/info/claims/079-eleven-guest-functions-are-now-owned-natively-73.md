---
id: C079
kind: claim
status: holds
created: 2026-07-29
tags: ownership,native,gte
---

## Claim

Eleven guest functions are now owned natively (~733 static call sites), including GTE geometry code — owned WITHOUT reimplementing the GTE, by calling the platform's own COP2 model from the native body.

## Evidence

PSXPORT_NDIFF=60: all 11 report 60/60 matches, zero DIVERGES = 660 verified calls comparing RAM, scratchpad, all 31 GPRs, hi/lo AND the full COP2 register file (I021). veclen@0x800171FC (87 callers) is GTE SQR + leading-zero normalise + a sqrt table at 0x80074B84; the native body does the loads, scalar maths and table lookup itself and calls gte_op/gte_read_data/gte_write_data — the SAME free functions the generated shards call, bound per-core by gte_bind — so the hardware results match by construction rather than by re-deriving Beetle's saturation and flag rules. Reimplementing the GTE would be large, subtle, and the platform layer's job, not the game's.

## What would falsify it

a recompiler change altering any of these bodies, or a GTE model change altering SQR/LZC semantics; the gate re-verifies all eleven every run
