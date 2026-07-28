---
id: C080
kind: claim
status: holds
created: 2026-07-29
tags: ownership,native
---

## Claim

Thirteen guest functions are now owned natively (~793 static call sites), including two GTE bodies, each byte-exact on 40 consecutive calls.

## Evidence

PSXPORT_NDIFF=40, all 13 report 40/40 matches and zero DIVERGES = 520 verified calls comparing RAM, scratchpad, all 31 GPRs, hi/lo and the full COP2 register file. Added this round: copyw@0x80016958 (30 callers, 4-way unrolled word copy) and mvmva@0x80017048 (30 callers, GTE matrix transform). Running total: copy3 136, vadd 102, veclen 87, vsub 83, angtblA 69, angtblB 66, fill 59, rand 41, zero3 40, copyw 30, mvmva 30, angdist 26, vsra 24.

## What would falsify it

a recompiler or GTE-model change altering any of these bodies; the gate re-verifies all thirteen every run
