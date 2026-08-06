---
id: 40
title: Upstream's new per-jr `jr $ra` classifier (d2d99ff7, ra_computed_jumps) converted NINE `jr $ra` in MAIN and one in an overlay into computed dispatches. Every site audited was an ORDINARY RETURN.
status: resolved
symptom: After a psxport rebase the port dies at frame ~3544 with a single recomp-MISS at 0x8001E91C (caller ra=0x8001E91C, c->pc=0x80022A2C) and SIGABRT; no local code changed
tags: recomp,psxport,rebase
created: 2026-07-30
updated: 2026-08-06
WHY IT MISFIRES ON THIS GAME. The rule is sound; its PARTITION is not. It reasons per 'function', but the list it is handed is every address the emitter had to make addressable — pointer-target seeds, cross-boundary switch targets, and in one case a `j`'s DELAY SLOT (0x80023384, inside 0x80022A2C). Those split a guest body in half. This game's hand-written renderers have no stack frame at all: they spill $ra to a FIXED GLOBAL save area (`sw $ra, 44($at)`, $at = 0x80077DD8) and reload it in the epilogue. Split the body and the epilogue's fragment cannot see the prologue's store, so `lw $ra, 44($at)` fails the 'is this a save slot' test and reads as a continuation — and a correct `return` is emitted as `rec_dispatch(c, ra)` into the CALLER's mid-function return address, which is not a function entry.
RESOLUTION: the analysis is now OPT-IN (PSXPORT_RECOMP_RA_COROUTINE=1), default off, which the framework already supported cleanly — 'an empty set reproduces the previous output exactly'. The game that needs coroutine `jr $ra` turns it on and audits the printed set, which is what the analysis's own docstring asks of it. Gate back to 16/16, 0 misses, 0 divergences.
METHOD NOTE: what identified this as upstream rather than mine in one run was PSXPORT_NO_NATIVE=1 — the miss reproduced with NO native bodies installed at all, so no local code was even in the picture. Reach for that before reading a single line of the new upstream diff. This is the second instance of docs/issues/0028: run the gate after a REBASE, not only after your own edits.
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-07-30)
RESOLVED WITHOUT THE FIX I WROTE — upstream had already reverted the whole feature two commits later (88f58d7f, 'Revert jr $ra is not always a return until the MDEC decode is fixed'). My opt-in gate was dropped rather than force-fitted onto a rebase that no longer had a call site to gate.

WHAT SURVIVES AND IS WORTH KEEPING, because the feature will be re-landed: the nine false positives here are all ONE class, and the class is not the rule but the PARTITION. The analysis reasons per 'function', but the list it is handed is every address the emitter had to make addressable — pointer-target seeds, cross-boundary switch targets, and in one case a `j`'s DELAY SLOT (0x80023384, inside 0x80022A2C). Those split a guest body. This game's renderers have no stack frame: they spill $ra to a FIXED GLOBAL save area (`sw $ra, 44($at)`, $at = 0x80077DD8) and reload it in the epilogue, so a split body hides the store from the reload and `lw $ra, 44($at)` reads as a continuation. Anyone re-landing it should run it against THIS game and expect the printed set to be empty.

DO NOT re-derive the three partition repairs — all tried and measured here: drop delay-slot starts (9 -> 8), also exclude cross-boundary switch targets (no change), merge fragments that no `jal` targets (removed two, ADDED two new ones). The last one is the important dead end: a rule that moves sites in both directions is not converging on the truth.

METHOD THAT SETTLED OWNERSHIP OF THE BUG IN ONE RUN: PSXPORT_NO_NATIVE=1. The miss reproduced with no native bodies installed at all, so no local code was in the picture — reach for that before reading a line of the upstream diff.

### Note (2026-08-05)
REOPENED 2026-08-05 — RECURRED, same address, same nine sites. The resolution above ('upstream reverted the whole feature') no longer holds: psxport re-landed ra_computed_jumps as item (C) of the recomp-emitter RE-16 work for Spider-Man, unconditionally and with no per-game gate. Symptom is byte-identical to the original report: abort at f3544 (this file says ~3544 already), recomp-MISS 0x8001E91C, ra=0x8001E91C, c->pc=0x80022A2C. What is NEW this time and worth having: the nine were re-audited MECHANICALLY rather than by hand (tools/ra_classes.py, I040 — 9/9 proven ordinary returns by two independent proofs), and the partition defect this file blamed is now split into two distinct framework defects with a named fix each, plus the provenance of the delay-slot seed 0x80023384 (overlay_funcs seeding a jal-shaped DATA word from OV_287800.BIN+67712, and it is the one seeder in emit.py that does not consult is_func_entry). Full write-up and the three-part fix: docs/issues/0046. Do not re-derive this file's three dead-end partition repairs.

### Note (2026-08-06)
CLOSED FOR THE SECOND AND LAST TIME, 2026-08-06 — this time by FIXING the analysis rather than by
upstream reverting it. See docs/issues/0046 for the full write-up and the evidence.

What this file got right and is worth keeping on the record: the rule was sound and the PARTITION was
not. What it got wrong, and what cost two sessions: it concluded the fix therefore had to be a
partition repair, tried three, and measured all three failing (the last one moved sites in both
directions). The actual fix was to stop the analysis DEPENDING on the partition at all — look for the
matching `sw $ra` module-wide keyed by the resolved base, and traverse the basic-block graph instead
of address order. Both of this file's dead-end lessons survive intact; only the conclusion drawn from
them was wrong.

The delay-slot seed 0x80023384 this file blamed is still there and still wrong — it is now
docs/issues/0049 with its measurement (overlay_funcs seeds 223 MAIN addresses, 44 of which fail
is_func_entry). It is no longer FATAL, which is why it was not fixed in the same change.
