---
id: C081
kind: claim
status: holds
created: 2026-07-29
tags: ownership,native
---

## Claim

Fifteen guest functions are now owned natively (~834 static call sites) and the HIGH-CALLER LEAF QUEUE IS EXHAUSTED — the top remaining leaf candidate has 15 callers, down from 136.

## Evidence

PSXPORT_NDIFF=40: 600 'matches the recompiled body exactly' across 15 sites, zero DIVERGES, comparing RAM, scratchpad, all 31 GPRs, hi/lo and the full COP2 register file. Added this round: isqrt@0x80017A38 (17 callers) and vscale@0x800175B8 (24, GTE GPL + signed divide). own_candidates.py, which hides owned functions, now reports its best remaining leaf at 15 callers — so the cheap, high-value leaf work is done and further progress means non-leaf functions, whose callees must be owned first. FOUR of the fifteen use the GTE, all owned WITHOUT reimplementing it: scalar logic native, COP2 through the platform's gte_op/gte_read_data/gte_write_ctrl, and integer divide through cpu_div rather than C++ division (which is UB for /0 and INT_MIN/-1, both of which MIPS defines).

## What would falsify it

indirect callers are invisible to own_candidates.py, so a low static count is not proof a function is cold — a runtime call-count would falsify the 'exhausted' framing
