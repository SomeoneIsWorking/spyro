---
id: C014
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

a1 is NOT the CD read destination buffer — the per-sector copy hypothesis is falsified

## Evidence

Probes showed func_80065DBC and func_8006606C both receiving a1=0x8007AA38 (== heapBase), so a1 looked like the load buffer. Implemented a per-sector copy from Cd::setloc_lba into it, one sector per delivered completion. PREDICTED: the guest advances past its first sector. OBSERVED: it re-issued the SAME read at LBA 37, no new LBA was ever sought, frames stayed at 8. Reverted.

## What would falsify it

if a later run shows the guest advancing after writing sectors to a1, this is wrong and a1 is the buffer after all
