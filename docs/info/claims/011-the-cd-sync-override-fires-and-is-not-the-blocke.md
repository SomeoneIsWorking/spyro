---
id: C011
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

The cd_sync override fires and is not the blocker; the read-wait needs CD status bit 0x40

## Evidence

gdb breakpoint on cd_sync: hit, called from gen_func_80063BD8 <- gen_func_80016500. It returns 2, which is exactly what the loop compares against (r16=2). Decoding func_80016500 shows a THIRD condition — [0x800774B4] & 0x40 — that no code path ever satisfies, because that CD status byte is refreshed by libcd's interrupt callback and no guest IRQ is raised.

## What would falsify it

if the loop is later seen exiting without 0x800774B4 ever having bit 0x40 set, the decode is wrong
