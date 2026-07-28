---
id: C070
kind: claim
status: holds
created: 2026-07-28
tags: input,stage
---

## Claim

Forced input demonstrably changes the game's trajectory end-to-end: holding START drives the stage machine down a DIFFERENT branch (mode 13 sub 0->1, no demo handler installed) than idle (mode 13 sub 0->3, handler installed, then mode 0 with demos cycling).

## Evidence

Two 90s headless runs differing only in PSXPORT_FORCE_BUTTONS=FFF7 (START active-low). Idle: stage mode alternates 13/0, sub reaches 3, [0x800758CC] holds a level handler, and three different level overlays cycle. With START pulsed: mode stays 13 throughout, sub reaches 1 — a value never observed idle — and [0x800758CC] stays 0. Both runs rc=137 with zero recomp misses, so the difference is the game reacting to the button, not a crash or a stall difference.

## What would falsify it

if sub=1 turns out to be reachable idle in a longer run, the branch difference would not be attributable to the button
