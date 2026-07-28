---
id: C067
kind: claim
status: holds
created: 2026-07-28
tags: overlay,recomp,seeds
---

## Claim

Main names every level overlay's per-frame entry: 43 stores to [0x80075734] at 0x8005A4CC-0x8005B6BC yield 36 distinct addresses, and the prologue test in each overlay's own bytes assigns them one-to-one.

## Evidence

Corrected extraction (the earlier one grabbed a neighbouring store's lui/addiu pair and produced 41 wrong values — 0x8005A4CC actually stores 0x8007D8E0, not 0x80080548). 36 distinct matches the 36 code overlays of C033. Claiming an address only when it is prologue-shaped in that overlay's own image gives: OV_237D000 -> 0x8007AEB8, OV_2F5B000 -> 0x8007B7A8, OV_502F800 -> 0x8007CFB4, OV_B83800 -> 2, OV_0/OV_5F000/OV_5B800 -> none. The first two were independently confirmed against RAM dumped at their own fail-fasts; the third is the address that had been fail-fasting. Port runs rc=137 with 0 misses on the derived seeds alone.

## What would falsify it

an overlay whose real entry is NOT prologue-shaped (a leaf entry with no frame), or one claiming an entry that belongs to another level — either breaks the one-to-one assignment the derivation depends on
