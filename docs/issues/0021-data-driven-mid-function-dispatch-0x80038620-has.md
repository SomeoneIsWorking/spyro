---
id: 21
title: Data-driven mid-function dispatch: 0x80038620 has no static reference anywhere
status: open
symptom: [recomp-MISS] 0x80038620 (caller ra=0x80053274, a0=0x92C5). The port's only remaining fail-fast after the computed-jump recogniser landed; reached at ~4234 frames.
tags: recomp,blocker
created: 2026-07-28
updated: 2026-07-28
---

See C055/C056. Distinct from docs/issues/0020 (the computed-offset jump family, now solved in the
recompiler).

WHAT IS ESTABLISHED
  * 0x80038620 is the EPILOGUE of 0x800385BC, an already-recompiled function — so it needs a
    mid-function LABEL, never a seed (a seed splits the body; C051 measured 9.4M unmapped-RAM reads
    doing exactly that with a provably correct address).
  * It has NO static reference of any kind: no literal word, no lui/addiu-built constant, no direct
    jump — across the full resident text and BOTH loaded overlays. Its enclosing function is likewise
    unreferenced.
  * So the address is computed at runtime. The plausible source is a function-pointer table inside the
    level DATA blob loaded from WAD.WAD, which cannot be enumerated from the executable at all.

WHY IT IS NOT ANOTHER RECOGNISER
The computed-jump family was solvable because each dispatcher's base, scale and case set are all in the
instruction stream. Here there is no dispatcher to analyse — the target arrives as data. A recogniser
has nothing to recognise.

OPTIONS, none chosen — this needs a design decision, not a patch:
  1. Emit a label at every basic-block boundary in every recompiled function, so ANY address inside a
     known function is resumable. Correct and general; costs code size and compile time; a spurious
     label is dead code the compiler drops, so the risk profile is mild.
  2. Have rec_dispatch, on a miss, locate the containing recompiled function and enter it at a computed
     label — needs (1) to have emitted the labels anyway.
  3. RE the data table and seed what it contains. Fragile: it is level data, so the set changes per
     level and the port would chase it forever.

Option 1 is the honest general fix. Measure its size/time cost on this game before proposing it
upstream, since it affects every consumer.
