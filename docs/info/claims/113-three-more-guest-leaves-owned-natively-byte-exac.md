---
id: C113
kind: claim
status: holds
created: 2026-07-29
tags: ownership,native
---

## Claim

Three more guest leaves owned natively, byte-exact: 0x800168DC (display-list link, 16 static callers), 0x80017990 (2D distance approximation, 9 callers) and 0x80063C30 (set-global-return-previous, 7 callers). The display-list link writes a 24-BIT pointer — halfword then byte into the old head's low three bytes — and makes the new node the list head UNCONDITIONALLY because that store sits in the branch's delay slot; a0 also exits shifted right by 16 on the non-empty path and unshifted on the empty one. The distance routine is the octagonal approximation max + (3*min >> 3), with  exiting as 2*min on both arms.

## Evidence

PSXPORT_NDIFF=64 over a 1200-frame headless run: 0 divergences, all three reaching the 64-call cap. Gate 14/14 with 'native bodies verified' rising 136 -> 160 and divergences 0. Two further transcriptions (0x8006276C strlen, 0x80067614) were completed and deliberately NOT installed — a 4000-frame run with input driven calls neither once, so neither can be differentially verified; both are recorded in issue 0031. Their static caller counts are 9 and 8, which is a reminder that caller count describes the disassembly rather than the running port.

## What would falsify it

Any ndiff divergence at these three sites, or a display list observed with a node whose low three bytes do not point at its successor.
