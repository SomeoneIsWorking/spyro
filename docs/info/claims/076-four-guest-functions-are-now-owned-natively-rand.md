---
id: C076
kind: claim
status: holds
created: 2026-07-28
tags: ownership,native
---

## Claim

Four guest functions are now owned natively — rand plus three hot leaves — covering 276 static call sites, each verified byte-exact against the recompiled body on 25 consecutive calls.

## Evidence

PSXPORT_NDIFF=25 over a 45s run: copy3@0x80017700, zero3@0x800176F0, fill@0x80016914 and rand@0x8006272C each report 25 'matches the recompiled body exactly' and zero DIVERGES — 100 verified calls total, comparing RAM, scratchpad, all 31 GPRs and hi/lo. Static caller counts from tools/own_candidates.py: 136 + 40 + 59 + 41 = 276 (a LOWER bound; indirect calls are invisible to it). The ndiff log also answers the hollow-override question directly — a body that never ran could not report 25 verified calls.

## What would falsify it

a recompiler change that alters any of those bodies would make the native versions stale; the gate re-verifies every run, so a divergence there falsifies this
