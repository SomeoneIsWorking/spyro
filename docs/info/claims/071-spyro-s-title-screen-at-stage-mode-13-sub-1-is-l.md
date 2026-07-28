---
id: C071
kind: claim
status: holds
created: 2026-07-28
tags: stage,input,instrument
---

## Claim

Spyro's title screen at stage mode 13 / sub 1 is LIVE, not hung: a counter at [0x80078D84] advances every 2 frames while the sub-state stays 1. Absence of stage transitions is not absence of progress.

## Evidence

PSXPORT_WWATCH=0x80078D80,0x80078D90 over a 45s forced-START run shows continuous stores: [0x80078D84] stepping 0x730D, 0x730E, 0x730F... past 0x7312, and [0x80078D80] cycling 0/0xC/1, all from pc=0x80058CC0 with ra=0x8007CC50. The earlier 'hangs' reading came from counting stage transitions (two in 90s) — the wrong instrument for the question. The same watchpoint also settled in ONE run what two static scans had missed: sub=1 is written exactly once, at frame 835, by func_8002BFE0 from ra=0x8007AD04, via a register (s0=address, s1=value) rather than a lui/addiu pair, which is why address-immediate scans could not see it.

## What would falsify it

if [0x80078D84] stops advancing in a longer run, the state really would have stalled and this claim would need re-checking
