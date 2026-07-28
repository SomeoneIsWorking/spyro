---
id: 20
title: Computed-jump dispatch region 0x8004BE3C-0x8004C5F8 is not modelled by function discovery
status: open
symptom: [recomp-MISS] 0x8004C4EC (caller ra=0x1F800000, a0=0xFFFFFD90, pc=0x8004BE4C). Reached only after the first level overlay actually loads.
tags: recomp,blocker
created: 2026-07-28
updated: 2026-07-28
---

This is the port's only remaining fail-fast, and it is NOT a normal missing-function case.

0x8004C4EC is a jump-table CASE BODY, not a function entry: 0x8004C4E4 is , a
computed jump. Six case bodies follow in three pairs, each pair converging on a common continuation:
  0x8004C4EC, 0x8004C4FC -> 0x8004C514
  0x8004C550, 0x8004C560 -> 0x8004C578
  0x8004C5D0, 0x8004C5E0 -> 0x8004C5F8

TRIED AND REVERTED: adding those six to . No effect, because the enclosing code is not a
recompiled function at all — and it has no ordinary entry to seed either. A -based entry scan
reports 0x8004BE3C as the function start, but that address is  — more
unrolled dispatch, not a prologue. So the whole region 0x8004BE3C-0x8004C5F8 is one large computed-jump
area, and neither  nor  is the right lever.

DO NOT seed a guessed entry here. Splitting a real function at an arbitrary offset silently corrupts the
recomp, which is precisely what game/recomp_seeds.json exists to prevent.

NEXT: work out what the region actually is. The  case bodies say GTE, and the pairs-converging
shape suggests an unrolled per-vector routine. Establish where it is entered from (the caller's ra is
0x1F800000, the scratchpad, so the usual ra reading is meaningless here — see issue 0014) and whether
psxport's recompiler has an existing mechanism for computed-jump regions; the overlay path already
prunes 121 jump-table case labels for OVL1, so the machinery may exist and simply not apply to the
resident module.
