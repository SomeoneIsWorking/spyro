---
id: 49
title: overlay_funcs() seeds jal-shaped DATA words as MAIN function entries — 36 of 215 current candidates were not function entries
status: resolved
symptom: an emitted 'function' starts mid-body (gen_func_80023384 begins at a delay-slot nop); a guest function is split into two C functions and its prologue store is invisible to its epilogue load
tags: recomp,psxport,partition,debt,measured
created: 2026-08-06
updated: 2026-08-28
---

## Root cause

`external/psxport/tools/recomp/emit.py` `overlay_funcs()` scans each overlay BLOB word by word and
seeds every word that decodes as a `jal` into MAIN text. Its only filter is *"the target's first word
does not decode as UNKNOWN"*. A `nop` (0x00000000) passes that trivially, as does any data word whose
top six bits happen to read as `jal`. It is the ONE seeder in emit.py that does not consult
`is_func_entry` — `overlay_data_func_pointers()` right beside it already does.

MEASURED 2026-08-06 on SCUS_942.28 + scratch/bin/overlays:

    overlay_funcs seeded 223 MAIN addresses; 44 of them FAIL is_func_entry.

Four of those 44 are exactly the fragment starts the `jr $ra` misclassification report named:
0x80023384 (a `nop` in the delay slot of `j 0x800232A8` at 0x80023380 — provenance OV_287800.BIN +
67712 = 0x0C008CE1), 0x8004C3FC (an `mtc2` mid-GTE-sequence), 0x8004D580, 0x8005000C. The rest include
obvious data: 0x80040000/0x80040004 as an adjacent pair, 0x8004004C decoding as `break`, several
`nop`s, and mid-loop `bne`/`slti`/`subu`.

## Why it is NOT fixed here, and what the risk is

The `jr $ra` classifier (issue 0046) was made INDEPENDENT of the partition instead — its save-slot
test is module-wide and base-keyed, and a `jr` unreachable from a fragment's entry defaults to
`return`. So this seeder defect no longer causes the SIGABRT it used to. Fixing the seeder is a
separate change with its own risk profile: gating on `is_func_entry` would also drop any LEGITIMATE
overlay-jal'd MAIN entry that neither starts with `addiu sp,sp,-N` nor sits two words after a
`jr ra` — e.g. the first function after a data blob — and a dropped seed is a `recomp-MISS` at
runtime, i.e. the same fatality in the other direction. docs/issues/0040 already measured that
partition repairs on this game move sites in BOTH directions, which is the signature of a rule that
is not converging on the truth.

## Resolution

`overlay_funcs()` now requires every overlay-derived MAIN target to pass `is_func_entry()` before
it can seed a new resident function. The module-wide data scan remains available as a graph edge for
reaching-constant analysis; it no longer splits an existing MAIN body merely because a data word
decodes as `jal`.

On the current Spyro overlay set the old rule produced 215 unique candidates, of which 36 failed
`is_func_entry()`. The exact collision false entry `0x8004C3FC` came from
`OV_20F800.BIN + 0x109c4` word `0x0C0130FF`; it is an `mtc2` in the middle of the
`0x8004BE4C` collision body, not a function entry. After the filter, the regenerated substrate has
one `gen_func_8004BE4C` containing the `0x8004D030` selector case, and no `gen_func_8004C3FC`.

The framework emitter suite passes 64/64, the synthetic data-word regression passes, and the exact
Clang Spyro product reaches the stage-0 branch without the former `0x8004D030` dispatch miss. The
live New Game route then reaches the separate native-render cyclorama refusal; issue 0089 tracks that
remaining scene boundary.
