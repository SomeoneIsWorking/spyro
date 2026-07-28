---
id: C065
kind: claim
status: holds
created: 2026-07-28
tags: overlay,instrument,recomp
---

## Claim

The overlay resident at a fail-fast is NOT the last one the router identified — the arena is reloaded after it, so diagnosing a miss from the last-identified overlay's image reads unrelated bytes.

## Evidence

At the 0x8007CFB4 miss the arena content matched OVL2.BIN (the last identified load) for 94/12800 words — 0% — and OVL2.BIN's bytes were not present anywhere in the 2 MB dump. Searching WAD.WAD for the RESIDENT bytes matched at +0x502F800 for 40700 contiguous bytes, and the run log confirms 'stream: dest=0x8007AA38 len=40960 a3=0x0502F800' after the OVL2 load, with the router then reporting '(none/unmatched)'. Reading 0x8007CFB4 out of OVL2.BIN showed a table-indexed load and no prologue (which produced the wrong 'jump-table case label' diagnosis in issue 0025); in the resident bytes it is 'addiu sp,sp,-464' and Ghidra recovers it as a function with a 0x198-byte frame.

## What would falsify it

if a port change makes the arena hold one overlay for a whole run, or the router starts reporting residency continuously rather than at load time, the 'last identified' reading would become reliable
