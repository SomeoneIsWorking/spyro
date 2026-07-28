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

DECODED (C049) — it is a COMPUTED-OFFSET jump, not a table lookup:
    0x8004C4D4/D8  lui+addiu   -> s2 = 0x8004C4EC          (base, an IMMEDIATE)
    0x8004C4B0/B4  addi/sll    -> s1 = idx << 4            (stride 16)
    0x8004C4E0     add s2,s2,s1
    0x8004C4E4     jr  s2                                   => target = base + idx*16
emit.py's find_jump_tables cannot match this by design: both idioms it knows (A and B) require the
target ADDRESS to be loaded from a table via `lw rN,OFF(base)`. Here there is no table and no lw. That
is also why OVL1 got 121 case labels pruned correctly (a real table) while this one routes to
rec_dispatch and fail-fasts.

Because the base is an lui/addiu immediate, this IS statically recoverable — it is a recogniser gap,
not an undecidable case. A fix would detect `jr rN` where rN = <immediate base> + (idx << k) and emit
case labels at base + n*stride. The case COUNT is the open part: the strict table idiom gets it from a
`sltiu cond, idx, COUNT` guard, and whether an equivalent bound exists here needs checking before any
recogniser is written — emitting too many labels would carve up real code.

NEXT: work out what the region actually is. The  case bodies say GTE, and the pairs-converging
shape suggests an unrolled per-vector routine. Establish where it is entered from (the caller's ra is
0x1F800000, the scratchpad, so the usual ra reading is meaningless here — see issue 0014) and whether
psxport's recompiler has an existing mechanism for computed-jump regions; the overlay path already
prunes 121 jump-table case labels for OVL1, so the machinery may exist and simply not apply to the
resident module.


## Progress 2026-07-28 (C050, I010)

MECHANISM FOUND, and my first attempt used it wrongly. `main_reentry` alone does nothing: it does not
create a label, it makes a body that runs off its end FALL THROUGH into the named address instead of
returning — and that address must ALSO be seeded as a function in `main`. That is the documented
Tomba!2 pattern (0x8010637C -> 0x801063F4, "both seeded"). With the six case addresses in BOTH lists,
discovery goes 246 seeds -> 667 functions and the fail-fast MOVES 0x8004C4EC -> 0x8004C650: a different
case in the same unrolled region, so the port now runs past the first run. No gate metric regressed.

NEW TOOL, PARTIALLY TRUSTED: tools/computed_jumps.py (I010) locates the idiom. It correctly finds
base=0x8004C4EC stride=16 at the known site. But its case-COUNT stop heuristic never fires (it reports
the max-cases limit for 3 of 4 runs), it emitted one spurious detection (stride=2), and its backward
walk reports the jr at 0x8004C548 rather than the true dispatcher 0x8004C4E4. Its raw output is 73
candidate addresses; seeding those blind would carve up real code. LOCATE with it, hand-verify before
seeding.

NEXT: 0x8004C650 is not a member of any run the tool enumerated (0x8004C650 - 0x8004C4EC = 0x164, not a
multiple of 16), so at least one dispatcher in this region is still unaccounted for. Either widen the
tool's backward window or hand-decode the jr that reaches 0x8004C650. The durable fix is a recogniser
in emit.py for this idiom, but that needs the case COUNT recovered properly — the compiler must be
bounding the index somewhere, and finding that bound is the prerequisite for any recompiler change.
Do NOT add a recogniser that guesses the count: emit.py's own comments record that unconditional
heuristics previously BROKE already-correct table recoveries.