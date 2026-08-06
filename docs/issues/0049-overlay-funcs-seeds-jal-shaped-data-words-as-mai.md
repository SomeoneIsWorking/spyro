---
id: 49
title: overlay_funcs() seeds jal-shaped DATA words as MAIN function entries — 44 of 223 spyro seeds are not function entries, one is a nop in a j's delay slot
status: open
symptom: an emitted 'function' starts mid-body (gen_func_80023384 begins at a delay-slot nop); a guest function is split into two C functions and its prologue store is invisible to its epilogue load
tags: recomp,psxport,partition,debt,measured
created: 2026-08-06
updated: 2026-08-06
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

## What a fix would have to show

The 44 rejects re-classified by hand or by a second proof, and a full gate run proving no new
`recomp-MISS`. Do not land it on the count alone.
