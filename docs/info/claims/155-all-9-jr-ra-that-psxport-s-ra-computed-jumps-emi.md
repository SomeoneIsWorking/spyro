---
id: C155
kind: claim
status: holds
created: 2026-08-05
tags: recomp,psxport
depends: tools/ra_classes.py
---

## Claim

All 9 `jr $ra` that psxport's ra_computed_jumps emits as computed jumps in Spyro MAIN are ORDINARY RETURNS — the emitter has no genuine coroutine to find in this executable

## Evidence

tools/ra_classes.py (I040) on SCUS_942.28 + the shipped generated/rec_decls.h: 9 of 778 jr-$ra sites classified computed, 9 of 9 proven returns by two independent proofs. Rule A (module-wide save slot): 7 sites reload $ra from the fixed global block 0x80077DD8+44, with 19 matching 'sw $ra,44(0x80077DD8)' stores module-wide out of 457 'sw $ra' sites scanned — the store is simply in a different partition entry from the load. Rule B (real reaching-definitions over the CFG): 0x800535E0 and 0x80053600 sit past a jal at 0x80053598 that the bne at 0x8005358C skips, so $ra is untouched on every path that reaches them. Runtime corroboration from the failing run itself: the recomp-MISS report printed 'guest RAM locations holding 0x8001E91C: [0x80077E04]' = 0x80077DD8+44. Independently agrees with the hand audit recorded in docs/issues/0040.

## What would falsify it

ra_classes.py reports any site surviving BOTH proofs on this executable, i.e. a jr $ra whose $ra is 'computed' on every CFG path and is not a reload of a global slot with a module-wide store. Also falsified if the game is ever shown at runtime to resume mid-body through one of these nine — put a breakpoint on the emitted rec_dispatch and see where it goes.

## Confirmed and ACTED ON (2026-08-06)

The claim held, the emitter was fixed, and the fix is what the claim's own falsifier asked for.
`external/psxport/tools/recomp/emit.py` `ra_computed_jumps` now (a) looks for the matching `sw $ra`
MODULE-WIDE keyed by the resolved link-time BASE, not by the displacement inside one partition entry,
and (b) traverses a forward fixpoint over the basic-block graph instead of a linear address sweep.
Re-measured on the SAME executable with the patched emitter: **0 of 778** `jr $ra` in Spyro MAIN are
classified computed, over 790 fragments, 11 distinct global `sw $ra` save slots, 6 `lw $ra` bases
unresolved and counted as returns. Every one of Spyro's 12 overlay modules also reports 0.

CROSS-CHECKED AGAINST THE OTHER TWO CONSUMERS, same emitter, same executables, off each game's own
`generated/rec_decls.h` partition:
  * spider1 MAIN: 1 of 1722 before, 1 of 1722 after — **`0x8002A460` is unchanged**, which is the
    site RE-16 needs. It survives because the decoder saves its continuation from `$at`
    (`or $1,$zero,$ra` at 0x8002A7EC, then `sw $at,48($t6)`), never from `$ra`, so there are 0
    `sw $ra` anywhere in its 1039 `sw $ra` sites targeting `(0x80097D88, 48)`.
  * Tomba!2 MAIN: 53 of 433 before, 1 of 433 after. All 53 were in a DATA region decoded as code
    (0x80037658-0x80037B50 and 0x80037DE0/DE4 — repeating words that disassemble as `op:0x3F`,
    `special:0x0A`). The one that remains, `0x8001A620`, is NEW and is a FIX, not a regression: the
    emitter already emits `goto L_8001A554;   /* internal call (bal) */` for the `jal` at
    0x8001A378 (verified by calling emit_func on that fragment), so its `jr $ra` must resume at
    0x8001A380 — and the old classifier emitted a bare `return;` there, unwinding out of
    gen_func_8001A13C. That is Spider-Man's RE-16 bug, live in Tomba!2 and never noticed.
