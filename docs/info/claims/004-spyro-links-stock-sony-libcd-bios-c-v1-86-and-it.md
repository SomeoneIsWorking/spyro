---
id: C004
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

Spyro links stock Sony libcd (bios.c v1.86), and its internal primitives are identified by the name each prints

## Evidence

Image carries the literal '$Id: bios.c,v 1.86 1997/03/28 07:42:42 makoto Exp $' at 0x80011EB8. lui+addiu attribution of the name strings: func_800647A0='CD_sync', func_80064A20='CD_ready', func_80064CEC='CD_cw', func_800653B4='CD_init', func_800655A0='CD_datasync'. Confirmed dynamically: wiring hle.cdInitHandshake=0x800653B4 removed the CD_init/CdlNop/CdlReset boot loop, and hle.cdDataSync=0x800655A0 removed the CD_sync timeout.

## What would falsify it

if a boot log shows a CD_init or CD_sync timeout again, one of these attributions is wrong or a code path reaches a different primitive
