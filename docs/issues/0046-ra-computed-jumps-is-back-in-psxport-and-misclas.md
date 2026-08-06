---
id: 46
title: ra_computed_jumps is back in psxport and misclassifies all 9 of Spyro MAIN's jr-$ra sites — 0x80023ABC dispatches a plain return and the port SIGABRTs at f3544
status: resolved
symptom: port exits on its own at frame 3544, rc=139/signal 6; [recomp-MISS 0] no recompiled fn for 0x8001E91C (ra=0x8001E91C, c->pc=0x80022A2C); gate.sh 90 -> RC=1, 14/17
tags: recomp,psxport,rebase,blocker,dependency
created: 2026-08-05
updated: 2026-08-06
---

## Root cause

psxport re-landed `ra_computed_jumps` (`external/psxport/tools/recomp/emit.py`) as part of the
`recomp-emitter` RE-16 work for Spider-Man — the claim file for that area lists it explicitly as
shape item **(C) revert the revert of d2d99ff7**. It is unconditional: no env gate, no opt-in, no
per-game switch. This is docs/issues/0040 recurring, at the SAME address, with the SAME nine sites.

The analysis classifies a `jr $ra` as a coroutine resume (emitted `rec_dispatch(c, ra)`) rather
than a return. On this executable it fires on 9 of MAIN's 778 `jr $ra`, and **all nine are ordinary
returns**. `tools/ra_classes.py` proves it mechanically; run it, it takes a second and needs no
build:

    python3 tools/ra_classes.py            # -> 9 of 9 computed sites are PROVEN ORDINARY RETURNS
    python3 tools/ra_classes.py --selftest # both classes, so the rules are known to discriminate

Two independent framework defects produce the nine.

**F1 — the "save slot" test is per-FRAGMENT, and this game's save slot is a GLOBAL.**
Spyro's hand-written renderers have no stack frame. They spill `$ra` into a fixed global block:
`sw $ra, 44($at)` with `$at` = `0x80077DD8` (i.e. `0x80077E04`), reloaded by `lw $ra, 44($1)` in the
epilogue. `ra_computed_jumps` calls `lw $ra, N(rX)` a save-slot reload only if some `sw $ra, N(..)`
sits in the SAME partition entry — and its partition is the emitted function list, not the guest
function. A global block is shared across guest functions by construction, so the store is in a
different entry and the load reads as a continuation. 7 of the 9 sites are this, and there are 19
matching `sw $ra,44(0x80077DD8)` stores module-wide that the per-fragment test cannot see.

The runtime evidence is unambiguous — the miss report itself printed
`guest RAM locations holding 0x8001E91C: [0x80077E04]`.

For the site that actually fires, the partition is also wrong for a second reason: guest function
`0x80022A2C` is split at `0x80023384`, which is a **`nop` in the delay slot of `j 0x800232A8`** at
`0x80023380`. It reached the function set through `overlay_funcs()`, which scans overlay BLOBS
word-by-word and seeds every word that decodes as a `jal` into MAIN text, filtered only by
"the target's first word is not UNKNOWN" — a `nop` passes trivially. Provenance:
`OV_287800.BIN` + 67712 = `0x0C008CE1`. `emit.is_func_entry(exe, 0x80023384)` is **False**, and
`overlay_funcs` is the one seeder in emit.py that does not consult it. So the split feeds F1 and
the prologue's store at `0x80022A60` becomes invisible to the epilogue's load at `0x80023A8C`.

**F2 — "reaching definitions" is a linear sweep with no CFG.**
The docstring says reaching-definitions; the code walks addresses in order and treats the last
definition it passed as the reaching one. At `0x80053570` the `bne` at `0x8005358C` jumps over the
`jal` at `0x80053598`, and the two `jr $ra` at `0x800535E0` / `0x80053600` are reachable ONLY
through that skip — `$ra` is untouched on every path that reaches them. The sweep sees the `jal`
anyway. That accounts for the remaining 2 of 9.

## Why it kills the run

`0x80023ABC` is the tail of `0x80022A2C` (`RasterizeSpritePrimQueue`). Its caller
`gen_func_8001E6B8` sets `c->r[31] = 0x8001E91C` and calls it; the emitted body ends with

    { uint32_t _tgt = c->r[31];  rec_dispatch(c, _tgt); return; }   /* coroutine resume, not a return */

`0x8001E91C` is mid-function code inside `0x8001E6B8`, not a function entry, so `rec_dispatch`
misses and `rec_dispatch_miss` aborts. Stack: `gen_func_80012204` <- `gen_func_8001ED5C` <-
`gen_func_8001E6B8`. The other 8 sites are latent, not safe.

## The fix, and where it belongs

Framework, `external/psxport/tools/recomp/emit.py`. This repo cannot make it — the `recomp-emitter`
coordination area is claimed by the spider1 agent and `/home/bhamil/repo/psx/coord/PROTOCOL.md`
forbids a second writer. Three changes, in decreasing order of how much they matter:

1. `ra_computed_jumps`: when the base of `lw $ra, N(rX)` resolves to a link-time constant address,
   look for `sw $ra, N(<same constant>)` MODULE-WIDE, not within the partition entry. A global save
   area is shared across functions by definition, so the per-entry restriction is only sound for
   `sp`-relative slots. `tools/ra_classes.py` Rule A is a working implementation.
2. `ra_computed_jumps`: replace the linear sweep with an actual forward fixpoint over the basic-block
   graph, merging disagreeing paths to UNKNOWN, which the function's own stated default renders as
   `return`. `tools/ra_classes.py` Rule B is a working implementation, and it self-tests against a
   synthetic case that MUST stay `computed`.
