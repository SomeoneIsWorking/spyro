---
id: C024
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

Spyro DOES load code from WAD.WAD and call it: the overlay question is answered

## Evidence

The new regression gate caught a recomp-MISS the manual log reading had not: 'no recompiled fn for 0x8007ABAC (caller ra=0x800339E4, c->pc=0x800647A0)'. 0x8007ABAC is ABOVE the resident text end (0x80075800) and sits at heapBase+0x174 (heapBase=0x8007AA38) — inside the very region the owned loader had just filled with 2048 bytes read from WAD.WAD. So the guest loads code off the disc into the heap and calls it. That is the overlay mechanism the disc's file tree could not show, since there are no per-overlay FILES.

## What would falsify it

if 0x8007ABAC is later shown to be data being called through a corrupted pointer rather than loaded code, this is wrong — check whether the loaded bytes at that offset decode as a valid MIPS prologue
