---
id: C107
kind: claim
status: holds
created: 2026-07-29
tags: ownership,native
---

## Claim

Two more guest leaves owned natively, byte-exact: 0x80017908 (8-bit angle absolute separation, 14 static callers) and 0x8005C720 (a calibrated busy-wait, 18 static callers — the most of any remaining leaf). 0x8005C720 is a pure delay loop: 60 iterations multiplying a stack temp by 13 via shift-add, product never read. Owning it is how a PC port stops burning CPU on a cycle-calibrated spin, but the replacement is still byte-exact because the two stack words live below sp at exit and the differential compares all of RAM regardless.

## Evidence

PSXPORT_NDIFF=64 over a 900-frame and a 4000-frame headless run: 0 divergences, 17 native bodies each reaching the 64-call cap including both new ones. Gate 14/14 with 'native bodies verified' rising 120 -> 136 and divergences 0. The differential earned its keep: spin60 diverged on call #1 (v1 native=0x890E6FBD vs substrate 0x94639271) because v1 is loaded at the TOP of the loop body and so lags the stored product by one multiply (13^60, not 13^61) — an error in a register no caller reads, in a function whose result is discarded, which no amount of reading the code would have flagged.

## What would falsify it

Any ndiff divergence at these two sites, or a caller found that reads a0/a1 after 0x80017908 in a way the reproduced exit state gets wrong.