3. `overlay_funcs()`: gate its `jal`-target seeds on `is_func_entry`, as `overlay_data_func_pointers`
   already does. The overlay images are game DATA scanned as code; without the gate, any data word
   whose top six bits look like `jal` promotes an arbitrary in-text address to a function entry and
   splits a guest body. `0x80023384` is one; the split is what makes defect 1 fatal here.

Until then this port's `tools/gate.sh` cannot pass, and the 8 latent sites are landmines for any run
that gets further than 3544 frames.

## Dead ends — do not re-derive

* Issue 0040 already audited these nine BY HAND and reached the same verdict. Its three partition
  repairs (drop delay-slot starts, exclude cross-boundary switch targets, merge fragments no `jal`
  targets) are all measured and all fail — the last one moved sites in both directions.
* There is no game-side lever. `emit.py` reads only `PSXPORT_LABEL_ALL`, `PSXPORT_USE_GHIDRA` and
  `PSXPORT_SHARDS`; `ra_computed_jumps` is called unconditionally.
* Putting the splitting addresses into `main_reentry` would suppress site 1 (reentry seeds are
  excluded from the partition) — that is a bandaid, it does not touch the other 8, and for the
  genuinely-`jal`'d fragment starts like `0x8004F7E8` it would be actively wrong.

### Resolution (2026-08-06)
FIXED IN THE FRAMEWORK, 2026-08-06, in tools/recomp/emit.py — no per-game switch. The handoff note in
coord/claims/recomp-emitter/claim.md proposed one ("a per-game switch is the obvious shape"); it is
the wrong shape. A behaviour one consumer needs and another cannot tolerate being UNCONDITIONAL is a
defect in the discriminator, and a flag would have frozen a known-wrong analysis in place for both.

RE FIRST, as the issue asked. Ghidra headless (external/psxport/tools/decomp.sh, project spyro470)
on 0x8001E6B8 and 0x80022A2C settles it with no ambiguity:
  * 0x8001E6B8 decompiles as straight-line C with `FUN_80022a2c();` followed by more statements and a
    plain `return`. The call is `jal 0x80022A2C` at 0x8001E914 with a `nop` delay slot, so
    ra = 0x8001E91C, and 0x8001E91C is `lui $v1,0x8008` — the first instruction of the next statement
    (`if (DAT_80078d7c == 2)`). An ORDINARY CALL RETURN. There is no coroutine here.
  * 0x80022A2C is a FRAMELESS function with a fixed global register-save block at 0x80077DD8: prologue
    0x80022A34-0x80022A60 stores $s0-$s7,$gp,$sp,$fp,$ra at +0..+44; epilogue 0x80023A84-0x80023AB8
    reloads all of them; `jr $ra` at 0x80023ABC. `lw $ra,44($1)` with $1 = 0x80077DD8 is a SAVE-SLOT
    RELOAD whose matching store is at 0x80022A60 — 19 such stores module-wide.

THE TWO FIXES (defects 1 and 2 of the three this issue named):
  1. `ra_global_save_slots()` — the save-slot test is now MODULE-WIDE and keyed by the resolved
     link-time BASE as well as the offset (`ra_const_base()`). A global save block is shared across
     guest functions by construction, so the old per-partition-entry, offset-only test was asking a
     question the guest does not answer. Side effect that matters more than the fix: the analysis is
     now INDEPENDENT of the partition.
  2. The traversal is a forward fixpoint over the BASIC-BLOCK GRAPH (the docstring already claimed
     "reaching-definitions"; the code walked addresses in order). Disagreeing paths merge to
     not-proven = return. Walking the graph needs a `jal` rule, taken from emit_func's own
     `intra_links` so classifier and emitter cannot drift: enter a `jal` target only when it is
     inside this fragment AND not a function entry; never a `bltzal`/`bgezal` target.

Defect 3 (overlay_funcs seeding a delay-slot nop) is NOT fixed and is now docs/issues/0049 with its
measurement (44 of 223 seeds fail is_func_entry). Fix 1 makes it non-fatal; fixing the seeder has its
own regression risk in the opposite direction and should not be landed on a count.

RECOMP_VERSION 2026-08-05.1 -> 2026-08-06.1. The classifier now prints its denominators
unconditionally, including how many `lw $ra` bases it could not resolve.

EVIDENCE, with the negative control.
  RED FIRST, hermetic: three new tests in external/psxport/tools/recomp/test_emit.py
  (test_ra_global_save_slot_survives_a_partition_split,
   test_ra_a_jal_on_a_skipped_path_does_not_poison_a_return,
   test_ra_save_slot_is_matched_by_BASE_not_just_offset) all FAILED against the shipped emitter and
  pass after. Suite 41 -> 44 tests, 44/44 green, including the two pre-existing spider1 RE-16 tests.
  STATIC, same executable, before -> after:  spyro 9 of 778 -> 0 of 778 (790 fragments, 11 global
  save slots, 6 unresolved bases). Every one of the 12 overlay modules also 0.
  NEGATIVE CONTROL, the gate that FAILED: tools/gate.sh 90 BEFORE = RC=1, 15/17, port exited on its
  own rc=139 (signal 6), 3544 frames, 1 recomp-MISS `no recompiled fn for 0x8001E91C (caller
  ra=0x8001E91C, c->pc=0x80022A2C)`.
  AFTER, same command, same disc, headless: 80399 frames, 0 recomp-MISS, 0 native/substrate
  divergences over 160 byte-exact per-call verifications, 0 watchdog stalls. That is 22.7x past the
  frame the port used to die at, and it is the whole point of the fix.
