---
id: C056
kind: claim
status: holds
created: 2026-07-28
tags: recomp,blocker
---

## Claim

The next fail-fast is a data-driven mid-function dispatch — not statically enumerable, unlike the GTE jump family

## Evidence

0x80038620 is dispatched at runtime but NOTHING references it statically: zero literal words, zero lui/addiu-built constants and zero direct jumps across the whole resident text AND both loaded overlays. Its enclosing function 0x800385BC is equally unreferenced, so the region is reached only through addresses computed at runtime — the plausible source being a pointer table inside the level DATA blob loaded off WAD.WAD, which by construction cannot be enumerated from the executable. It sits INSIDE an already-recompiled function (0x800385BC..0x80038638, and 0x80038620 is that function's epilogue), so it needs a mid-function LABEL, not a function seed — but unlike the computed-offset jumps just solved, there is no dispatcher to analyse and therefore no case set to recover. That makes it a different problem class requiring a design decision (e.g. emitting labels at every basic-block boundary so any address inside a known function is resumable), not another recogniser.

## What would falsify it

Finding a static reference to 0x80038620 or to 0x800385BC anywhere, which would mean it IS enumerable and I mis-scanned.
