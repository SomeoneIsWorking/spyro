---
id: C077
kind: claim
status: holds
created: 2026-07-28
tags: ownership,native
---

## Claim

Seven guest functions are now owned natively, covering ~530 static call sites, each verified byte-exact on 40 consecutive calls — and the per-call differential caught two real errors in transcription that reading the disassembly did not.

## Evidence

PSXPORT_NDIFF=40: rand@0x8006272C, copy3@0x80017700, zero3@0x800176F0, fill@0x80016914, vadd@0x80017758, vsub@0x8001778C, angtbl@0x80016CB0 — all 40/40 'matches the recompiled body exactly', 280 verified calls, zero DIVERGES. Static callers (lower bound, tools/own_candidates.py): 41+136+40+59+102+83+69 = 530. TWO ERRORS CAUGHT: (1) an unreproduced $at clobber in rand, which matched for 9 calls before diverging; (2) a mis-subtracted table base in angtbl — lui 0x8007 + addiu -13192 is 0x8006CC78, I wrote 0x80073C78 — caught on call #1 by 'a0: native=0x80073C94 substrate=0x8006CC94', exactly 0x7000 apart. Both are the class of error the project rule 'never guess a guest address' exists to prevent, and both were found in one run each rather than by review.

## What would falsify it

a recompiler change altering any of these bodies makes the native versions stale; the gate re-verifies all seven every run, so a divergence there falsifies this
