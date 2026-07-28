---
id: C059
kind: claim
status: holds
created: 2026-07-28
tags: recomp,framework
---

## Claim

Recognising tail-returns through a non-ra register advanced the port 4234 -> 4354 frames

## Evidence

A guest that moves ra into another register and jumps through it is RETURNING, but the recompiler routed that to rec_dispatch, where the target — the instruction after some caller's jal — is mid-function, never a function entry, and fail-fasts. Spyro: 0x80053594 'addi a3,ra,0' then 0x800535B8 'jr a3' lands on 0x80038620, exactly the instruction after 'jal 0x800530C0'. Emitting 'return' instead lets the C stack unwind to the real caller, which resumes after its own call site — the guest semantics exactly. Decided STATICALLY by a new ra_tail_returns() pass, so it cannot mask a genuine computed call. Narrow by measurement: 4 of 168 'jr rX' (rX != ra) in the resident text qualify, and 3 sites were emitted. Result: 4234 -> 4354 frames, unmapped-RAM zero, next fail-fast at a new address (0x800240F8).

## What would falsify it

Any jr classified as a tail-return whose register does not in fact hold ra at runtime — which would return to the wrong place and show up as a wild control-flow failure rather than a clean miss.
