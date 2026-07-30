---
id: C136
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership
---

## Claim

Four of the five remaining widescreen renderers are ACTIVE and IDENTITY-CLEAN under the per-call differential — 0x80022A2C, 0x8001F798, 0x80020F34, 0x800258F0 each match the recompiled body exactly on 4/4 calls. 0x8004F000 is armed but NEVER CALLED in the boot capture, so it cannot be certified there and must not be the first target however small it is.

## Evidence

scratch/logs/ident5.log — one 90s headless capture with PSXPORT_NDIFF=4 and the now-multi-address PSXPORT_NDIFF_IDENTITY=80022A2C,8004F000,8001F798,80020F34,800258F0. All five ARMED (logged individually, so a silent typo cannot read as 'never called'). Four report 'call #1..#4 matches the recompiled body exactly'; 0x8004F000 reports nothing at all.

## What would falsify it

0x8004F000 firing in some other capture (a level/state this boot run never reaches) — its absence here is scenario-scoped, not a property of the function
