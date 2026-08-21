---
id: 69
title: Static non-leaf ranking mostly selects cold boot-path functions
status: resolved
symptom: The next own.non-leaf milestone needs a body with owned callees and real reach; seven of eight dependency-valid static candidates were never called in a 3,000-field shipping boot.
tags: ownership,ndiff,reach
created: 2026-08-21
updated: 2026-08-21
---

## Root cause

Static caller count measures potential coverage, not execution on the shipping path. Even after
filtering to small non-leaves with no indirect calls and only already-owned direct children, the
ranking cannot distinguish a boot-reached parent from a state or level handler that remains cold.

## What was tried / dead ends

One 3,000-field `PSXPORT_FNTRACE` run covered the eight dependency-valid candidates together.
`0x80068F44`, `0x8003DFA4`, `0x80037EA0`, `0x80037714`, `0x80018534`, `0x80053570`, and
`0x8005DA74` were never called. Implementing any of them from static rank alone would not have
advanced exercised ownership.

## Resolution

### Resolution (2026-08-21)
Selection needed dynamic reach, not more static ranking. Of eight <=60-instruction non-leaves with no jalr and only already-owned direct callees, seven were never called in the 3,000-field shipping boot. InitActorMeshScratchRegions 0x8005B6F8 alone was reached (one call at frame 437 from ra=0x8005B8C0); its native body then matched the retained generated body under PSXPORT_NDIFF=1. Future ownership picks must repeat the dependency filter plus FNTRACE discriminator.
