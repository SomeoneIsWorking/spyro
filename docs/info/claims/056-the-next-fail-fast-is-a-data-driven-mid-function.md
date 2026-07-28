---
id: C056
kind: claim
status: falsified
created: 2026-07-28
tags: recomp,blocker
falsified_on: 2026-07-28
---

## Claim

The next fail-fast is a data-driven mid-function dispatch — not statically enumerable, unlike the GTE jump family

## Evidence

0x80038620 is dispatched at runtime but NOTHING references it statically: zero literal words, zero lui/addiu-built constants and zero direct jumps across the whole resident text AND both loaded overlays. Its enclosing function 0x800385BC is equally unreferenced, so the region is reached only through addresses computed at runtime — the plausible source being a pointer table inside the level DATA blob loaded off WAD.WAD, which by construction cannot be enumerated from the executable. It sits INSIDE an already-recompiled function (0x800385BC..0x80038638, and 0x80038620 is that function's epilogue), so it needs a mid-function LABEL, not a function seed — but unlike the computed-offset jumps just solved, there is no dispatcher to analyse and therefore no case set to recover. That makes it a different problem class requiring a design decision (e.g. emitting labels at every basic-block boundary so any address inside a known function is resumable), not another recogniser.

## What would falsify it

Finding a static reference to 0x80038620 or to 0x800385BC anywhere, which would mean it IS enumerable and I mis-scanned.

## FALSIFIED 2026-07-28

The symptom description was right (no static reference anywhere) but the diagnosis was wrong. I suspected a function-pointer table inside level DATA loaded off the disc. It is not data at all: a new RAM scan finds the value stored NOWHERE, and the address is a RETURN CONTINUATION held in a3 at a  tail-return (gen_func_80053570 ends with rec_dispatch(c, c->r[7])). 0x80038620 is exactly the instruction after 'jal 0x800530C0' in the function that called into this path. Superseded by C058. The 'options' recorded in docs/issues/0021 — universal basic-block labels and friends — address the wrong problem: no amount of labelling helps if the recompiler is treating a return as a call.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
