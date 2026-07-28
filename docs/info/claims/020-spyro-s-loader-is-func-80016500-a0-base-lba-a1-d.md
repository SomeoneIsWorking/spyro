---
id: C020
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

Spyro's loader is func_80016500(a0=base LBA, a1=dest, a2=len, a3=byte offset)

## Evidence

Logged all five loader calls: a0 is CONSTANT at 37 (WAD.WAD's LBA) while a3 varies and is always 2048-aligned — 0x00000, 0x5F000, 0x5F800, 0x5B800, 0x00800 — and destinations/lengths vary independently. So a0 is the archive base and a3 the byte offset into it; sector = a0 + a3/2048. Reading from a0 alone fetched the archive's FIRST sectors for every request: right destination, right length, wrong content — bytes moved, so it looked like it worked.

## What would falsify it

if a3 is ever observed non-2048-aligned, or content read at a0+a3/2048 fails a checksum the guest applies, the mapping is wrong
