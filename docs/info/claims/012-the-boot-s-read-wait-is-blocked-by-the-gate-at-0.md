---
id: C012
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

The boot's read-wait is blocked by the gate at 0x80076BB8, not the CD status bit

## Evidence

In-process override logging (PSXPORT_DEBUG=cdq) on every iteration: gate=[0x80076BB8]=1, status=[0x800774B4]=0x40, a=[0x800758E0]=0. Since func_80016500 tests the gate FIRST and retries when non-zero, the CdSync and status tests are never reached — and the status test would pass anyway. func_800163E4 enters and exits cleanly each iteration (early-returns on a==0), so it is not the spin either.

## What would falsify it

if a run ever shows gate==0 while the boot still spins, the gate is not the blocker
